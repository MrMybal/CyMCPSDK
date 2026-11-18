#include "test.hpp"
#include <cymcp/mcp.hpp>
#include <fstream>
#include <httplib.h>
#include <set>
using namespace cymcp;
using namespace std::chrono_literals;
namespace {
constexpr auto revision = "2026-07-28";
Json request(int id, const std::string& method, Json params = Json::object()) {
    params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", revision},
                       {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
Operation long_operation() {
    return {"long",
            "Wait past the former MCP timeout",
            {{"type", "object"}},
            {{"type", "object"}},
            true,
            [](const Json&, const CallContext& context) {
                const auto finish = Clock::now() + 121s;
                while (Clock::now() < finish) {
                    context.cancellation.check();
                    std::this_thread::sleep_for(250ms);
                }
                return Json{{"finished", true}};
            }};
}
int serve_fixture(const std::filesystem::path& started) {
    int writes = 0, interrupted = 0;
    Registry registry(Policy::allow_all());
    registry.add(long_operation());
    registry.add({"wait",
                  "Wait for cancellation",
                  {{"type", "object"}},
                  {{"type", "object"}},
                  true,
                  [&](const Json&, const CallContext& ctx) -> Json {
                      if (!started.empty())
                          std::ofstream(started) << interrupted + 1;
                      std::cerr << "waiting\n" << std::flush;
                      auto limit = Clock::now() + 5s;
                      while (!ctx.cancellation.cancelled() && Clock::now() < limit)
                          std::this_thread::sleep_for(2ms);
                      if (ctx.cancellation.cancelled())
                          ++interrupted;
                      return Json::object();
                  }});
    registry.add({"increment",
                  "Increment",
                  {{"type", "object"}},
                  {{"type", "object"}},
                  false,
                  [&](const Json&, const CallContext&) {
                      ++writes;
                      return Json::object();
                  }});
    registry.add({"counts",
                  "Counts",
                  {{"type", "object"}},
                  {{"type", "object"}},
                  true,
                  [&](const Json&, const CallContext&) {
                      return Json{{"writes", writes}, {"interrupted", interrupted}};
                  }});
    McpServer(registry).serve_stdio(std::cin, std::cout, 1);
    return 0;
}
void test_long_calls(const std::filesystem::path& executable) {
    Registry registry(Policy::allow_all());
    registry.add(long_operation());
    McpServer mcp(registry);
    auto token = random_token();
    LocalHttpServer server(
        token, [&](const Json& value, const CallContext& ctx) { return mcp.handle(value, ctx); });
    McpClient http("http://127.0.0.1:" + std::to_string(server.start()) + "/mcp", token);
    McpClient stdio(ProcessOptions{executable, {"--stdio-fixture"}});
    auto run = [](McpClient& client) {
        client.connect();
        return client.call_tool("long", Json::object())
            .at("structuredContent")
            .at("finished")
            .get<bool>();
    };
    auto first = std::async(std::launch::async, run, std::ref(stdio));
    check(run(http), "HTTP call succeeds past 120 seconds with default cancellation");
    check(first.get(), "stdio call succeeds past 120 seconds with default cancellation");
}
void stdio_shutdown(const std::filesystem::path& executable) {
    for (bool linger : {false, true}) {
        PrivateFile marker("shutdown.txt", "");
        ProcessOptions options;
        options.executable = executable;
        options.arguments = {"--shutdown-fixture", path_utf8(marker.path()),
                             linger ? "wait" : "exit"};
        options.max_queue_bytes = 1024;
        auto client = std::make_unique<McpClient>(std::move(options));
        client->connect();
        const auto start = Clock::now();
        client.reset();
        std::string flag;
        std::ifstream(marker.path()) >> flag;
        check(flag == (linger ? "eof" : "done"),
              "Client delivers EOF and lets a cooperating peer finish flushing");
        check(Clock::now() - start < 5s, "An unresponsive peer cannot stall shutdown indefinitely");
    }
}
void stdio_calls(const std::filesystem::path& executable) {
    ProcessOptions options{executable, {"--stdio-fixture"}};
    Process wire(options);
    Cancellation limit;
    limit.deadline = Clock::now() + 7s;
    auto send = [&](const Json& message) { wire.write(message.dump() + "\n", limit); };
    auto call = [&](int id, const std::string& name) {
        send(request(id, "tools/call", {{"name", name}}));
    };
    auto cancel = [&](int id) {
        send({{"jsonrpc", "2.0"},
              {"method", "notifications/cancelled"},
              {"params", {{"requestId", id}}}});
    };
    call(1, "wait");
    ProcessLine line;
    check(wire.next(line, limit) && line.stderr_stream && line.text == "waiting",
          "stdio fixture has entered first operation");
    call(2, "increment");
    cancel(2);
    cancel(1);
    call(3, "counts");
    check(wire.next(line, limit) && !line.stderr_stream, "stdio replies after queued cancellation");
    const auto reply = Json::parse(line.text);
    check(reply.at("id") == 3 &&
              reply.at("result").at("structuredContent") == Json{{"writes", 0}, {"interrupted", 1}},
          "Cancelled queued call never runs; cancelled calls emit no replies");
    wire.close_input();
    check(wire.wait(limit) == 0, "stdio fixture exits");

    PrivateFile started("waiting", "");
    options.arguments.push_back(path_utf8(started.path()));
    McpClient client(options);
    client.connect();
    Cancellation timeout;
    timeout.deadline = Clock::now() + 750ms;
    expect_error("Timeout", [&] { client.call_tool("wait", Json::object(), timeout); });
    check(client.call_tool("counts", Json::object()).at("structuredContent").at("interrupted") == 1,
          "Caller deadline cancels only that stdio operation and keeps its process");
    Cancellation stop;
    auto active = std::async(std::launch::async,
                             [&] { return client.call_tool("wait", Json::object(), stop); });
    int waiting = 0;
    auto limit_start = Clock::now() + 4s;
    while (waiting != 2 && Clock::now() < limit_start) {
        std::ifstream(started.path()) >> waiting;
        std::this_thread::sleep_for(5ms);
    }
    stop.cancel();
    check(waiting == 2, "Explicit cancellation is exercised after the peer entered its handler");
    expect_error("Cancelled", [&] { active.get(); });
    check(client.call_tool("counts", Json::object()).at("structuredContent").at("interrupted") == 2,
          "Explicit cancel notifies peer and keeps the connection");
}
void server_requests() {
    Registry registry(Policy::allow_all());
    std::atomic<int> entered = 0;
    registry.add({"overlap",
                  "Wait until both clients enter",
                  {{"type", "object"}},
                  {{"type", "object"}},
                  true,
                  [&](const Json&, const CallContext& c) {
                      ++entered;
                      auto limit = Clock::now() + 2s;
                      while (entered.load() < 2 && Clock::now() < limit)
                          std::this_thread::sleep_for(2ms);
                      return Json{{"overlap", entered.load() >= 2}, {"principal", c.principal}};
                  }});
    McpServer mcp(registry);
    auto token = random_token();
    LocalHttpServer server(token,
                           [&](const Json& j, const CallContext& c) { return mcp.handle(j, c); });
    auto start = [&] { return "http://127.0.0.1:" + std::to_string(server.start()) + "/mcp"; };
    auto url = start();
    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"Authorization", "Bearer " + token},
                   {"Content-Type", "application/json"},
                   {"Accept", "application/json, text/event-stream"},
                   {"MCP-Protocol-Version", revision}};
    req.body = request(11, "tools/call", {{"name", "overlap"}}).dump();
    for (const auto& bad : {std::string(), std::string("wrong")}) {
        if (!bad.empty())
            req.headers["Mcp-Method"] = bad;
        auto res = HttpClient().perform(req);
        check(res.status == 400 && parse_json(res.body).at("error").at("code") == -32020,
              "Missing or inconsistent modern method rejected");
    }
    req.headers["Mcp-Method"] = "tools/call";
    auto result = HttpClient().perform(req);
    check(result.status == 400 && parse_json(result.body).at("id") == 11,
          "Named methods require mirrored name");
    req.headers["Mcp-Name"] = "wrong";
    check(HttpClient().perform(req).status == 400 && entered == 0,
          "Name mismatch rejected before dispatch");

    McpClient one(url, token), two(url, token);
    one.connect();
    two.connect();
    auto run = [](McpClient& client) {
        return client.call_tool("overlap", Json::object()).at("structuredContent");
    };
    auto first = std::async(std::launch::async, run, std::ref(one));
    auto second = run(two);
    auto expected = Json{{"overlap", true}, {"principal", "authenticated-local"}};
    check(first.get() == expected && second == expected,
          "Two authenticated clients with identical request IDs can run simultaneously");
    server.stop();
    McpClient restarted(start(), token);
    restarted.connect();
    check(run(restarted) == expected, "Restarted HTTP server creates fresh cancellation context");
}
void legacy_http_sessions() {
    Registry registry(Policy::allow_all());
    std::atomic<int> active = 0;
    std::atomic_bool release = false;
    registry.add({"wait",
                  "Await completion or cancellation",
                  {{"type", "object"}},
                  {{"type", "object"}},
                  true,
                  [&](const Json&, const CallContext& context) {
                      ++active;
                      const auto end = Clock::now() + 3s;
                      while (!release && Clock::now() < end) {
                          context.cancellation.check();
                          std::this_thread::sleep_for(2ms);
                      }
                      return Json{{"principal", context.principal}};
                  }});
    McpServer mcp(registry);
    const auto token = random_token();
    LocalHttpServer server(token, [&](const Json& msg, const CallContext& context) {
        return mcp.handle(msg, context);
    });
    const auto url = "http://127.0.0.1:" + std::to_string(server.start()) + "/mcp";
    auto post = [&](const std::string& session, Json msg) {
        HttpRequest req;
        req.url = url;
        req.timeout = 5s;
        req.method = "POST";
        req.headers = {{"Authorization", "Bearer " + token},
                       {"Content-Type", "application/json"},
                       {"MCP-Protocol-Version", "2025-11-25"}};
        if (!session.empty())
            req.headers["Mcp-Session-Id"] = session;
        req.body = msg.dump();
        return HttpClient().perform(req);
    };
    const Json init = {{"jsonrpc", "2.0"},
                       {"id", 1},
                       {"method", "initialize"},
                       {"params",
                        {{"protocolVersion", "2025-11-25"},
                         {"capabilities", Json::object()},
                         {"clientInfo", {{"name", "independent-test"}, {"version", "1"}}}}}};
    const Json initialized = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
    const Json invoke = {
        {"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"}, {"params", {{"name", "wait"}}}};
    const Json cancel = {
        {"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", {{"requestId", 2}}}};
    std::string first = post("", init).headers["mcp-session-id"];
    std::string second = post("", init).headers["mcp-session-id"];
    check(post(first, initialized).status == 202 && post(second, initialized).status == 202,
          "Legacy peers complete their initialization");
    auto a = std::async(std::launch::async, [&] { return post(first, invoke); });
    auto b = std::async(std::launch::async, [&] { return post(second, invoke); });
    const auto deadline = Clock::now() + 2s;
    while (active != 2 && Clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    check(active == 2, "Both legacy sessions may use the same JSON-RPC ID");
    check(post(first, cancel).status == 202 && a.wait_for(1s) == std::future_status::ready &&
              b.wait_for(0ms) != std::future_status::ready,
          "A separate legacy POST cancels only its session's running request");
    release = true;
    check(Json::parse(a.get().body).at("result").at("isError") == true &&
              Json::parse(b.get().body).at("result").at("structuredContent").at("principal") ==
                  "authenticated-local",
          "Legacy cancellation preserves the other peer and host authorization identity");
    check(!first.empty() && first != second, "Server assigns distinct legacy session IDs");
    check(post("", initialized).status == 400, "A legacy request must echo the issued session ID");
    HttpRequest close;
    close.url = url;
    close.headers = {{"Authorization", "Bearer " + token}, {"Mcp-Session-Id", first}};
    check(HttpClient().perform(close).status == 405,
          "JSON-only server declines the optional standalone legacy event stream");
    close.method = "DELETE";
    check(HttpClient().perform(close).status == 204 && post(first, initialized).status == 404 &&
              post(second, initialized).status == 202,
          "Closing a legacy session retires only its own ID");
    server.stop();
}
void independent_peer() {
    httplib::Server server;
    const std::string name = "caf\xc3\xa9";
    std::atomic<int> calls = 0;
    server.Get("/progress", [](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider("text/plain", [](size_t offset, httplib::DataSink& body) {
            if (offset == 8) {
                body.done();
                return true;
            }
            std::this_thread::sleep_for(60ms);
            return body.write("x", 1);
        });
    });
    server.Post("/mcp", [&](const auto& req, auto& res) {
        auto message = Json::parse(req.body);
        auto method = message.at("method").template get<std::string>();
        Json reply;
        if (req.get_header_value("Mcp-Method") != method ||
            req.get_header_value("MCP-Protocol-Version") != revision ||
            req.has_header("Mcp-Session-Id")) {
            res.status = 400;
            return;
        }
        if (method == "server/discover") {
            res.set_header("Mcp-Session-Id", "ignored-on-stateless-transport");
            reply = {{"supportedVersions", Json::array({revision})}};
        } else if (method == "tools/list") {
            Json sku = {{"type", "integer"}, {"x-mcp-header", "Sku"}};
            Json item = {{"type", "object"}, {"properties", {{"sku", sku}}}};
            Json tag = {{"type", "string"}, {"x-mcp-header", "Tag"}};
            Json schema = {{"type", "object"}, {"properties", {{"item", item}, {"tag", tag}}}};
            Json bad = {{"type", "array"}, {"items", sku}};
            reply = {{"tools", Json::array({{{"name", name}, {"inputSchema", schema}},
                                            {{"name", "invalid"}, {"inputSchema", bad}}})}};
        } else if (method == "tools/call") {
            if (req.get_header_value("Mcp-Name") != "=?base64?Y2Fmw6k=?=" ||
                req.get_header_value("Mcp-Param-Sku") != "42" ||
                req.get_header_value("Mcp-Param-Tag") != "=?base64?IHRhZyA=?=") {
                res.status = 400;
                return;
            }
            ++calls;
            reply = {{"isError", false}};
        }
        res.set_content(
            Json{{"jsonrpc", "2.0"}, {"id", message.at("id")}, {"result", reply}}.dump(),
            "application/json");
    });
    std::promise<void> legacy_started;
    const auto legacy_token = random_token();
    std::atomic_bool session_deleted = false;
    server.Delete("/legacy", [&](const httplib::Request& req, httplib::Response& res) {
        session_deleted = req.get_header_value("Authorization") == "Bearer " + legacy_token &&
                          req.get_header_value("Mcp-Session-Id") == "fixture-session" &&
                          req.get_header_value("MCP-Protocol-Version") == "2025-06-18";
        res.status = session_deleted ? 204 : 400;
    });
    std::atomic<int> legacy_id{0};
    std::atomic_bool legacy_cancelled = false;
    server.Post("/legacy", [&](const httplib::Request& req, httplib::Response& res) {
        const auto message = Json::parse(req.body);
        const auto method = message.at("method").get<std::string>();
        auto reply = [&](const Json& result) {
            res.set_content(
                Json{{"jsonrpc", "2.0"}, {"id", message.at("id")}, {"result", result}}.dump(),
                "application/json");
        };
        if (method == "server/discover") {
            res.status = 400;
            res.set_content(
                R"({"jsonrpc":"2.0","id":null,"error":{"code":-32601,"message":"Initialize first"}})",
                "application/json");
        } else if (method == "initialize") {
            if (message.at("params").at("protocolVersion") != "2025-11-25") {
                res.status = 400;
                return;
            }
            // This independent peer negotiates June even though the client proposes November.
            res.set_header("Mcp-Session-Id", "fixture-session");
            reply({{"protocolVersion", "2025-06-18"}, {"capabilities", Json::object()}});
        } else if (req.get_header_value("Mcp-Session-Id") != "fixture-session" ||
                   req.get_header_value("MCP-Protocol-Version") != "2025-06-18" ||
                   req.has_header("Mcp-Method"))
            res.status = 400;
        else if (method == "notifications/initialized")
            res.status = 202;
        else if (method == "notifications/cancelled") {
            legacy_cancelled = message.at("params").at("requestId") == legacy_id.load();
            res.status = 202;
        } else if (method == "tools/call" && message.at("params").at("name") == "wait") {
            legacy_id = message.at("id").get<int>();
            legacy_started.set_value();
            const auto limit = Clock::now() + 3s;
            while (!legacy_cancelled && Clock::now() < limit)
                std::this_thread::sleep_for(2ms);
            reply({{"isError", true}});
        } else if (method == "tools/call")
            reply({{"isError", false}});
        else
            res.status = 404;
    });
    auto port = server.bind_to_any_port("127.0.0.1");
    check(port > 0, "Independent HTTP peer bound");
    std::jthread listener([&] { server.listen_after_bind(); });
    struct Stop {
        httplib::Server& server;
        ~Stop() {
            server.stop();
        }
    } stop{server};
    server.wait_until_ready();
    HttpRequest progress;
    progress.url = "http://127.0.0.1:" + std::to_string(port) + "/progress";
    progress.timeout = 0ms;
    progress.idle_timeout = 250ms;
    std::size_t seen = 0;
    auto response = HttpClient().perform(progress, {}, [&](const char*, std::size_t bytes) {
        seen += bytes;
        return true;
    });
    check(response.status == 200 && seen == 8,
          "A stream longer than the idle interval stays alive when bytes keep arriving");
    McpClient client("http://127.0.0.1:" + std::to_string(port) + "/mcp", random_token());
    client.connect();
    check(!client.call_tool(name, {{"item", {{"sku", 42}}}, {"tag", " tag "}})
               .at("isError")
               .get<bool>(),
          "A direct call mirrors method, encoded name and nested annotated parameters");
    auto tools = client.list_tools();
    check(tools.size() == 1 && tools[0].at("name") == name,
          "Invalid header annotation excluded from catalogue");
    expect_error("InvalidTool", [&] { client.call_tool("invalid", Json::object()); });
    expect_error("InvalidParameters",
                 [&] { client.call_tool(name, {{"item", {{"sku", 9007199254740992LL}}}}); });
    check(calls == 1, "Rejected requests never reach independent peer");
    {
        McpClient legacy("http://127.0.0.1:" + std::to_string(port) + "/legacy", legacy_token);
        legacy.connect();
        check(
            !legacy.call_tool("echo", Json::object()).at("isError").get<bool>(),
            "Legacy server negotiation and session headers still work without modern annotations");
        Cancellation stop_call;
        auto pending = std::async(std::launch::async, [&] {
            return legacy.call_tool("wait", Json::object(), stop_call);
        });
        check(legacy_started.get_future().wait_for(2s) == std::future_status::ready,
              "Independent legacy peer started the long call");
        stop_call.cancel();
        expect_error("Cancelled", [&] { pending.get(); });
        check(legacy_cancelled &&
                  !legacy.call_tool("echo", Json::object()).at("isError").get<bool>(),
              "SDK delivers an explicit HTTP cancellation and preserves the legacy session");
    }
    check(session_deleted, "Closing a legacy HTTP client deletes its negotiated session");
}
// Optional cross-SDK test. The supplied interpreter must contain CyToolsCore and its MCP extra.
void cytoolscore_interop(const std::filesystem::path& python) {
    PrivateFile scratch("session.marker", "");
    ProcessOptions options;
    options.executable = std::filesystem::absolute(python);
    options.cwd = scratch.path().parent_path();
    options.arguments = {"-m", "cytool_test", "--data-dir", "runtime", "mcp"};
    options.environment["CYTOOLS_TOKEN"] = "";
    options.environment["PYTHONDONTWRITEBYTECODE"] = "1";
    McpClient client(std::move(options));
    Cancellation limit;
    limit.deadline = Clock::now() + 45s;
    client.connect(limit);
    const auto tools = client.list_tools(limit);
    std::set<std::string> names;
    for (const auto& tool : tools)
        names.insert(tool.at("name").get<std::string>());
    check(names.contains("op_echo") && names.contains("cy_get_job") &&
              names.contains("cy_finish_task"),
          "C++ client discovers the Python CyTool operation and job lifecycle");
    auto call = [&](const std::string& name, const Json& args) -> Json {
        const auto reply = client.call_tool(name, args, limit);
        if (reply.value("isError", false))
            throw std::runtime_error("CyTool failed during " + name + ": " +
                                     reply.at("content").dump());
        return reply.at("structuredContent").at("result");
    };

    const Json parameters = {{"text", "CyMCPSDK to CyToolsCore"}};
    const auto submitted = call("op_echo", {{"parameters", parameters}});
    const auto job_id = submitted.at("jobId").get<std::string>();
    check(!job_id.empty(), "Python operation returned a submitted job ID");
    auto job = call("cy_get_job", {{"jobId", job_id}});
    while (job.at("state") != "Completed") {
        if (job.at("state") == "Failed" || job.at("state") == "Cancelled")
            throw std::runtime_error("Python echo did not complete: " + job.at("state").dump());
        limit.check();
        std::this_thread::sleep_for(20ms);
        job = call("cy_get_job", {{"jobId", job_id}});
    }
    check(job.at("outputs") == parameters, "C++ client receives the exact Python output");
    const auto rejected = client.call_tool("op_echo", {{"parameters", {{"text", 42}}}}, limit);
    check(rejected.value("isError", false), "Python rejects incompatible tool arguments");
    const auto finished = call("cy_finish_task", {{"sessionId", submitted.at("sessionId")}});
    check(finished.at("closed") == true && finished.at("cleanupPending") == false,
          "Python finishes its temporary task after the result is consumed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--cytoolscore") {
            cytoolscore_interop(std::filesystem::u8path(argv[2]));
            std::cout << checks << " CyToolsCore interoperability checks passed\n";
            return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "--stdio-fixture")
            return serve_fixture(argc == 3 ? std::filesystem::u8path(argv[2])
                                           : std::filesystem::path{});
        if (argc == 4 && std::string(argv[1]) == "--shutdown-fixture") {
            const auto marker = std::filesystem::u8path(argv[2]);
            serve_fixture({});
            std::ofstream(marker) << "eof";
            for (int i = 0; i < 4096; ++i)
                std::cout << "pending output at shutdown\n";
            std::cout.flush();
            if (std::string(argv[3]) == "wait")
                std::this_thread::sleep_for(10s);
            std::ofstream(marker) << "done";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--long-call") {
            test_long_calls(std::filesystem::absolute(argv[0]));
            std::cout << checks << " long MCP transport checks passed\n";
            return 0;
        }
        stdio_calls(std::filesystem::absolute(argv[0]));
        stdio_shutdown(std::filesystem::absolute(argv[0]));
        server_requests();
        legacy_http_sessions();
        independent_peer();
        std::cout << checks << " MCP transport checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
