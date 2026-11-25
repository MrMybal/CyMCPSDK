#include "test.hpp"
#include <cymcp/ai.hpp>
#include <cymcp/local.hpp>
#include <cymcp/mcp.hpp>
#include <fstream>
#include <httplib.h>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
using namespace cymcp;
struct FixtureServer {
    httplib::Server server;
    std::thread thread;
    std::string base;
    void start() {
        int port = server.bind_to_any_port("127.0.0.1");
        if (port < 1)
            throw std::runtime_error("Fixture server bind failed");
        base = "http://127.0.0.1:" + std::to_string(port);
        thread = std::thread([&] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~FixtureServer() {
        server.stop();
        if (thread.joinable())
            thread.join();
    }
};
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            return 2;
        std::string fixture = argv[1];
#ifndef _WIN32
        {
            auto marker =
                std::filesystem::temp_directory_path() / ("cymcp-group-" + random_token(16));
            int lock = -1;
            {
                Process process({fixture, {"--exit-with-descendant", path_utf8(marker)}});
                check(process.wait() == 0 && !process.running(), "Group leader has exited");
                lock = open(marker.c_str(), O_RDWR);
                check(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) < 0 && errno == EWOULDBLOCK,
                      "Descendant keeps its resource after the group leader exits");
            }
            const auto until = Clock::now() + std::chrono::seconds(2);
            int acquired;
            while ((acquired = flock(lock, LOCK_EX | LOCK_NB)) < 0 && Clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            close(lock);
            std::filesystem::remove(marker);
            check(acquired == 0, "Destroying the owner also terminates surviving descendants");
        }
#endif
        {
            Process process(
                {fixture,
                 {"--echo-args", "spaces and \"quotes\" ", "$(not-a-shell); & | `literal`"}});
            process.close_input();
            ProcessLine line;
            check(process.next(line), "Process returns output");
            auto args = Json::parse(line.text);
            check(args[2] == "$(not-a-shell); & | `literal`", "No shell interpolation");
            check(process.wait() == 0, "Process exits cleanly");
        }
        {
            Process process({fixture, {"--silent"}});
            Cancellation cancel;
            cancel.deadline = Clock::now() + std::chrono::milliseconds(100);
            ProcessLine line;
            expect_error("Timeout", [&] { process.next(line, cancel); });
            process.terminate();
        }
        {
            Process process({fixture, {"--silent"}});
            Cancellation cancel;
            cancel.deadline = Clock::now() + std::chrono::milliseconds(100);
            expect_error("Timeout", [&] { process.write(std::string(1024 * 1024, 'x'), cancel); });
        }
        {
            ProcessOptions config{fixture, {"--overflow"}};
            config.max_line_bytes = 4096;
            Process process(config);
            ProcessLine line;
            expect_error("OutputOverflow", [&] {
                while (process.next(line)) {
                };
            });
        }
        auto operations = std::make_shared<Registry>(Policy::allow_all());
        {
            ProcessOptions options{fixture, {"--empty-lines"}};
            options.max_queue_bytes = 1024;
            Process process(options);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ProcessLine line;
            std::size_t lines = 0;
            while (process.next(line))
                ++lines;
            check(lines == 1024 * 1024,
                  "A full output queue applies backpressure instead of failing the turn");
        }
        std::atomic<int> invocations = 0;
        operations->add(
            {"sum",
             "Add numbers",
             {{"type", "object"},
              {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}},
              {"required", {"a", "b"}},
              {"additionalProperties", false}},
             {{"type", "object"}},
             true,
             [&](const Json& j, const CallContext&) {
                 ++invocations;
                 return Json{{"sum", j["a"].get<double>() + j["b"].get<double>()}};
             }});
        McpServer mcp(*operations);
        auto token = random_token();
        LocalHttpServer server(
            token, [&](const Json& j, const CallContext& c) { return mcp.handle(j, c); });
        int port = server.start();
        auto base = "http://127.0.0.1:" + std::to_string(port);
        {
            HttpRequest req;
            req.url = base + "/mcp";
            req.method = "POST";
            req.body = "{}";
            req.headers["Content-Type"] = "application/json";
            check(HttpClient().perform(req).status == 401, "HTTP authentication required");
            req.headers["Authorization"] = "Bearer " + token;
            req.headers["Origin"] = "http://evil.invalid";
            check(HttpClient().perform(req).status == 403, "Browser origin rejected");
        }
        expect_error("InsecureTransport", [] { parse_url("http://example.com/v1"); });
        expect_error("InvalidUrl", [] { parse_url("https://user:secret@example.com/"); });
        {
            std::atomic_bool cancelled = false;
            LocalHttpServer long_request(
                token, [&](const Json&, const CallContext& context) -> Json {
                    try {
                        while (true) {
                            context.cancellation.check();
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    } catch (const Error&) {
                        cancelled = true;
                        throw;
                    }
                });
            int local_port = long_request.start();
            HttpRequest req;
            req.method = "POST";
            req.url = "http://127.0.0.1:" + std::to_string(local_port) + "/v1";
            req.body = "{}";
            req.headers = {{"Authorization", "Bearer " + token},
                           {"Content-Type", "application/json"}};
            req.timeout = std::chrono::milliseconds(100);
            expect_error("Timeout", [&] { HttpClient().perform(req); });
            auto deadline = Clock::now() + std::chrono::seconds(2);
            while (!cancelled && Clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(cancelled, "Closing HTTP request propagates cooperative cancellation");
            long_request.stop();
        }
        {
            McpClient client(base + "/mcp", token);
            client.connect();
            check(client.list_tools().size() == 1, "MCP discovery");
            auto result = client.call_tool("sum", {{"a", 4}, {"b", 5}});
            check(result["structuredContent"]["sum"] == 9, "MCP execution");
            check(client.call_tool("sum", {{"a", "invalid"}, {"b", 5}})["isError"] == true,
                  "MCP validation errors");
            Registry app_a(Policy::allow_all());
            app_a.add({"appB.sum",
                       "Delegate to app B",
                       {{"type", "object"}},
                       {{"type", "object"}},
                       true,
                       [&](const Json& args, const CallContext& context) {
                           return client.call_tool("sum", args, context.cancellation)
                               .at("structuredContent");
                       }});
            check(app_a.invoke("appB.sum", {{"a", 1}, {"b", 2}})["sum"] == 3,
                  "App A delegates a typed command to app B");
        }
        {
            CodexOptions options;
            options.process = {fixture, {"--silent"}};
            options.connect_timeout = std::chrono::milliseconds(100);
            CodexProvider provider(options);
            expect_error("Timeout", [&] { provider.connect(); });
        }
        {
            CodexOptions options;
            options.process = {fixture, {"--codex-fixture"}};
            options.tools = operations;
            auto provider = std::make_shared<CodexProvider>(options);
            Conversation chat(provider, "fixture-model");
            chat.connect();
            check(provider->list_models().size() == 1, "Codex model catalogue");
            int deltas = 0;
            CallContext context;
            context.events = [&](const Event& e) {
                if (e.type == "TextDelta")
                    ++deltas;
            };
            check(chat.send("hello", context).text == "Hello fixture", "Codex turn");
            check(chat.send("tool").text == "Hello fixture" && invocations == 3,
                  "Codex dynamic tool uses registry");
            check(deltas == 1, "Codex streaming without duplicate snapshot");
            Executor worker(1);
            auto future = chat.send_async(worker, "silent");
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            chat.cancel();
            expect_error("Cancelled", [&] { future.get(); });
            check(chat.send("again").text == "Hello fixture",
                  "Codex connection reusable after interrupt");
            std::promise<void> release_worker;
            auto ready = release_worker.get_future();
            worker.post([&] { ready.wait(); });
            auto queued = chat.send_async(worker, "silent");
            chat.cancel();
            release_worker.set_value();
            expect_error("Cancelled", [&] { queued.get(); });
            provider->disconnect();
        }
        {
            ClaudeOptions options;
            options.process = {fixture, {"--claude-fixture"}};
            options.persistent = false;
            auto provider = std::make_shared<ClaudeProvider>(options);
            Conversation chat(provider, "fixture-model");
            chat.connect();
            check(chat.send("hello").text == "Hello fixture", "Claude turn");
            check(chat.session().id == "claude-fixture-session", "Claude native session");
            check(chat.send("second").text == "Hello fixture", "Claude resume");
        }
        {
            ClaudeOptions options;
            options.process = {fixture, {"--claude-fixture", "--fixture-fail"}};
            options.persistent = false;
            auto provider = std::make_shared<ClaudeProvider>(options);
            Conversation chat(provider, "fixture-model");
            chat.connect();
            expect_error("ProviderError", [&] { chat.send("hello"); });
        }
        {
            ClaudeOptions options;
            options.process = {fixture, {"--claude-fixture", "--fixture-crash"}};
            options.persistent = false;
            auto provider = std::make_shared<ClaudeProvider>(options);
            Conversation chat(provider, "fixture-model");
            chat.connect();
            expect_error("ProcessCrash", [&] { chat.send("hello"); });
        }
        {
            FixtureServer fake;
            const std::string component = "verified native component fixture";
            fake.server.Get("/component", [&](const auto&, auto& res) {
                res.set_content(component, "application/octet-stream");
            });
            fake.server.Get("/redirect", [&](const auto&, auto& res) {
                res.set_redirect(fake.base + "/component");
            });
            fake.server.Post("/echo", [](const auto& req, auto& res) {
                res.set_header("X-Client-Agent", req.get_header_value("User-Agent"));
                res.set_content(req.body, "application/octet-stream");
            });
            fake.server.Get("/rejected", [](const auto&, auto& res) {
                res.status = 400;
                res.set_content("rejected body", "text/plain");
            });
            fake.server.Get("/v1/models", [](const auto&, auto& res) {
                res.set_content("{\"data\":[{\"id\":\"fixture-model\"}]}", "application/json");
            });
            fake.server.Post("/v1/chat/completions", [](const auto& req, auto& res) {
                auto payload = parse_json(req.body, 8 * 1024 * 1024);
                auto last = payload["messages"].back();
                std::string stream;
                auto event = [&](Json delta, Json reason = nullptr) {
                    stream +=
                        "data: " +
                        Json({{"choices",
                               Json::array(
                                   {{{"index", 0}, {"delta", delta}, {"finish_reason", reason}}})}})
                            .dump() +
                        "\n\n";
                };
                if (last.value("role", std::string()) == "tool") {
                    event({{"content", "Computed 5"}});
                    event(Json::object(), "stop");
                } else if (last.value("content", std::string()) == "use-tool") {
                    event({{"tool_calls",
                            Json::array(
                                {{{"index", 0},
                                  {"id", "call-1"},
                                  {"function", {{"name", "sum"}, {"arguments", "{\"a\":"}}}}})}});
                    event({{"tool_calls",
                            Json::array(
                                {{{"index", 0}, {"function", {{"arguments", "2,\"b\":3}"}}}}})}});
                    event(Json::object(), "tool_calls");
                } else {
                    event({{"content", "Hello HTTP"}});
                    if (last.value("content", std::string()) != "broken")
                        event(Json::object(), "stop");
                }
                stream += "data: [DONE]\n\n";
                res.set_content(stream, "text/event-stream");
            });
            fake.start();
            {
                HttpRequest request;
                request.url = fake.base + "/redirect";
                check(HttpClient().perform(request).status == 302,
                      "HTTP redirects remain explicit");
                request.url = fake.base + "/component";
                request.method = "HEAD";
                auto response = HttpClient().perform(request);
                check(response.status == 200 && response.body.empty(),
                      "HTTP HEAD preserves status without a response body");
                request.method = "POST";
                request.url = fake.base + "/echo";
                request.body = std::string("UTF-8 é\0binary", 15);
                request.timeout = std::chrono::hours(24 * 365);
                response = HttpClient().perform(request);
                check(response.body == request.body &&
                          response.headers.at("x-client-agent") == std::string("CyMCPSDK/") + version,
                      "HTTP preserves binary bodies, client identity and long deadlines");
                request.max_response_bytes = 3;
                expect_error("ResponseTooLarge", [&] { HttpClient().perform(request); });
                request.max_response_bytes = 1024;
                expect_error("Cancelled", [&] {
                    HttpClient().perform(request, {}, [](const char*, std::size_t) { return false; });
                });
            }
            {
                HttpRequest request;
                request.url = fake.base + "/rejected";
                std::string streamed;
                const auto receive = [&](const char* data, std::size_t size) {
                    streamed.append(data, size);
                    return true;
                };
                auto response = HttpClient().perform(request, {}, receive);
                check(response.status == 400 && response.body.empty() &&
                          streamed == "rejected body",
                      "Default HTTP sink still receives error bodies");
                streamed.clear();
                request.error_body_limit = 64;
                response = HttpClient().perform(request, {}, receive);
                check(response.status == 400 && streamed.empty() &&
                          response.body == "rejected body",
                      "Streaming HTTP errors are buffered separately from generation output");
                request.error_body_limit = 3;
                response = HttpClient().perform(request, {}, receive);
                check(response.status == 400 && streamed.empty() && response.body.empty(),
                      "Oversized error retains its status and discards its untrusted body");
                request.url = fake.base + "/component";
                response = HttpClient().perform(request, {}, receive);
                check(response.status == 200 && response.body.empty() && streamed == component,
                      "Successful content streams past the error-body limit");
            }
            HttpChatOptions options;
            options.base_url = fake.base + "/v1";
            options.tools = operations;
            auto provider = std::make_shared<HttpChatProvider>(options);
            Conversation chat(provider, "fixture-model", "Be helpful");
            chat.connect();
            check(provider->list_models().size() == 1, "HTTP models");
            int pieces = 0;
            CallContext context;
            context.events = [&](const Event& e) {
                if (e.type == "TextDelta")
                    ++pieces;
            };
            check(chat.send("hello", context).text == "Hello HTTP" && pieces == 1,
                  "HTTP token streaming");
            check(chat.send("use-tool").text == "Computed 5", "HTTP tool round-trip");
            expect_error("UnexpectedEOF", [&] { chat.send("broken"); });
            auto cache = std::filesystem::current_path() / ("dependency-test-" + random_token(16));
            DependencyOptions dependencies;
            dependencies.cache_directory = cache;
            dependencies.policy = Policy::allow_all();
            dependencies.allow_loopback_http_for_tests = true;
            DependencyManager manager(dependencies);
            Package package;
            package.id = "fixture";
            package.version = "1.0";
            package.platform = platform_id();
            package.artifacts.push_back({"bin/component.txt", fake.base + "/component",
                                         sha256(component), component.size(), false});
            auto installed = manager.install(package);
            check(manager.verify(package), "Dependency integrity verified");
            check(manager.install(package) == installed, "Dependency cache reused");
            {
                std::ofstream file(installed / "bin/component.txt");
                file << "tampered";
            }
            check(!manager.verify(package), "Dependency tamper detected");
            expect_error("CorruptInstallation", [&] { manager.install(package); });
            manager.uninstall(package);
            auto installer = package;
            installer.version = "installer";
            installer.artifacts[0].relative_path = "setup.exe";
            installer.artifacts[0].executable = true;
            installer.installer = "setup.exe";
            installer.requires_elevation = true;
            manager.install(installer);
            auto scoped_options = dependencies;
            scoped_options.policy =
                Policy([](const CallContext&, const std::string& operation, const Json& request) {
                    return operation == "dependencies.runInstaller" &&
                                   request.at("installer") == "setup.exe" &&
                                   request.at("arguments") == Json::array({"--runtime"}) &&
                                   request.at("requires_elevation") == true
                               ? Decision::allow
                               : Decision::deny;
                });
            DependencyManager scoped(scoped_options);
            installer.installer_arguments = {"--runtime"};
            bool launched = false;
            auto elevate = [&](const ProcessOptions& process, const CallContext&) {
                launched = process.executable.filename() == "setup.exe" &&
                           process.arguments == std::vector<std::string>{"--runtime"};
                return 0;
            };
            check(scoped.run_installer(installer, {}, elevate) == 0 && launched,
                  "Installer policy and elevation receive the approved invocation");
            launched = false;
            auto alternate = installer;
            alternate.installer_arguments = {"--different"};
            expect_error("AccessDenied", [&] { scoped.run_installer(alternate, {}, elevate); });
            alternate = installer;
            alternate.requires_elevation = false;
            expect_error("AccessDenied", [&] { scoped.run_installer(alternate, {}, elevate); });
            check(!launched, "Denied installer variants never reach elevation");
            expect_error("NeedsElevation", [&] { manager.run_installer(installer); });
            bool restart = false;
            CallContext install_context;
            install_context.events = [&](const Event& e) {
                if (e.type == "RestartRequired")
                    restart = true;
            };
            check(manager.run_installer(
                      installer, install_context,
                      [](const ProcessOptions&, const CallContext&) { return 3010; }) == 3010 &&
                      restart,
                  "Elevation result exposes restart requirement");
            expect_error("InstallFailed", [&] {
                manager.run_installer(installer, {},
                                      [](const ProcessOptions&, const CallContext&) { return 5; });
            });
            manager.uninstall(installer);
            auto bad = package;
            bad.version = "bad";
            bad.artifacts[0].sha256 = std::string(64, '0');
            expect_error("IntegrityError", [&] { manager.install(bad); });
            auto traversal = package;
            traversal.artifacts[0].relative_path = "../outside";
            expect_error("InvalidManifest", [&] { manager.install(traversal); });
            auto ads = package;
            ads.artifacts[0].relative_path = "file:stream";
            expect_error("InvalidManifest", [&] { manager.install(ads); });
            DependencyOptions protected_options = dependencies;
            protected_options.policy = Policy::deny_all();
            DependencyManager protected_manager(protected_options);
            expect_error("AccessDenied", [&] { protected_manager.install(package); });
            check(std::filesystem::is_empty(cache),
                  "Failed downloads leave no installation or stage");
            std::filesystem::remove(cache);
        }
        {
            auto private_dir =
                std::filesystem::current_path() / ("engine-test-" + random_token(16));
            std::filesystem::create_directory(private_dir);
            auto model = private_dir / "model.gguf";
            {
                std::ofstream file(model);
                file << "fixture model";
            }
            LocalEngineOptions options;
            options.process = {fixture, {"--engine-fixture"}};
            options.model = model;
            options.private_work_directory = private_dir;
            options.startup_timeout = std::chrono::seconds(5);
            LocalEngine engine(options);
            engine.start();
            HttpChatOptions chat_options;
            chat_options.base_url = engine.base_url();
            chat_options.api_key = engine.token();
            auto provider = std::make_shared<HttpChatProvider>(chat_options);
            Conversation chat(provider, "fixture-local");
            chat.connect();
            check(chat.send("hello").text == "Local fixture",
                  "Owned local engine launch, authenticated readiness and streaming");
            engine.stop();
            options.process.arguments = {"--silent"};
            LocalEngine loading(options);
            auto starting = std::async(std::launch::async, [&] { loading.start(); });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto before = Clock::now();
            loading.stop();
            expect_error("Cancelled", [&] { starting.get(); });
            check(Clock::now() - before < std::chrono::seconds(2),
                  "Stopping an engine cancels its startup");
            std::filesystem::remove(model);
            check(std::filesystem::is_empty(private_dir),
                  "Engine credential removed after shutdown");
            std::filesystem::remove(private_dir);
        }
        server.stop();
        std::cout << checks << " integration checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
