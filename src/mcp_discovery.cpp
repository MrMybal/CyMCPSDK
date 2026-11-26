// SPDX-License-Identifier: MIT
#include <cymcp/mcp.hpp>

#include "http_stream.hpp"
#include "mcp_headers.hpp"

#include <tuple>
#ifdef _WIN32
// WinSock must define the Windows types before IP Helper is included.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on
#endif

namespace cymcp {
namespace {
Json response_json(const std::string& body) {
    auto json = Json::parse(body, nullptr, false);
    return json.is_object() ? json : Json::object();
}
class DiscoveryFrames {
    std::string body_, line_, data_;
    Json reply_;
    bool first_ = true;
    void event() {
        if (data_.empty())
            return;
        auto value = response_json(data_);
        data_.clear();
        if (value.value("jsonrpc", Json()) == "2.0" && value.value("id", Json()) == 1 &&
            (value.contains("result") || value.contains("error")))
            reply_ = std::move(value);
    }
    void line() {
        if (!line_.empty() && line_.back() == '\r')
            line_.pop_back();
        if (first_ && line_.starts_with("\xEF\xBB\xBF"))
            line_.erase(0, 3);
        first_ = false;
        if (line_.empty())
            event();
        else if (line_.starts_with("data:")) {
            const auto at = line_.size() > 5 && line_[5] == ' ' ? 6U : 5U;
            data_.append(line_, at);
            data_ += '\n';
        }
        line_.clear();
    }

  public:
    bool feed(const char* bytes, std::size_t size) {
        body_.append(bytes, size);
        for (std::size_t n = 0; n < size; ++n) {
            if (bytes[n] == '\n') {
                line();
                if (!reply_.is_null())
                    throw detail::HttpStreamComplete{};
            } else
                line_ += bytes[n];
        }
        return true;
    }
    Json finish() {
        if (!line_.empty())
            line();
        event();
        return reply_.is_null() ? response_json(body_) : reply_;
    }
};
HttpResponse exchange(HttpRequest request, const Cancellation& cancel) {
    DiscoveryFrames frames;
    request.error_body_limit = request.max_response_bytes;
    auto response = HttpClient().perform(request, cancel, [&](const char* bytes, std::size_t size) {
        return frames.feed(bytes, size);
    });
    if (response.status >= 200 && response.status < 300)
        response.body = frames.finish().dump();
    return response;
}
LocalMcpDiscovery identity(const Json& rpc, bool modern) {
    try {
        if (rpc.value("jsonrpc", Json()) != "2.0" || rpc.value("id", Json()) != 1 ||
            rpc.contains("error"))
            return {};
        const auto result = rpc.value("result", Json());
        if (!result.is_object() || !result.value("capabilities", Json()).is_object())
            return {};
        Json info;
        std::string protocol;
        if (modern) {
            const auto supported = result.value("supportedVersions", Json());
            if (!supported.is_array() ||
                std::find(supported.begin(), supported.end(), mcp_http::modern) == supported.end())
                return {};
            info = result.value("_meta", Json::object())
                       .value("io.modelcontextprotocol/serverInfo", Json());
            protocol = mcp_http::modern;
        } else {
            info = result.value("serverInfo", Json());
            protocol = result.value("protocolVersion", std::string());
            if (!mcp_http::legacy_version(protocol))
                return {};
        }
        if (!info.is_object())
            return {};
        auto name = info.value("name", std::string());
        auto server_version = info.value("version", std::string());
        if (name.empty() || server_version.empty() || name.size() > 256 ||
            server_version.size() > 256)
            return {};
        const auto instructions = result.find("instructions");
        auto summary = instructions != result.end() && instructions->is_string()
                           ? instructions->get<std::string>()
                           : std::string();
        return {LocalMcpStatus::active, std::move(name), std::move(server_version),
                std::move(protocol), std::move(summary)};
    } catch (const Json::exception&) {
        return {};
    }
}
} // namespace

std::vector<LocalMcpListener> windows_mcp_listeners(bool& available) {
    std::vector<LocalMcpListener> result;
    available = false;
#ifdef _WIN32
    const auto list = [&]<class Table>(ULONG family, const auto& add) {
        DWORD size = 0;
        for (int retries = 0; retries < 4; ++retries) {
            std::vector<std::byte> buffer(size);
            const auto status = GetExtendedTcpTable(buffer.empty() ? nullptr : buffer.data(), &size,
                                                    FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
            if (status == ERROR_INSUFFICIENT_BUFFER)
                continue;
            if (status != NO_ERROR || buffer.empty())
                return;
            available = true;
            const auto& table = *reinterpret_cast<const Table*>(buffer.data());
            for (DWORD index = 0; index < table.dwNumEntries; ++index)
                add(table.table[index]);
            return;
        }
    };
    list.operator()<MIB_TCPTABLE_OWNER_PID>(AF_INET, [&](const MIB_TCPROW_OWNER_PID& row) {
        const auto address = ntohl(row.dwLocalAddr);
        if (address != 0 && address != INADDR_LOOPBACK)
            return;
        result.push_back({"http://127.0.0.1:" +
                              std::to_string(ntohs(static_cast<u_short>(row.dwLocalPort))) + "/mcp",
                          row.dwOwningPid});
    });
    list.operator()<MIB_TCP6TABLE_OWNER_PID>(AF_INET6, [&](const MIB_TCP6ROW_OWNER_PID& row) {
        const auto* bytes = row.ucLocalAddr;
        if (!std::all_of(bytes, bytes + 15, [](auto byte) { return byte == 0; }) || bytes[15] > 1)
            return;
        result.push_back({"http://[::1]:" +
                              std::to_string(ntohs(static_cast<u_short>(row.dwLocalPort))) + "/mcp",
                          row.dwOwningPid});
    });
#endif
    return result;
}

LocalMcpDiscovery probe_local_mcp(const std::string& url, const Cancellation& cancellation,
                                  std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0 || timeout > std::chrono::hours(24))
        throw Error("InvalidOptions", "Discovery requires a positive timeout of at most 24 hours");
    cancellation.check();
    auto cancel = cancellation.child();
    cancel.deadline = std::min(cancel.deadline, Clock::now() + timeout);
    try {
        const auto parsed = parse_url(url);
        if (!parsed.loopback)
            return {};
        HttpRequest request;
        request.url = url;
        request.method = "POST";
        request.timeout = timeout;
        request.max_response_bytes = 64 * 1024;
        request.headers = {{"Content-Type", "application/json"},
                           {"Accept", "application/json, text/event-stream"},
                           {"MCP-Protocol-Version", mcp_http::modern},
                           {"Mcp-Method", "server/discover"}};
        request.body = Json{{"jsonrpc", "2.0"},
                            {"id", 1},
                            {"method", "server/discover"},
                            {"params",
                             {{"_meta",
                               {{mcp_http::version_key, mcp_http::modern},
                                {"io.modelcontextprotocol/clientCapabilities", Json::object()}}}}}}
                           .dump();
        const auto authentication = [&] {
            LocalMcpDiscovery result;
            result.status = LocalMcpStatus::authentication_required;
            if (parsed.target != "/mcp")
                return result;
            request.method = "GET";
            request.body.clear();
            request.headers = {{"Accept", "application/json"}};
            const auto host =
                parsed.host.find(':') == std::string::npos ? parsed.host : '[' + parsed.host + ']';
            const auto origin = parsed.scheme + "://" + host + ':' + std::to_string(parsed.port);
            request.url = origin + "/.well-known/cymcp";
            try {
                const auto metadata = HttpClient().perform(request, cancel);
                const auto info = response_json(metadata.body);
                if (metadata.status == 200 && info.value("protocol", std::string()) == "mcp" &&
                    info.value("path", std::string()) == "/mcp" &&
                    info.value("requiresToken", Json()) == true) {
                    const auto name = info.value("name", std::string());
                    const auto server_version = info.value("version", std::string());
                    if (!name.empty() && !server_version.empty() && name.size() <= 256 &&
                        server_version.size() <= 256) {
                        result.name = name;
                        result.version = server_version;
                        result.advertised = true;
                    }
                }
            } catch (const Error&) {
                cancellation.check();
            } catch (const Json::exception&) {
            }
            // A generic 401/403 or /health reply alone cannot identify an MCP server.
            return result;
        };
        auto response = exchange(request, cancel);
        if (response.status == 401 || response.status == 403)
            return authentication();
        if (response.status == 200) {
            auto found = identity(response_json(response.body), true);
            if (found.status == LocalMcpStatus::active)
                return found;
        }
        if (response.status != 200 && response.status != 400 && response.status != 404 &&
            response.status != 405 && response.status != 406 && response.status != 415 &&
            response.status != 501)
            return {};
        request.headers.erase("Mcp-Method");
        request.headers["MCP-Protocol-Version"] = mcp_http::legacy;
        request.body =
            Json{{"jsonrpc", "2.0"},
                 {"id", 1},
                 {"method", "initialize"},
                 {"params",
                  {{"protocolVersion", mcp_http::legacy},
                   {"capabilities", Json::object()},
                   {"clientInfo", {{"name", "CyMCPSDK discovery"}, {"version", version}}}}}}
                .dump();
        response = exchange(request, cancel);
        if (response.status == 401 || response.status == 403)
            return authentication();
        if (response.status != 200)
            return {};
        auto found = identity(response_json(response.body), false);
        if (const auto session = response.headers.find("mcp-session-id");
            session != response.headers.end()) {
            request.method = "DELETE";
            request.body.clear();
            request.headers["Mcp-Session-Id"] = session->second;
            request.headers["MCP-Protocol-Version"] =
                found.protocol.empty() ? mcp_http::legacy : found.protocol;
            try {
                (void)HttpClient().perform(request, cancel);
            } catch (const Error&) {
                cancellation.check();
            }
        }
        return found;
    } catch (const Error&) {
        cancellation.check();
    } catch (const Json::exception&) {
    }
    return {};
}
} // namespace cymcp
