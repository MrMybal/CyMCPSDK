#include "test.hpp"
#include <cymcp/ai.hpp>
#include <httplib.h>
using namespace cymcp;
struct Server {
    httplib::Server http;
    std::thread worker;
    std::string url;
    void start() {
        auto port = http.bind_to_any_port("127.0.0.1");
        if (port <= 0)
            throw std::runtime_error("Fixture bind failed");
        url = "http://127.0.0.1:" + std::to_string(port);
        worker = std::thread([&] { http.listen_after_bind(); });
        http.wait_until_ready();
    }
    ~Server() {
        http.stop();
        if (worker.joinable())
            worker.join();
    }
};
std::string sse(const Json& value) {
    return "data: " + value.dump() + "\n\n";
}
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            return 2;
        std::string fixture = argv[1];
        ProviderRegistry registry;
        auto catalogue = registry.describe();
        check(catalogue.size() == 15, "Built-in catalogue has fifteen implemented profiles");
        expect_error("UnknownProvider", [&] { registry.create("unregistered"); });
        expect_error("DuplicateProvider", [&] {
            registry.add({"openai", "Duplicate", "http", "", {}},
                         [](const auto&) { return std::shared_ptr<Provider>(); });
        });
        ProviderConfig invalid;
        invalid.settings["api_key"] = "do-not-persist";
        expect_error("InvalidOptions", [&] { registry.create("openai", invalid); });
        invalid.settings = {{"max_tool_rounds", "invalid-type"}};
        expect_error("InvalidOptions", [&] { registry.create("openai", invalid); });
        invalid.settings = {{"max_tool_rounds", -1}};
        expect_error("InvalidOptions", [&] { registry.create("openai", invalid); });
        registry.add({"null-test", "Null", "test", "", {}},
                     [](const auto&) { return std::shared_ptr<Provider>(); });
        expect_error("InvalidProvider", [&] { registry.create("null-test"); });
        ProviderRegistry empty(false);
        check(empty.describe().empty(), "Host can select only its required providers");
        for (bool agy : {false, true}) {
            auto id = agy ? "antigravity" : "opencode";
            auto config_for = [&](std::string flag = "", bool allow = false) {
                ProviderConfig config;
                config.process = {fixture, {"--agent-fixture"}};
                if (agy)
                    config.process.arguments.push_back("--agy");
                if (!flag.empty())
                    config.process.arguments.push_back(flag);
                if (allow)
                    config.process.arguments.push_back("--fixture-allow");
                config.settings["allow_all_actions"] = allow;
                return config;
            };
            auto provider = registry.create(id, config_for());
            provider->connect();
            check(provider->list_models().size() == 2, "CLI model catalogue comes from executable");
            Conversation chat(provider, agy ? "fixture-model" : "fixture/model");
            chat.connect();
            std::string literal = "spaces \"quotes\" ; $(not-a-shell) & | `literal` é";
            check(chat.send(literal).text == literal,
                  "CLI sends prompt literally and enforces deny baseline");
            check(chat.session().id == "agent-fixture-session", "CLI captures native session ID");
            auto resumed = registry.create(id, config_for());
            Conversation second(resumed, "fixture/model");
            second.connect(chat.session().id);
            check(second.send("resumed").text == "resumed", "CLI resumes the exact native session");
            Conversation unrestricted(registry.create(id, config_for("", true)), "fixture/model");
            unrestricted.connect();
            check(unrestricted.send("allowed").text == "allowed",
                  "Explicit allow-all reaches native provider flags");
            for (const auto& [flag, error] : std::vector<std::pair<std::string, std::string>>{
                     {"--fixture-fail", "ProviderError"},
                     {"--fixture-crash", "ProcessCrash"},
                     {"--fixture-truncated", "UnexpectedEOF"}}) {
                Conversation broken(registry.create(id, config_for(flag)), "fixture/model");
                broken.connect();
                expect_error(error, [&] { broken.send("partial failure"); });
            }
            Conversation mismatch(registry.create(id, config_for("--fixture-wrong-session")),
                                  "fixture/model");
            mismatch.connect("agent-fixture-session");
            expect_error("SessionInvalid", [&] { mismatch.send("resume"); });
            if (!agy) {
                auto v2 = registry.create(id, config_for("--fixture-version2"));
                expect_error("UnsupportedVersion", [&] { v2->connect(); });
            }
            auto slow = registry.create(id, config_for("--fixture-silent-turn"));
            Conversation cancellable(slow, "fixture/model");
            cancellable.connect();
            Executor worker(1);
            auto future = cancellable.send_async(worker, "wait");
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            cancellable.cancel();
            expect_error("Cancelled", [&] { future.get(); });
            auto disconnecting = cancellable.send_async(worker, "wait again");
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            slow->disconnect();
            expect_error("Cancelled", [&] { disconnecting.get(); });
            auto silent = config_for();
            silent.process.arguments = {"--silent"};
            silent.settings["connect_timeout_ms"] = 100;
            expect_error("Timeout", [&] { registry.create(id, silent)->connect(); });
        }
        auto tools = std::make_shared<Registry>(Policy::allow_all());
        int invoked = 0;
        tools->add({"app.sum",
                    "Add numbers",
                    {{"type", "object"},
                     {"properties", {{"a", {{"type", "integer"}}}}},
                     {"required", {"a"}},
                     {"additionalProperties", false}},
                    {{"type", "object"}},
                    true,
                    [&](const Json& args, const CallContext&) {
                        ++invoked;
                        return Json{{"sum", args.at("a").get<int>() + 1}};
                    }});
        auto wire_name = "cy_" + sha256("app.sum").substr(0, 32);
        Server server;
        std::atomic<int> key_probes = 0, model_pages = 0, requests = 0;
        std::atomic<bool> invalid_body = false;
        std::atomic<int> mode = 0;
        server.http.Get("/key", [&](const auto& req, auto& res) {
            ++key_probes;
            if (req.get_header_value("Authorization") != "Bearer test-key")
                res.status = 401;
            res.set_content("{\"data\":{}}", "application/json");
        });
        server.http.Get("/models", [&](const auto& req, auto& res) {
            ++model_pages;
            bool anthropic = req.has_header("x-api-key");
            if (anthropic && req.get_header_value("anthropic-version") != "2023-06-01")
                invalid_body = true;
            if ((anthropic ? req.get_header_value("x-api-key")
                           : req.get_header_value("Authorization")) !=
                (anthropic ? "test-key" : "Bearer test-key")) {
                res.status = 401;
                return;
            }
            bool next = req.has_param("after_id");
            Json data = {
                {"data", Json::array({{{"id", next ? "fixture-other" : "fixture-model"}}})}};
            if (anthropic) {
                data["has_more"] = !next || mode == 6;
                data["last_id"] = "cursor/with space";
                if (next && req.get_param_value("after_id") != "cursor/with space")
                    invalid_body = true;
            }
            res.set_content(data.dump(), "application/json");
        });
        server.http.Get("/api/tags", [](const auto&, auto& res) {
            res.set_content(
                "{\"models\":[{\"name\":\"fixture-model\",\"model\":\"fixture-model\"}]}",
                "application/json");
        });
        auto reject = [&](auto& res) {
            if (mode == 4 || mode == 5) {
                res.status = mode == 4 ? 401 : 429;
                res.set_content("{\"private-account-data\":true}", "application/json");
                return true;
            }
            return false;
        };
        server.http.Post("/responses", [&](const auto& req, auto& res) {
            ++requests;
            if (reject(res))
                return;
            if (mode == 7) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                res.set_content("", "text/event-stream");
                return;
            }
            auto body = parse_json(req.body);
            if (body.at("store") != false || body.at("model") != "fixture-model" ||
                !body.at("input").is_array() || body.at("tools").at(0).at("name") != wire_name)
                invalid_body = true;
            bool output = false;
            bool preserved_reasoning = false;
            for (const auto& item : body.at("input"))
                if (item.value("type", std::string()) == "function_call_output") {
                    output = true;
                    if (item.at("call_id") != "call-1" ||
                        parse_json(item.at("output").template get<std::string>()).at("sum") != 42)
                        invalid_body = true;
                } else if (item.value("type", std::string()) == "reasoning")
                    preserved_reasoning = item.at("encrypted_content") == "opaque-reasoning";
            if (output && !preserved_reasoning)
                invalid_body = true;
            Json items =
                output ? Json::array({{{"type", "message"},
                                       {"role", "assistant"},
                                       {"content", Json::array({{{"type", "output_text"},
                                                                 {"text", "Answer 42"}}})}}})
                       : Json::array(
                             {{{"type", "reasoning"}, {"encrypted_content", "opaque-reasoning"}},
                              {{"type", "function_call"},
                               {"name", wire_name},
                               {"call_id", "call-1"},
                               {"arguments", "{\"a\":41}"}}});
            std::string data = sse({{"type", "response.output_text.delta"}, {"delta", "Partial "}});
            if (mode == 1) {
                res.set_content(data, "text/event-stream");
                return;
            }
            if (mode == 2)
                data += sse({{"type", "response.failed"}, {"error", {{"message", "failure"}}}});
            else
                data += sse({{"type", "response.completed"},
                             {"response",
                              {{"status", "completed"},
                               {"output", items},
                               {"usage", {{"output_tokens", 2}}}}}});
            res.set_content(data, "text/event-stream");
        });
        server.http.Post("/messages", [&](const auto& req, auto& res) {
            ++requests;
            if (reject(res))
                return;
            auto body = parse_json(req.body);
            if (body.at("system") != "App instructions\n" ||
                body.at("tools").at(0).at("name") != wire_name || !body.contains("max_tokens"))
                invalid_body = true;
            bool output = body.at("messages").back().at("content").is_array();
            if (output) {
                auto block = body.at("messages").back().at("content").at(0);
                if (block.at("tool_use_id") != "call-1")
                    invalid_body = true;
            }
            std::string data =
                sse({{"type", "message_start"}, {"message", {{"usage", {{"input_tokens", 3}}}}}});
            data += sse({{"type", "content_block_start"},
                         {"index", 0},
                         {"content_block", output ? Json{{"type", "text"}, {"text", ""}}
                                                  : Json{{"type", "tool_use"},
                                                         {"id", "call-1"},
                                                         {"name", wire_name},
                                                         {"input", Json::object()}}}});
            if (output)
                data += sse({{"type", "content_block_delta"},
                             {"index", 0},
                             {"delta", {{"type", "text_delta"}, {"text", "Answer 42"}}}});
            else {
                data +=
                    sse({{"type", "content_block_delta"},
                         {"index", 0},
                         {"delta", {{"type", "input_json_delta"}, {"partial_json", "{\"a\":"}}}});
                data += sse({{"type", "content_block_delta"},
                             {"index", 0},
                             {"delta", {{"type", "input_json_delta"}, {"partial_json", "41}"}}}});
            }
            data += sse({{"type", "content_block_stop"}, {"index", 0}});
            if (mode != 1) {
                if (mode == 2)
                    data += sse({{"type", "error"}, {"error", {{"type", "overloaded_error"}}}});
                else {
                    data += sse({{"type", "message_delta"},
                                 {"delta", {{"stop_reason", output ? "end_turn" : "tool_use"}}},
                                 {"usage", {{"output_tokens", 2}}}});
                    data += sse({{"type", "message_stop"}});
                }
            }
            res.set_content(data, "text/event-stream");
        });
        server.http.Post("/api/chat", [&](const auto& req, auto& res) {
            ++requests;
            if (reject(res))
                return;
            auto body = parse_json(req.body);
            bool output = body.at("messages").back().at("role") == "tool";
            if (body.at("tools").at(0).at("function").at("name") != wire_name)
                invalid_body = true;
            if (output && body.at("messages").back().at("tool_name") != wire_name)
                invalid_body = true;
            Json message = {{"role", "assistant"}, {"content", output ? "Answer 42" : ""}};
            if (!output)
                message["tool_calls"] = Json::array(
                    {{{"function", {{"name", wire_name}, {"arguments", {{"a", 41}}}}}}});
            std::string data = Json({{"message", message}, {"done", false}}).dump() + "\n";
            if (mode != 1)
                data += mode == 2 ? Json({{"error", "provider failure"}}).dump() + "\n"
                                  : Json({{"message", {{"content", ""}}},
                                          {"done", true},
                                          {"done_reason", "stop"},
                                          {"eval_count", 2}})
                                            .dump() +
                                        "\n";
            res.set_content(data, "application/x-ndjson");
        });
        server.http.Post("/chat/completions", [&](const auto& req, auto& res) {
            if (reject(res))
                return;
            auto body = parse_json(req.body);
            bool has_tools = body.contains("tools");
            bool output = body.at("messages").back().at("role") == "tool";
            std::string data;
            if (has_tools && !output) {
                if (body.at("tools").at(0).at("function").at("name") != wire_name)
                    invalid_body = true;
                Json delta = {{"tool_calls",
                               Json::array({{{"index", 0},
                                             {"id", "call-1"},
                                             {"function",
                                              {{"name", wire_name}, {"arguments", "{\"a\":"}}}}})}};
                data +=
                    sse({{"choices",
                          Json::array(
                              {{{"index", 0}, {"delta", delta}, {"finish_reason", nullptr}}})}});
                data +=
                    sse({{"choices",
                          Json::array({{{"index", 0},
                                        {"delta",
                                         {{"tool_calls",
                                           Json::array({{{"index", 0},
                                                         {"function", {{"arguments", "41}"}}}}})}}},
                                        {"finish_reason", "tool_calls"}}})}});
            } else {
                if (output &&
                    parse_json(body.at("messages").back().at("content").template get<std::string>())
                            .at("sum") != 42)
                    invalid_body = true;
                data +=
                    sse({{"choices",
                          Json::array(
                              {{{"index", 0},
                                {"delta", {{"content", has_tools ? "Answer 42" : "Compatible"}}},
                                {"finish_reason", "stop"}}})},
                         {"usage", {{"completion_tokens", 2}}}});
            }
            if (mode != 1)
                data += mode == 2 ? sse({{"error", {{"message", "provider failed"}}}})
                                  : "data: [DONE]\n\n";
            res.set_content(data, "text/event-stream");
        });
        server.start();
        for (const std::string id : {"openai", "anthropic", "ollama", "openrouter"}) {
            ProviderConfig config;
            config.api_key = "test-key";
            config.tools = tools;
            config.settings = {{"base_url", server.url}};
            auto provider = registry.create(id, config);
            Conversation chat(provider, "fixture-model", "App instructions");
            mode = 0;
            chat.connect();
            check(provider->list_models().size() == (id == "anthropic" ? 2 : 1),
                  "API native model catalogue/pagination");
            int before = invoked;
            auto answer = chat.send("use app.sum");
            check(answer.text == "Answer 42" && invoked == before + 1,
                  "API streams and completes a host tool round trip");
            check(!answer.usage.empty(), "API reports native usage");
            check(!invalid_body.load(),
                  "API native request, auth and tool result shapes validated");
            expect_error("Unsupported",
                         [&] { provider->start_session("fixture-model", "native-cli-id"); });
            for (const auto& [scenario, code] :
                 std::vector<std::pair<int, std::string>>{{1, "UnexpectedEOF"},
                                                          {2, "ProviderError"},
                                                          {4, "AuthenticationRequired"},
                                                          {5, "RateLimited"}}) {
                mode = scenario;
                Conversation broken(registry.create(id, config), "fixture-model",
                                    "App instructions");
                broken.connect();
                expect_error(code, [&] { broken.send("test failure"); });
            }
            mode = 0;
            auto denied = std::make_shared<Registry>();
            denied->add({"app.sum",
                         "Denied",
                         {{"type", "object"}},
                         {{"type", "object"}},
                         true,
                         [](const Json&, const CallContext&) {
                             throw std::runtime_error("Must never run");
                             return Json::object();
                         }});
            // A single round executes the denied tool into a structured result, then reaches
            // the budget.
            config.tools = denied;
            config.settings["max_tool_rounds"] = 1;
            Conversation limited(registry.create(id, config), "fixture-model", "App instructions");
            limited.connect();
            expect_error("ToolBudgetExceeded", [&] { limited.send("denied"); });
            check(limited.history().back().at("content").get<std::string>().find("AccessDenied") !=
                      std::string::npos,
                  "API tools honor host policy and retain the denied result");
        }
        mode = 6;
        ProviderConfig anthropic;
        anthropic.api_key = "test-key";
        anthropic.settings["base_url"] = server.url;
        expect_error("ProtocolError",
                     [&] { registry.create("anthropic", anthropic)->list_models(); });
        mode = 0;
        {
            ProviderConfig timed;
            timed.api_key = "test-key";
            timed.settings = {{"base_url", server.url}, {"timeout_ms", 100}};
            Conversation stalled(registry.create("openai", timed), "fixture-model");
            stalled.connect();
            mode = 7;
            expect_error("Timeout", [&] { stalled.send("wait"); });
            mode = 0;
        }
        ProviderConfig config;
        config.settings["base_url"] = server.url;
        expect_error("AuthenticationRequired",
                     [&] { registry.create("openrouter", config)->connect(); });
        config.api_key = "test-key";
        int before_probes = key_probes;
        auto router = registry.create("openrouter", config);
        router->connect();
        check(key_probes == before_probes + 1,
              "OpenRouter connection authenticates via key endpoint");
        Conversation routed(router, "fixture-model");
        routed.connect();
        check(routed.send("hello").text == "Compatible", "OpenRouter uses Chat Completions SSE");
        config.api_key = "wrong-key";
        expect_error("AuthenticationRequired",
                     [&] { registry.create("openrouter", config)->connect(); });
        config.api_key = "test-key";
        for (const std::string id : {"llama.cpp", "lmstudio", "openai-compatible"}) {
            Conversation compatible(registry.create(id, config), "fixture-model");
            compatible.connect();
            check(compatible.send("hello").text == "Compatible",
                  "Compatible server profile uses supplied endpoint");
        }
        registry.add({"my-service",
                      "Host extension",
                      "chat-completions-sse",
                      "host",
                      {true, false, false, true, true}},
                     [&](const ProviderConfig& c) {
                         check(!registry.describe().empty(),
                               "Custom factory runs outside registry lock");
                         HttpChatOptions options;
                         options.base_url = c.settings.at("base_url").get<std::string>();
                         return std::make_shared<HttpChatProvider>(options);
                     });
        Conversation custom(registry.create("my-service", config), "fixture-model");
        custom.connect();
        check(custom.send("hello").text == "Compatible",
              "Host adds a working connector without changing the SDK");
        check(registry.describe().dump().find("test-key") == std::string::npos,
              "Provider catalogue contains no credentials");
        check(!invalid_body.load(), "All native API protocol checks passed");
        std::cout << checks << " provider checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
