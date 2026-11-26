#include "test.hpp"
#include <cymcp/mcp.hpp>
#include <httplib.h>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace cymcp;
using namespace std::chrono_literals;
namespace {
constexpr auto modern = "2026-07-28";
constexpr auto legacy = "2025-11-25";
struct Fixture {
    httplib::Server server;
    std::thread thread;
    std::string base;
    void start() {
        const auto port = server.bind_to_any_port("127.0.0.1");
        check(port > 0, "Fixture binds loopback");
        base = "http://127.0.0.1:" + std::to_string(port);
        thread = std::thread([&] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~Fixture() {
        server.stop();
        if (thread.joinable())
            thread.join();
    }
};
HttpResponse get(const std::string& url) {
    HttpRequest request;
    request.url = url;
    return HttpClient().perform(request);
}
HttpResponse post(const std::string& url, const std::string& token = {}) {
    HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.body = "{}";
    request.headers["Content-Type"] = "application/json";
    if (!token.empty())
        request.headers["Authorization"] = "Bearer " + token;
    return HttpClient().perform(request);
}
Json rpc(Json result) {
    return {{"jsonrpc", "2.0"}, {"id", 1}, {"result", std::move(result)}};
}
Json legacy_reply() {
    return rpc({{"protocolVersion", legacy},
                {"capabilities", Json::object()},
                {"serverInfo", {{"name", "Legacy fixture"}, {"version", "1"}}},
                {"instructions", "Read-only fixture"}});
}
Json modern_reply() {
    return rpc(
        {{"supportedVersions", Json::array({modern})},
         {"capabilities", Json::object()},
         {"_meta",
          {{"io.modelcontextprotocol/serverInfo", {{"name", "Modern fixture"}, {"version", "1"}}}}},
         {"instructions", "Read-only fixture"}});
}
void advertisements() {
    for (bool tokenless : {false, true}) {
        Registry registry(Policy::allow_all());
        McpServer mcp(registry, "Named local app");
        std::atomic_int discovery{0}, other_calls{0};
        auto handler = [&](const Json& value, const CallContext& context) {
            if (value.value("method", std::string()) == "server/discover")
                ++discovery;
            else
                ++other_calls;
            return mcp.handle(value, context);
        };
        LocalMcpEndpoint advertisement;
        advertisement.name = "Named local app";
        advertisement.version = version;
        const auto token = random_token();
        auto server = tokenless ? std::make_unique<LocalHttpServer>(
                                      LocalHttpServer::UnauthenticatedLoopback{}, handler, 4,
                                      provider_message_limit, advertisement)
                                : std::make_unique<LocalHttpServer>(
                                      token, handler, 4, provider_message_limit, advertisement);
        const auto base = "http://127.0.0.1:" + std::to_string(server->start());
        const auto metadata = get(base + "/.well-known/cymcp");
        const auto info = parse_json(metadata.body);
        check(metadata.status == 200 && metadata.headers.at("cache-control") == "no-store",
              "Public advertisement is available without a token and never cached");
        check(info == Json{{"protocol", "mcp"},
                           {"path", "/mcp"},
                           {"name", "Named local app"},
                           {"version", version},
                           {"requiresToken", !tokenless}},
              "Only the configured identity, MCP path and token requirement are published");
        check(metadata.body.find(token) == std::string::npos, "Advertisement excludes credentials");
        const auto found = probe_local_mcp(base + "/mcp", {}, 2s);
        check(found.name == "Named local app" && found.version == version,
              "Free and protected servers can be identified without credentials");
        check(tokenless
                  ? found.status == LocalMcpStatus::active && found.protocol == modern
                  : found.status == LocalMcpStatus::authentication_required && found.advertised,
              "Probe distinguishes an active free server from an advertised protected server");
        check(other_calls == 0 && discovery == (tokenless ? 1 : 0),
              "Probe never initializes modern sessions or enumerates/calls application tools");
        for (const auto& [header, value] :
             std::vector<std::pair<std::string, std::string>>{{"Origin", "http://localhost"},
                                                              {"Host", "example.invalid"},
                                                              {"Host", "localhost:invalid"}}) {
            HttpRequest request;
            request.url = base + "/.well-known/cymcp";
            request.headers[header] = value;
            check(HttpClient().perform(request).status == 403,
                  "Advertisement retains Origin and Host protection");
        }
        bool available = false;
        const auto listeners = windows_mcp_listeners(available);
#ifdef _WIN32
        check(available && std::any_of(listeners.begin(), listeners.end(),
                                       [&](const auto& item) {
                                           return item.url == base + "/mcp" &&
                                                  item.pid == GetCurrentProcessId();
                                       }),
              "Windows listener inventory contains the real server port and process");
#else
        check(!available && listeners.empty(),
              "Windows-only inventory reports unavailable on POSIX");
#endif
    }
}
void independent_access() {
    Registry registry(Policy::allow_all());
    McpServer mcp(registry, "Independent endpoint");
    std::atomic_bool require_token{false}, enabled{true}, fail{false};
    LocalMcpEndpoint endpoint;
    endpoint.name = "Independent endpoint";
    endpoint.version = "1";
    endpoint.handler = [&](const Json& value, const CallContext& context) {
        return mcp.handle(value, context);
    };
    endpoint.requires_token = [&] { return require_token.load(); };
    endpoint.enabled = [&] {
        if (fail)
            throw std::runtime_error("private callback information");
        return enabled.load();
    };
    const auto token = random_token();
    auto sdk = [](const Json&, const CallContext& context) {
        return Json{{"principal", context.principal}};
    };
    LocalHttpServer server(TokenAuthenticator([&](std::string_view value) {
                               if (value != token)
                                   throw Error("AuthenticationRequired", "Invalid token");
                               return std::string("opaque-user");
                           }),
                           sdk, 4, provider_message_limit, endpoint);
    const auto base = "http://127.0.0.1:" + std::to_string(server.start());
    check(probe_local_mcp(base + "/mcp", {}, 2s).status == LocalMcpStatus::active,
          "Independent MCP endpoint can be tokenless");
    check(post(base + "/v1").status == 401 && post(base + "/mcp").status == 400,
          "Free MCP neither unlocks SDK commands nor dispatches SDK requests on /mcp");
    check(parse_json(post(base + "/v1", token).body).at("principal") == "opaque-user",
          "Authenticated SDK retains the host's principal");
    require_token = true;
    check(probe_local_mcp(base + "/mcp", {}, 2s).advertised,
          "Changing MCP token policy updates protected discovery");
    McpClient client(base + "/mcp", token);
    client.connect();
    check(client.list_tools().empty(), "Protected MCP stays usable through its authenticator");
    enabled = false;
    check(get(base + "/.well-known/cymcp").status == 404 && post(base + "/mcp").status == 404 &&
              post(base + "/v1", token).status == 200,
          "Disabling MCP hides its endpoint and advertisement without disabling the SDK");
    fail = true;
    auto error = get(base + "/.well-known/cymcp");
    check(error.status == 500 && error.body.find("private callback") == error.body.npos,
          "Callback failures reject the request without exposing private exception text");
    LocalMcpEndpoint closed;
    closed.name = "No authenticator";
    closed.version = "1";
    closed.requires_token = [] { return true; };
    LocalHttpServer no_auth(LocalHttpServer::UnauthenticatedLoopback{}, sdk, 4,
                            provider_message_limit, closed);
    const auto no_auth_base = "http://127.0.0.1:" + std::to_string(no_auth.start());
    check(post(no_auth_base + "/mcp").status == 401 && post(no_auth_base + "/v1").status == 200,
          "Requesting token protection without an authenticator never opens MCP access");
    LocalHttpServer legacy_server(LocalHttpServer::UnauthenticatedLoopback{}, sdk);
    const auto legacy_base = "http://127.0.0.1:" + std::to_string(legacy_server.start());
    check(get(legacy_base + "/.well-known/cymcp").status == 404,
          "Existing hosts do not publish an identity unless explicitly configured");
    expect_error("InvalidOptions", [&] {
        LocalHttpServer invalid(LocalHttpServer::UnauthenticatedLoopback{}, sdk, 4,
                                provider_message_limit, LocalMcpEndpoint{"Missing version"});
    });
}
void legacy_and_sse() {
    for (bool stream : {false, true}) {
        std::atomic_int initialized{0}, deleted{0}, unexpected{0};
        Fixture fixture;
        fixture.server.Post("/mcp", [&](const auto& req, auto& res) {
            const auto value = parse_json(req.body);
            if (value.at("method") == "server/discover") {
                res.status = 400;
                return;
            }
            if (value.at("method") != "initialize") {
                ++unexpected;
                res.status = 400;
                return;
            }
            ++initialized;
            res.set_header("Mcp-Session-Id", "discovery-session");
            if (!stream) {
                res.set_content(legacy_reply().dump(), "application/json");
                return;
            }
            std::string data =
                ": heartbeat\r\ndata: "
                "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\r\n\r\n";
            std::istringstream lines(legacy_reply().dump(2));
            for (std::string line; std::getline(lines, line);)
                data += "data: " + line + "\r\n";
            data += "\r\n";
            res.set_chunked_content_provider(
                "text/event-stream",
                [data, sent = false](std::size_t, httplib::DataSink& sink) mutable {
                    if (!sent) {
                        sent = true;
                        return sink.write(data.data(), data.size());
                    }
                    std::this_thread::sleep_for(20ms);
                    return sink.write(": heartbeat\n\n", 13);
                });
        });
        fixture.server.Delete("/mcp", [&](const auto& req, auto& res) {
            if (req.get_header_value("Mcp-Session-Id") == "discovery-session" &&
                req.get_header_value("MCP-Protocol-Version") == legacy)
                ++deleted;
            res.status = 204;
        });
        fixture.start();
        const auto started = Clock::now();
        const auto found = probe_local_mcp(fixture.base + "/mcp", {}, 2s);
        check(found.status == LocalMcpStatus::active && found.name == "Legacy fixture" &&
                  found.protocol == legacy && found.summary == "Read-only fixture",
              "Legacy JSON and multiline SSE replies identify the right RPC after notifications");
        check(initialized == 1 && deleted == 1 && unexpected == 0,
              "Discovery closes the temporary legacy session without calling tools");
        check(Clock::now() - started < 1500ms,
              "SSE discovery completes without waiting for stream closure");
    }
}
void invalid_and_bounded() {
    Fixture fixture;
    Json body = modern_reply();
    std::atomic_int delay{0}, trap{0};
    std::atomic_bool reject{false}, redirect{false};
    fixture.server.Post("/mcp", [&](const auto&, auto& res) {
        if (delay)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay.load()));
        if (redirect) {
            res.set_redirect(fixture.base + "/trap");
            return;
        }
        if (reject) {
            res.status = 401;
            res.set_header("WWW-Authenticate", "Bearer");
            return;
        }
        res.set_content(body.dump(), "application/json");
    });
    fixture.server.Get("/health", [](const auto&, auto& res) {
        res.set_content("{\"status\":\"ready\"}", "application/json");
    });
    fixture.server.Get("/trap", [&](const auto&, auto& res) {
        ++trap;
        res.set_content("{}", "application/json");
    });
    fixture.start();
    check(probe_local_mcp(fixture.base + "/mcp").status == LocalMcpStatus::active,
          "Modern peers are supported without legacy initialize");
    for (int mode = 0; mode < 6; ++mode) {
        body = modern_reply();
        if (mode == 0)
            body["id"] = 99;
        if (mode == 1)
            body["result"]["capabilities"] = nullptr;
        if (mode == 2)
            body["result"]["_meta"] = "invalid";
        if (mode == 3)
            body["result"]["supportedVersions"] = Json::array({"unknown"});
        if (mode == 4)
            body = legacy_reply(), body["result"]["protocolVersion"] = "unknown";
        if (mode == 5)
            body["padding"] = std::string(70 * 1024, 'x');
        check(probe_local_mcp(fixture.base + "/mcp").status == LocalMcpStatus::unverified,
              "Malformed identities, wrong RPC IDs/revisions and oversized replies are rejected");
    }
    reject = true;
    const auto unnamed = probe_local_mcp(fixture.base + "/mcp");
    check(unnamed.status == LocalMcpStatus::authentication_required && !unnamed.advertised &&
              unnamed.name.empty(),
          "A generic Bearer-protected HTTP server is not advertised as MCP by its health reply");
    reject = false;
    redirect = true;
    check(probe_local_mcp(fixture.base + "/mcp").status == LocalMcpStatus::unverified && trap == 0,
          "Discovery does not follow HTTP redirects");
    redirect = false;
    body = legacy_reply();
    delay = 130;
    check(probe_local_mcp(fixture.base + "/mcp", {}, 200ms).status == LocalMcpStatus::unverified,
          "Modern fallback and legacy negotiation share one total time budget");
    delay = 250;
    Cancellation cancel;
    auto running = std::async(std::launch::async, [&] {
        expect_error("Cancelled", [&] { probe_local_mcp(fixture.base + "/mcp", cancel, 2s); });
    });
    std::this_thread::sleep_for(30ms);
    cancel.cancel();
    running.get();
    check(probe_local_mcp("https://example.invalid/mcp").status == LocalMcpStatus::unverified &&
              probe_local_mcp("not-a-url").status == LocalMcpStatus::unverified,
          "Only explicit loopback URLs are probed");
    expect_error("InvalidOptions", [&] { probe_local_mcp(fixture.base + "/mcp", {}, 0ms); });
}
} // namespace
int main() {
    try {
        advertisements();
        independent_access();
        legacy_and_sse();
        invalid_and_bounded();
        std::cout << checks << " MCP discovery checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
