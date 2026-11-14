#include <cymcp/mcp.hpp>
#include <set>
namespace cymcp {
namespace {
constexpr auto modern = "2026-07-28";
constexpr auto legacy = "2025-11-25";
constexpr auto version_key = "io.modelcontextprotocol/protocolVersion";
constexpr auto caps_key = "io.modelcontextprotocol/clientCapabilities";
Json rpc_error(const Json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}
Json meta() {
    return {{version_key, modern},
            {caps_key, Json::object()},
            {"io.modelcontextprotocol/clientInfo", {{"name", "CyMCPSDK"}, {"version", version}}}};
}
} // namespace
McpServer::McpServer(const Registry& registry, std::string name)
    : registry_(registry), name_(std::move(name)) {}
void McpServer::cancel_all() {
    std::lock_guard lock(mutex_);
    for (const auto& [id, cancel] : active_)
        cancel.cancel();
}
Json McpServer::handle(const Json& request, const CallContext& input) {
    Json id = nullptr;
    if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0" ||
        !request.contains("method") || !request["method"].is_string())
        return rpc_error(id, -32600, "Invalid JSON-RPC request");
    bool notification = !request.contains("id");
    if (!notification) {
        id = request["id"];
        if (!id.is_string() && !id.is_number_integer())
            return rpc_error(nullptr, -32600, "Invalid request ID");
    }
    auto method = request["method"].get<std::string>();
    Json params = request.value("params", Json::object());
    if (!params.is_object())
        return notification ? Json() : rpc_error(id, -32602, "Parameters must be an object");
    if (notification) {
        if (method == "notifications/cancelled" && params.contains("requestId")) {
            std::lock_guard lock(mutex_);
            auto it = active_.find(input.principal + ":" + params["requestId"].dump());
            if (it != active_.end())
                it->second.cancel();
        }
        return nullptr;
    }
    try {
        auto metadata = params.value("_meta", Json::object());
        if (!metadata.is_object())
            return rpc_error(id, -32602, "Invalid metadata");
        bool current = metadata.contains(version_key);
        if (current) {
            if (metadata[version_key] != modern) {
                auto reply = rpc_error(id, -32022, "Unsupported protocol version");
                reply["error"]["data"] = {{"supported", Json::array({modern, legacy})},
                                          {"requested", metadata[version_key]}};
                return reply;
            }
            if (!metadata.contains(caps_key) || !metadata[caps_key].is_object())
                return rpc_error(id, -32602, "Client capabilities metadata required");
        }
        Json result;
        if (method == "server/discover") {
            if (!current)
                return rpc_error(id, -32602, "Discovery requires version metadata");
            result = {
                {"supportedVersions", Json::array({modern, legacy})},
                {"capabilities", {{"tools", Json::object()}}},
                {"_meta",
                 {{"io.modelcontextprotocol/serverInfo", {{"name", name_}, {"version", version}}}}},
                {"ttlMs", 0},
                {"cacheScope", "private"}};
        } else if (method == "initialize") {
            if (params.value("protocolVersion", std::string()) != legacy &&
                params.value("protocolVersion", std::string()) != "2025-06-18")
                return rpc_error(id, -32602, "Supported legacy version: 2025-11-25");
            {
                std::lock_guard lock(mutex_);
                initialized_ = true;
            }
            result = {{"protocolVersion", params["protocolVersion"]},
                      {"capabilities", {{"tools", Json::object()}}},
                      {"serverInfo", {{"name", name_}, {"version", version}}}};
        } else {
            if (!current) {
                std::lock_guard lock(mutex_);
                if (!initialized_)
                    return rpc_error(id, -32000, "Initialize the connection first");
            }
            if (method == "ping")
                result = Json::object();
            else if (method == "tools/list") {
                if (params.contains("cursor"))
                    return rpc_error(id, -32602,
                                     "This server returns its bounded tool catalogue in one page");
                result = {{"tools", registry_.describe()}};
                if (current) {
                    result["ttlMs"] = 0;
                    result["cacheScope"] = "private";
                }
            } else if (method == "tools/call") {
                if (!params.contains("name") || !params["name"].is_string() ||
                    (params.contains("arguments") && !params["arguments"].is_object()))
                    return rpc_error(id, -32602, "Tool name and object arguments required");
                CallContext context = input;
                context.request_id = id.dump();
                Cancellation child = input.cancellation.child();
                context.cancellation = child;
                auto key = input.principal + ":" + id.dump();
                {
                    std::lock_guard lock(mutex_);
                    if (active_.size() >= 128)
                        return rpc_error(id, -32001, "Server busy");
                    if (!active_.emplace(key, child).second)
                        return rpc_error(id, -32600, "Request ID already active");
                }
                struct ActiveCleanup {
                    McpServer* self;
                    std::string key;
                    ~ActiveCleanup() {
                        std::lock_guard lock(self->mutex_);
                        self->active_.erase(key);
                    }
                } cleanup{this, key};
                try {
                    auto value =
                        registry_.invoke(params["name"].get<std::string>(),
                                         params.value("arguments", Json::object()), context);
                    result = {
                        {"content", Json::array({{{"type", "text"}, {"text", value.dump()}}})},
                        {"isError", false}};
                    if (value.is_object())
                        result["structuredContent"] = value;
                } catch (const Error& e) {
                    result = {
                        {"content",
                         Json::array(
                             {{{"type", "text"},
                               {"text", Json({{"code", e.code}, {"message", e.what()}}).dump()}}})},
                        {"isError", true}};
                }
            } else
                return rpc_error(id, -32601, "Method not implemented");
        }
        if (current)
            result["resultType"] = "complete";
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
    } catch (const Json::exception&) {
        return rpc_error(id, -32602, "Invalid method parameters");
    } catch (const Error& e) {
        return rpc_error(id, -32603, e.what());
    } catch (...) {
        return rpc_error(id, -32603, "Internal error");
    }
}
void McpServer::serve_stdio(std::istream& in, std::ostream& out, std::size_t workers) {
    std::mutex output;
    CallContext base;
    auto send = [&](const Json& result) {
        if (!result.is_null()) {
            std::lock_guard lock(output);
            out << result.dump() << '\n' << std::flush;
        }
    };
    Executor executor(workers);
    std::string line;
    line.reserve(4096);
    bool oversized = false;
    char ch;
    while (in.get(ch)) {
        if (ch != '\n') {
            if (line.size() < 1024 * 1024)
                line += ch;
            else
                oversized = true;
            continue;
        }
        if (oversized) {
            send(rpc_error(nullptr, -32700, "Message exceeds 1 MiB"));
            line.clear();
            oversized = false;
            continue;
        }
        if (line.empty())
            continue;
        Json request;
        try {
            request = parse_json(line);
        } catch (...) {
            send(rpc_error(nullptr, -32700, "Parse error"));
            line.clear();
            continue;
        }
        line.clear();
        if (request.is_object() && request.value("method", Json()) == "tools/call") {
            try {
                executor.post([&, request] { send(handle(request, base)); });
            } catch (const Error&) {
                send(rpc_error(request.value("id", Json()), -32001, "Server busy"));
            }
        } else
            send(handle(request, base));
    }
    cancel_all();
    base.cancellation.cancel();
}
struct McpClient::Impl {
    ProcessOptions options;
    std::unique_ptr<Process> process;
    std::string url, token, protocol = modern;
    Json session_headers = Json::object();
    std::int64_t counter = 0;
    bool connected = false;
    std::mutex mutex;
    Json exchange(std::string method, Json params, const Cancellation& cancel,
                  bool notify = false) {
        if (protocol == modern)
            params["_meta"] = meta();
        Json message = {
            {"jsonrpc", "2.0"}, {"method", std::move(method)}, {"params", std::move(params)}};
        auto id = ++counter;
        if (!notify)
            message["id"] = id;
        Json reply;
        if (process) {
            process->write(message.dump() + "\n", cancel);
            if (notify)
                return nullptr;
            ProcessLine line;
            while (process->next(line, cancel)) {
                if (line.stderr_stream)
                    continue;
                Json next = parse_json(line.text);
                if (next.contains("method") && next.contains("id")) {
                    process->write(
                        rpc_error(next["id"], -32601, "Client request unsupported").dump() + "\n");
                    continue;
                }
                if (next.value("id", Json()) == id) {
                    reply = std::move(next);
                    break;
                }
            }
        } else {
#ifdef CYMCP_HAS_HTTP
            HttpRequest req;
            req.method = "POST";
            req.url = url;
            req.body = message.dump();
            req.headers = {{"Content-Type", "application/json"},
                           {"Accept", "application/json, text/event-stream"},
                           {"Authorization", "Bearer " + token},
                           {"MCP-Protocol-Version", protocol}};
            for (auto it = session_headers.begin(); it != session_headers.end(); ++it)
                req.headers[it.key()] = it.value().get<std::string>();
            auto response = HttpClient().perform(req, cancel);
            if (response.headers.count("mcp-session-id"))
                session_headers["Mcp-Session-Id"] = response.headers.at("mcp-session-id");
            if (notify && response.status == 202)
                return nullptr;
            if (response.status < 200 || response.status >= 300) {
                try {
                    reply = parse_json(response.body);
                } catch (...) {
                    throw Error("ProtocolError", "MCP HTTP request rejected");
                }
                if (!reply.contains("error"))
                    throw Error("ProtocolError", "MCP HTTP request rejected");
            } else if (response.headers["content-type"].find("text/event-stream") !=
                       std::string::npos) {
                std::istringstream events(response.body);
                std::string line, data;
                while (std::getline(events, line)) {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.rfind("data:", 0) == 0)
                        data += line.substr(5) + "\n";
                    else if (line.empty() && !data.empty()) {
                        auto next = parse_json(data);
                        if (next.value("id", Json()) == id)
                            reply = std::move(next);
                        data.clear();
                    }
                }
            } else
                reply = parse_json(response.body);
#else
            throw Error("Unsupported", "HTTP module disabled");
#endif
        }
        if (!reply.is_object() || reply.value("id", Json()) != id)
            throw Error("UnexpectedEOF", "No matching MCP response");
        return reply;
    }
    Json call(std::string method, Json params, const Cancellation& cancel) {
        auto response = exchange(std::move(method), std::move(params), cancel);
        if (response.contains("error"))
            throw Error("RemoteError", response["error"].dump());
        if (!response.contains("result"))
            throw Error("ProtocolError", "Missing MCP result");
        return response["result"];
    }
};
McpClient::McpClient(ProcessOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
}
#ifdef CYMCP_HAS_HTTP
McpClient::McpClient(std::string url, std::string token) : impl_(std::make_unique<Impl>()) {
    parse_url(url);
    impl_->url = std::move(url);
    impl_->token = std::move(token);
}
#endif
McpClient::~McpClient() = default;
void McpClient::connect(const Cancellation& input) {
    auto cancel = input;
    if (cancel.deadline == Clock::time_point::max())
        cancel.deadline = Clock::now() + std::chrono::seconds(30);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (p.connected)
        throw Error("InvalidState", "MCP client already connected");
    p.protocol = modern;
    p.session_headers = Json::object();
    try {
        if (p.url.empty())
            p.process = std::make_unique<Process>(p.options);
        auto discovery = p.exchange("server/discover", Json::object(), cancel);
        if (discovery.contains("error")) {
            if (discovery["error"].value("code", 0) == -32022)
                throw Error("Unsupported",
                            "MCP server does not support the selected modern revision");
            p.protocol = legacy;
            p.call("initialize",
                   {{"protocolVersion", legacy},
                    {"capabilities", Json::object()},
                    {"clientInfo", {{"name", "CyMCPSDK"}, {"version", version}}}},
                   cancel);
            p.exchange("notifications/initialized", Json::object(), cancel, true);
        } else if (!discovery.contains("result") ||
                   !discovery["result"].contains("supportedVersions"))
            throw Error("ProtocolError", "Invalid discovery response");
        p.connected = true;
    } catch (...) {
        p.process.reset();
        throw;
    }
}
Json McpClient::list_tools(const Cancellation& input) {
    auto cancel = input;
    if (cancel.deadline == Clock::time_point::max())
        cancel.deadline = Clock::now() + std::chrono::seconds(120);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.connected)
        throw Error("InvalidState", "Connect MCP client first");
    Json tools = Json::array();
    Json params = Json::object();
    std::set<std::string> seen;
    for (int page = 0; page < 1000; ++page) {
        auto result = p.call("tools/list", params, cancel);
        if (!result.contains("tools") || !result["tools"].is_array())
            throw Error("ProtocolError", "Missing tool catalogue");
        for (const auto& tool : result["tools"]) {
            tools.push_back(tool);
            if (tools.size() > 10000)
                throw Error("ResponseTooLarge", "Tool catalogue exceeds limit");
        }
        auto cursor = result.value("nextCursor", std::string());
        if (cursor.empty())
            return tools;
        if (!seen.insert(cursor).second)
            throw Error("ProtocolError", "Cyclic tool pagination");
        params["cursor"] = cursor;
    }
    throw Error("ResponseTooLarge", "Too many catalogue pages");
}
Json McpClient::call_tool(const std::string& name, const Json& args, const Cancellation& input) {
    auto cancel = input;
    if (cancel.deadline == Clock::time_point::max())
        cancel.deadline = Clock::now() + std::chrono::seconds(120);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.connected)
        throw Error("InvalidState", "Connect MCP client first");
    try {
        return p.call("tools/call", {{"name", name}, {"arguments", args}}, cancel);
    } catch (const Error& e) {
        if (e.code == "Cancelled" || e.code == "Timeout" || e.code == "UnexpectedEOF") {
            p.connected = false;
            p.process.reset();
        }
        throw;
    }
}
} // namespace cymcp
