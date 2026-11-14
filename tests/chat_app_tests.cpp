#include "../examples/chat_app/help.hpp"
#include "test.hpp"
#include <cymcp/mcp.hpp>
#include <fstream>
#include <httplib.h>

using namespace cymcp;
namespace {
const std::string fixture_key = "CYMCP_FIXTURE_KEY_NOT_FOR_EXPORT";
std::string sse(const Json& data) {
    return "data: " + data.dump() + "\n\n";
}
void streamed(httplib::Response& response, std::vector<std::string> frames, bool ndjson = false) {
    response.set_chunked_content_provider(
        ndjson ? "application/x-ndjson" : "text/event-stream",
        [frames = std::move(frames), index = std::size_t(0)](std::size_t,
                                                             httplib::DataSink& sink) mutable {
            if (index == frames.size()) {
                sink.done();
                return true;
            }
            if (!sink.write(frames[index].data(), frames[index].size()))
                return false;
            ++index;
            std::this_thread::sleep_for(std::chrono::milliseconds(45));
            return true;
        });
}
std::string prompt(const Json& history) {
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->value("role", std::string()) != "user")
            continue;
        const auto& content = it->at("content");
        if (content.is_string())
            return content.get<std::string>();
        if (content.is_array())
            for (const auto& block : content)
                if (block.value("type", std::string()) == "text" ||
                    block.value("type", std::string()) == "input_text")
                    return block.at("text").get<std::string>();
    }
    return {};
}
struct ModelServer {
    httplib::Server server;
    std::thread worker;
    std::string base;
    std::atomic_int requests{0}, outputs{0}, keys{0};
    ModelServer() {
        auto models = [&](const httplib::Request& req, httplib::Response& res) {
            const auto key = req.has_header("x-api-key") ? req.get_header_value("x-api-key")
                                                         : req.get_header_value("Authorization");
            if (key != fixture_key && key != "Bearer " + fixture_key) {
                res.status = 401;
                return;
            }
            ++keys;
            res.set_content(
                Json({{"data", Json::array({{{"id", "fixture-model"}}})}, {"has_more", false}})
                    .dump(),
                "application/json");
        };
        server.Get("/v1/models", models);
        server.Get("/v1/key", models);
        server.Get("/api/tags", [&](const auto&, auto& res) {
            res.set_content("{\"models\":[{\"name\":\"fixture-model\"}]}", "application/json");
        });
        auto reply = [&](const httplib::Request& req, httplib::Response& res) {
            ++requests;
            auto body = parse_json(req.body);
            const bool responses = req.path == "/v1/responses";
            const bool anthropic = req.path == "/v1/messages";
            const bool ollama = req.path == "/api/chat";
            auto history = body.at(responses ? "input" : "messages");
            const auto text = prompt(history);
            bool output = false;
            if (responses)
                output = history.back().value("type", std::string()) == "function_call_output";
            else if (anthropic) {
                auto last = history.back().at("content");
                output = last.is_array() && !last.empty() &&
                         last.front().value("type", std::string()) == "tool_result";
            } else
                output = history.back().value("role", std::string()) == "tool";
            if (output)
                ++outputs;
            if (text == "REJECT") {
                res.status = 401;
                return;
            }
            if (text == "SILENT") {
                res.set_chunked_content_provider(
                    "text/event-stream", [](std::size_t, httplib::DataSink& sink) {
                        while (sink.is_writable())
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        return false;
                    });
                return;
            }
            const bool tool = text == "APP_TOOL" && !output;
            const bool denied =
                output && history.back().dump().find("AccessDenied") != std::string::npos;
            const auto answer = output ? (denied ? "DENIED" : "COUNTER42") : "APP_OK";
            std::vector<std::string> frames;
            if (responses) {
                Json items = tool ? Json::array({{{"type", "function_call"},
                                                  {"name", "set_counter"},
                                                  {"call_id", "call-1"},
                                                  {"arguments", "{\"value\":42}"}}})
                                  : Json::array({{{"type", "message"},
                                                  {"role", "assistant"},
                                                  {"content", Json::array({{{"type", "output_text"},
                                                                            {"text", answer}}})}}});
                if (!tool)
                    frames.push_back(
                        sse({{"type", "response.output_text.delta"}, {"delta", answer}}));
                frames.push_back(sse({{"type", "response.completed"},
                                      {"response",
                                       {{"status", "completed"},
                                        {"output", items},
                                        {"usage", {{"output_tokens", 3}}}}}}));
            } else if (anthropic) {
                frames.push_back(sse(
                    {{"type", "message_start"}, {"message", {{"usage", {{"input_tokens", 2}}}}}}));
                frames.push_back(
                    sse({{"type", "content_block_start"},
                         {"index", 0},
                         {"content_block", tool ? Json{{"type", "tool_use"},
                                                       {"id", "call-1"},
                                                       {"name", "set_counter"},
                                                       {"input", {{"value", 42}}}}
                                                : Json{{"type", "text"}, {"text", ""}}}}));
                if (!tool)
                    frames.push_back(sse({{"type", "content_block_delta"},
                                          {"index", 0},
                                          {"delta", {{"type", "text_delta"}, {"text", answer}}}}));
                frames.push_back(sse({{"type", "content_block_stop"}, {"index", 0}}));
                frames.push_back(
                    sse({{"type", "message_delta"},
                         {"delta", {{"stop_reason", tool ? "tool_use" : "end_turn"}}}}));
                frames.push_back(sse({{"type", "message_stop"}}));
            } else if (ollama) {
                frames.push_back(
                    Json({{"message",
                           tool ? Json{{"content", ""},
                                       {"tool_calls",
                                        Json::array({{{"function",
                                                       {{"name", "set_counter"},
                                                        {"arguments", {{"value", 42}}}}}}})}}
                                : Json{{"content", answer}}},
                          {"done", false}})
                        .dump() +
                    "\n");
                frames.push_back(
                    "{\"message\":{\"content\":\"\"},\"done\":true,\"done_reason\":\"stop\"}\n");
            } else {
                Json delta =
                    tool
                        ? Json{{"tool_calls", Json::array({{{"index", 0},
                                                            {"id", "call-1"},
                                                            {"function",
                                                             {{"name", "set_counter"},
                                                              {"arguments", "{\"value\":42}"}}}}})}}
                        : Json{{"content", answer}};
                frames.push_back(sse({{"choices", Json::array({{{"index", 0},
                                                                {"delta", delta},
                                                                {"finish_reason", nullptr}}})}}));
                if (text != "TRUNCATE") {
                    frames.push_back(
                        sse({{"choices",
                              Json::array({{{"index", 0},
                                            {"delta", Json::object()},
                                            {"finish_reason", tool ? "tool_calls" : "stop"}}})}}));
                    frames.push_back("data: [DONE]\n\n");
                }
            }
            streamed(res, std::move(frames), ollama);
        };
        for (const char* path :
             {"/v1/chat/completions", "/v1/responses", "/v1/messages", "/api/chat"})
            server.Post(path, reply);
        auto port = server.bind_to_any_port("127.0.0.1");
        if (port < 1)
            throw std::runtime_error("Model fixture bind failed");
        base = "http://127.0.0.1:" + std::to_string(port);
        worker = std::thread([&] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~ModelServer() {
        server.stop();
        if (worker.joinable())
            worker.join();
    }
};
void write(const std::filesystem::path& path, const Json& value) {
    std::ofstream file(path, std::ios::binary);
    file << value.dump(2);
    if (!file)
        throw std::runtime_error("Cannot write test configuration");
}
Json read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return parse_json(std::string(std::istreambuf_iterator<char>(file), {}));
}
struct Run {
    std::unique_ptr<Process> process;
    std::filesystem::path report;
    Run(const std::string& app, const std::filesystem::path& directory, Json config, Json steps,
        bool ready = false) {
        private_directory(directory);
        auto input = directory / "config.json", plan = directory / "plan.json";
        report = directory / "report.json";
        write(input, config);
        write(plan, {{"steps", steps}});
        ProcessOptions options;
        options.executable = std::filesystem::u8path(app);
        options.arguments = {"--hidden",
                             "--config",
                             path_utf8(input),
                             "--exercise",
                             path_utf8(plan),
                             "--report",
                             path_utf8(report),
                             "--language",
                             "fr",
                             "--capture",
                             path_utf8(directory / "screen.png")};
        if (ready)
            options.arguments.push_back("--test-ready-stdout");
        options.environment["CYMCP_TEST_API_KEY"] = fixture_key;
        process = std::make_unique<Process>(options);
        process->close_input();
    }
    Json finish() {
        Cancellation timeout;
        timeout.deadline = Clock::now() + std::chrono::seconds(30);
        ProcessLine line;
        while (process->next(line, timeout)) {
        }
        auto exit = process->wait(timeout);
        auto report_value = read(report);
        check(exit == 0 && report_value.at("passed").get<bool>(),
              "Native app exercise reports successful terminal completion");
        check(report_value.dump().find(fixture_key) == std::string::npos,
              "API key excluded from app validation report");
        check(report_value.at("frames").get<int>() > 5,
              "Real window rendered throughout the exercise");
        return report_value;
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            return 2;
        const auto output = std::filesystem::current_path() / "chat-app-validation";
        private_directory(output);
        {
            const auto folder = output / std::filesystem::u8path("help space é#") / "Documentation";
            private_directory(folder);
            std::ofstream(folder / "Documentation.html") << "<!doctype html><title>Help</title>";
            const auto executable = folder.parent_path() / "chat.exe";
            const auto english = chat_example::help_url(executable, false);
            check(english.find(L"%20") != std::wstring::npos &&
                      english.find(L"%C3%A9") != std::wstring::npos &&
                      english.find(L"%23") != std::wstring::npos &&
                      english.ends_with(L"#/en/chat-lab"),
                  "Local help URL encodes spaces, UTF-8 and filename fragments");
            check(chat_example::help_url(executable, true).ends_with(L"#/fr/chat-lab"),
                  "Local help uses the explicitly selected French locale");
            expect_error("DocumentationMissing", [&] {
                chat_example::help_url(output / "missing-manual" / "chat.exe", false);
            });
        }
        ModelServer model;
        Json report = Json::array();
        {
            const auto before = model.requests.load();
            Run run(argv[1], output / "automatic-local",
                    {{"provider", "openai-compatible"},
                     {"automatic", true},
                     {"discovery_environment", false},
                     {"api_key_env", "CYMCP_TEST_API_KEY"},
                     {"settings", {{"base_url", model.base + "/v1"}, {"timeout_ms", 10000}}}},
                    Json::array({{{"action", "automatic-connect"},
                                  {"provider", "openai-compatible"},
                                  {"model", "fixture-model"}},
                                 {{"action", "wait"}, {"messages", 0}}}));
            report.push_back(run.finish());
            check(model.requests == before,
                  "Automatic discovery and connection submit zero generations");
        }
        {
            Run run(argv[1], output / "automatic-native",
                    {{"provider", "codex"},
                     {"automatic", true},
                     {"discovery_environment", false},
                     {"process",
                      {{"executable", argv[2]},
                       {"arguments", Json::array({"--codex-fixture", "--fixture-app-delay"})}}}},
                    Json::array({{{"action", "automatic-connect"}, {"provider", "codex"}},
                                 {{"action", "send"},
                                  {"text", "automatic"},
                                  {"reply", "Hello fixture"},
                                  {"streaming", true}}}));
            report.push_back(run.finish());
        }
        for (const std::string id :
             {"openai-compatible", "openai", "anthropic", "openrouter", "ollama"}) {
            int before = model.requests;
            Json config = {
                {"provider", id},
                {"model", "fixture-model"},
                {"api_key_env", "CYMCP_TEST_API_KEY"},
                {"settings",
                 {{"base_url", model.base + (id == "ollama" ? "" : "/v1")}, {"timeout_ms", 10000}}},
                {"app_commands", {{"attach_to_ai", true}, {"allow_changes", true}}}};
            Json steps = Json::array({{{"action", "connect"}},
                                      {{"action", "models"}},
                                      {{"action", "send"},
                                       {"text", "APP_TOOL"},
                                       {"reply", "COUNTER42"},
                                       {"counter", 42},
                                       {"streaming", true}},
                                      {{"action", "disconnect"}},
                                      {{"action", "reconnect"}, {"same_session", true}},
                                      {{"action", "send"},
                                       {"text", "TEXT_ONLY"},
                                       {"reply", "APP_OK"},
                                       {"streaming", true}}});
            Run run(argv[1], output / id, config, steps);
            report.push_back(run.finish());
            check(model.requests == before + 3,
                  "Exactly two tool-round requests and one text request; no generation replay");
        }
        auto config = Json{{"provider", "openai-compatible"},
                           {"model", "fixture-model"},
                           {"api_key_env", "CYMCP_TEST_API_KEY"},
                           {"settings", {{"base_url", model.base + "/v1"}, {"timeout_ms", 5000}}},
                           {"app_commands", {{"attach_to_ai", true}, {"allow_changes", false}}}};
        {
            Run run(argv[1], output / "rights-denied", config,
                    Json::array({{{"action", "connect"}},
                                 {{"action", "send"},
                                  {"text", "APP_TOOL"},
                                  {"reply", "DENIED"},
                                  {"counter", 0}},
                                 {{"action", "rights"}, {"allow", true}},
                                 {{"action", "send"},
                                  {"text", "APP_TOOL"},
                                  {"reply", "COUNTER42"},
                                  {"counter", 42}}}));
            report.push_back(run.finish());
        }
        {
            auto directory = output / "failures";
            config["history"] = {{"directory", path_utf8(directory / "history")}, {"id", "saved"}};
            Run run(
                argv[1], directory, config,
                Json::array(
                    {{{"action", "connect"}},
                     {{"action", "send"}, {"text", "TRUNCATE"}, {"error", "UnexpectedEOF"}},
                     {{"action", "reconnect"}, {"same_session", true}},
                     {{"action", "send"}, {"text", "REJECT"}, {"error", "AuthenticationRequired"}},
                     {{"action", "reconnect"}, {"same_session", true}},
                     {{"action", "send"},
                      {"text", "SILENT"},
                      {"cancel_after_ms", 200},
                      {"error", "Cancelled"}},
                     {{"action", "reconnect"}, {"same_session", true}},
                     {{"action", "send"}, {"text", "TEXT_ONLY"}, {"reply", "APP_OK"}},
                     {{"action", "save"}},
                     {{"action", "disconnect"}},
                     {{"action", "connect"}, {"restore", true}, {"same_session", true}}}));
            report.push_back(run.finish());
        }
        {
            Json native = {
                {"provider", "codex"},
                {"model", "fixture-model"},
                {"process",
                 {{"executable", path_utf8(std::filesystem::absolute(argv[2]))},
                  {"arguments",
                   {"--codex-fixture", "--fixture-app-counter", "--fixture-app-delay"}}}},
                {"app_commands", {{"attach_to_ai", true}, {"allow_changes", true}}}};
            Run run(argv[1], output / "codex-process", native,
                    Json::array({{{"action", "connect"}},
                                 {{"action", "send"},
                                  {"text", "tool"},
                                  {"reply", "Hello fixture"},
                                  {"counter", 42},
                                  {"streaming", true}},
                                 {{"action", "send"},
                                  {"text", "silent"},
                                  {"cancel_after_ms", 150},
                                  {"error", "Cancelled"}},
                                 {{"action", "reconnect"}, {"same_session", true}},
                                 {{"action", "send"},
                                  {"text", "after cancel"},
                                  {"reply", "Hello fixture"},
                                  {"streaming", true}}}));
            report.push_back(run.finish());
        }
        for (bool writable : {false, true}) {
            Json mcp_config = {{"provider", "demo"},
                               {"app_commands", {{"allow_changes", writable}}}};
            Run run(argv[1], output / (writable ? "mcp-allow" : "mcp-deny"), mcp_config,
                    Json::array(
                        {{{"action", "wait"}, {"wait_ms", 1800}, {"counter", writable ? 42 : 0}}}),
                    true);
            Cancellation timeout;
            timeout.deadline = Clock::now() + std::chrono::seconds(5);
            ProcessLine line;
            check(run.process->next(line, timeout) && !line.stderr_stream,
                  "App exposes MCP handshake over private parent pipe");
            auto ready = parse_json(line.text);
            HttpRequest request;
            request.method = "POST";
            request.url = ready.at("endpoint");
            request.headers["Content-Type"] = "application/json";
            request.body = "{}";
            check(HttpClient().perform(request).status == 401,
                  "Real app MCP rejects missing token");
            request.headers["Authorization"] = "Bearer " + ready.at("token").get<std::string>();
            request.headers["Origin"] = "http://untrusted.invalid";
            check(HttpClient().perform(request).status == 403,
                  "Real app MCP rejects browser origins");
            McpClient client(ready.at("endpoint"), ready.at("token"));
            client.connect(timeout);
            check(client.list_tools(timeout).size() == 2,
                  "External client discovers the app's actual command registry");
            auto read_value = client.call_tool("get_counter", Json::object(), timeout);
            check(read_value.at("structuredContent").at("value") == 0,
                  "External MCP reads visible app state");
            request.headers.erase("Origin");
            request.url = ready.at("endpoint").get<std::string>();
            request.url.replace(request.url.size() - 4, 4, "/v1");
            request.body = Json({{"method", "describe"}}).dump();
            auto description = HttpClient().perform(request, timeout);
            check(description.status == 200 &&
                      parse_json(description.body).at("operations").size() == 2,
                  "External SDK discovers the same app operations over JSON HTTP");
            request.body =
                Json({{"method", "invoke"}, {"name", "get_counter"}, {"arguments", Json::object()}})
                    .dump();
            auto sdk_read = HttpClient().perform(request, timeout);
            check(sdk_read.status == 200 && parse_json(sdk_read.body).at("value") == 0,
                  "External SDK reads the real app counter directly");
            request.body =
                Json(
                    {{"method", "invoke"}, {"name", "set_counter"}, {"arguments", {{"value", 41}}}})
                    .dump();
            auto sdk_write = HttpClient().perform(request, timeout);
            auto sdk_result = parse_json(sdk_write.body);
            check(writable ? sdk_write.status == 200 && sdk_result.at("value") == 41
                           : sdk_write.status == 403 &&
                                 sdk_result.at("error").at("code") == "AccessDenied",
                  "External SDK mutations obey the same live permission policy");
            check(client.call_tool("get_counter", Json::object(), timeout)
                          .at("structuredContent")
                          .at("value") == (writable ? 41 : 0),
                  "Direct SDK changes are visible immediately through MCP");
            auto changed = client.call_tool("set_counter", {{"value", 42}}, timeout);
            check(changed.value("isError", false) == !writable,
                  "MCP mutations obey the app's live policy");
            check(client.call_tool("get_counter", Json::object(), timeout)
                          .at("structuredContent")
                          .at("value") == (writable ? 42 : 0),
                  "MCP changes reach the real app state");
            check(client.call_tool("set_counter", {{"value", 1000001}}, timeout)
                      .value("isError", false),
                  "MCP rejects values outside the schema bounds");
            report.push_back(run.finish());
        }
        write(output / "summary.json", {{"kind", "deterministic-transport-fixtures"},
                                        {"real_inference", false},
                                        {"checks", checks},
                                        {"runs", report}});
        std::cout << checks << " bidirectional native app checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
