#include "../examples/chat_app/help.hpp"
#include "test.hpp"
#include <cymcp/mcp.hpp>
#include <fstream>
#include <httplib.h>
#ifdef _WIN32
#include <sddl.h>
#else
#include <sys/stat.h>
#endif

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
#ifdef _WIN32
std::wstring directory_or_file_acl(const std::filesystem::path& path) {
    DWORD size = 0;
    GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
    std::vector<unsigned char> descriptor(size);
    if (!size ||
        !GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, descriptor.data(), size, &size))
        throw std::runtime_error("Cannot read fixture ACL");
    LPWSTR text = nullptr;
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
            descriptor.data(), SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &text, nullptr))
        throw std::runtime_error("Cannot format fixture ACL");
    std::wstring result(text);
    LocalFree(text);
    return result;
}
#else
mode_t directory_or_file_acl(const std::filesystem::path& path) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0)
        throw std::runtime_error("Cannot read fixture permissions");
    return info.st_mode & 0777;
}
#endif
struct Run {
    std::unique_ptr<Process> process;
    std::filesystem::path report, journal;
    Run(const std::string& app, const std::filesystem::path& directory, Json config, Json steps,
        bool ready = false, std::string theme = "mid-grey", std::string language = "fr",
        std::filesystem::path custom_log = {}) {
        private_directory(directory);
        auto input = directory / "config.json", plan = directory / "plan.json";
        report = directory / "report.json";
        write(plan, {{"steps", steps}});
        ProcessOptions options;
        options.executable = std::filesystem::u8path(app);
        options.arguments = {"--hidden",
                             "--exercise",
                             path_utf8(plan),
                             "--report",
                             path_utf8(report),
                             "--language",
                             language,
                             "--theme",
                             theme,
                             "--capture",
                             path_utf8(directory / "screen.png")};
        if (!config.is_null()) {
            write(input, config);
            options.arguments.insert(options.arguments.end(), {"--config", path_utf8(input)});
        }
        if (ready)
            options.arguments.push_back("--test-ready-stdout");
        journal = directory / "application.log";
        if (!custom_log.empty()) {
            journal = std::move(custom_log);
            options.arguments.insert(options.arguments.end(), {"--log-file", path_utf8(journal)});
        }
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
        {
            std::ifstream log(journal, std::ios::binary);
            const std::string contents(std::istreambuf_iterator<char>(log), {});
            check(!contents.empty(), "Application writes a persistent diagnostic journal");
            check(contents.find(fixture_key) == std::string::npos &&
                      contents.find("APP_TOOL") == std::string::npos &&
                      contents.find("APP_OK") == std::string::npos,
                  "Journal excludes API credentials, prompts and generated response text");
        }
        check(report_value.at("frames").get<int>() > 5,
              "Real window rendered throughout the exercise");
        return report_value;
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            return 2;
        const auto output = std::filesystem::current_path() / "chat-app-validation";
        private_directory(output);
        {
            const auto folder = output / std::filesystem::u8path("help space é#") / "Documentation";
            private_directory(folder);
            std::ofstream(folder / "Documentation.html") << "<!doctype html><title>Help</title>";
            const auto executable = folder.parent_path() / "chat.exe";
            const auto english = path_utf8(
                std::filesystem::path(chat_example::help_url(executable, false)));
            check(english.find("%20") != std::string::npos &&
                      english.find("%C3%A9") != std::string::npos &&
                      english.find("%23") != std::string::npos &&
                      english.ends_with("#/en/chat-lab"),
                  "Local help URL encodes spaces, UTF-8 and filename fragments");
            check(path_utf8(std::filesystem::path(chat_example::help_url(executable, true)))
                      .ends_with("#/fr/chat-lab"),
                  "Local help uses the explicitly selected French locale");
            expect_error("DocumentationMissing", [&] {
                chat_example::help_url(output / "missing-manual" / "chat.exe", false);
            });
        }
        ModelServer model;
        Json report = Json::array();
        {
            const auto folder = output / "native-attachments";
            private_directory(folder);
            const auto image = folder / "preview.png";
            const auto note = folder / "notes.md";
            const auto outside = folder / "outside.md";
            const auto custom = folder / ("custom-logs-" + random_token(16));
            std::filesystem::create_directory(custom);
            const auto before = directory_or_file_acl(custom);
            const auto log = custom / "selected.log";
            std::ofstream(log) << "previous contents";
            std::filesystem::copy_file(argv[3], image,
                                       std::filesystem::copy_options::overwrite_existing);
            std::ofstream(note) << "## Attachment fixture\n\n![Native preview][photo]\n\n"
                                   "[photo]: ./preview.png\n";
            std::ofstream(outside) << "![Unattached](../preview.png)\n";
            Run run(
                argv[1], folder, {{"provider", "demo"}},
                Json::array(
                    {{{"action", "connect"}},
                     {{"action", "draft-attachment"}, {"file", path_utf8(image)}, {"messages", 0}},
                     {{"action", "page"}, {"index", 1}},
                     // Keep chat thumbnails offscreen. Only the Markdown popup can load an image.
                     {{"action", "send"},
                      {"text", "Ambiguous names"},
                      {"attachments",
                       Json::array({path_utf8(image), path_utf8(image), path_utf8(note)})}},
                     {{"action", "preview-attachment"}, {"index", 2}, {"image_ready", false}},
                     {{"action", "send"},
                      {"text", "Document only"},
                      {"attachments", Json::array({path_utf8(note)})}},
                     {{"action", "preview-attachment"}, {"index", 0}, {"image_ready", false}},
                     {{"action", "send"},
                      {"text", "Native attachments"},
                      {"attachments",
                       Json::array({path_utf8(image), path_utf8(note), path_utf8(outside)})}},
                     {{"action", "preview-attachment"}, {"index", 2}, {"image_ready", false}},
                     {{"action", "preview-attachment"}, {"index", 1}, {"image_ready", true}},
                     {{"action", "preview-attachment"}, {"index", 0}, {"image_ready", true}},
                     {{"action", "save-attachment-twice"}, {"index", 1}}}),
                false, "mid-grey", "fr", log);
            report.push_back(run.finish());
            check(sha256_file(folder / "saved-attachment.bin") == sha256_file(note),
                  "Repeated save keeps the application running and writes the selected content");
            check(directory_or_file_acl(custom) == before,
                  "Choosing an existing log folder leaves its access rules and inheritance intact");
#ifdef _WIN32
            auto owner_only = directory_or_file_acl(folder);
            owner_only.erase(owner_only.find(L"OICI"), 4);
            auto file_acl = directory_or_file_acl(log);
            check(file_acl.starts_with(L"D:P") && file_acl.substr(file_acl.find(L'(')) ==
                                                      owner_only.substr(owner_only.find(L'(')),
                  "Custom log replaces the previous file with owner-only access");
#else
            check(directory_or_file_acl(log) == 0600,
                  "Custom log replaces the previous file with owner-only access");
#endif
        }
        for (const std::string theme :
             {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            for (int page : {1, 2}) {
                Run run(argv[1], output / ("workspace-" + theme + "-" + std::to_string(page)),
                        {{"provider", "demo"}},
                        Json::array({{{"action", "select-provider"}, {"id", "openai"}},
                                     {{"action", "page"},
                                      {"index", page},
                                      {"page", page},
                                      {"log_code", "ProviderSelected"}}}),
                        false, theme, page == 1 ? "en" : "fr");
                report.push_back(run.finish());
            }
        }
        {
            // The real no-config startup initially selects the offline demo. This must not
            // prevent discovery from selecting an installed AI. Earlier tests pinned Codex.
            Run run(argv[1], output / "startup-no-preference",
                    {{"provider", "demo"},
                     {"system", "Only for the offline demo"},
                     {"automatic", true},
                     {"discovery_environment", false},
                     {"discovery",
                      {{"codex",
                        {{"process",
                          {{"executable", argv[2]},
                           {"arguments",
                            Json::array({"--codex-fixture", "--fixture-app-delay"})}}}}}}}},
                    Json::array({{{"action", "automatic-connect"},
                                  {"provider", "codex"},
                                  {"configured_system", ""},
                                  {"log_code", "Completed"}},
                                 {{"action", "send"},
                                  {"text", "startup"},
                                  {"reply", "Hello fixture"},
                                  {"streaming", true}},
                                 {{"action", "page"}, {"index", 1}, {"page", 1}},
                                 {{"action", "page"},
                                  {"index", 2},
                                  {"page", 2},
                                  {"log_code", "FirstResponse"}}}));
            report.push_back(run.finish());
        }
        {
            Run run(
                argv[1], output / "provider-settings-tabs",
                {{"provider", "openai-compatible"},
                 {"model", "fixture-model"},
                 {"system", "Only for this provider"},
                 {"api_key_env", "CYMCP_TEST_API_KEY"},
                 {"settings", {{"base_url", model.base + "/v1"}}}},
                Json::array(
                    {{{"action", "page"}, {"index", 1}, {"page", 1}},
                     {{"action", "select-provider"},
                      {"id", "claude-code"},
                      {"selected_provider", "claude-code"},
                      {"configured_system", ""}},
                     {{"action", "select-provider"},
                      {"id", "openai-compatible"},
                      {"configured_model", "fixture-model"},
                      {"configured_system", "Only for this provider"}},
                     {{"action", "connect"}},
                     {{"action", "send"}, {"text", "REJECT"}, {"error", "AuthenticationRequired"}},
                     {{"action", "page"},
                      {"index", 2},
                      {"page", 2},
                      {"error", "AuthenticationRequired"},
                      {"log_code", "AuthenticationRequired"}}}));
            report.push_back(run.finish());
        }
        {
            const auto before = model.requests.load();
            Run run(argv[1], output / "automatic-local",
                    {{"provider", "openai-compatible"},
                     {"system", "Use the configured provider instructions"},
                     {"automatic", true},
                     {"discovery_environment", false},
                     {"api_key_env", "CYMCP_TEST_API_KEY"},
                     {"settings", {{"base_url", model.base + "/v1"}, {"timeout_ms", 10000}}}},
                    Json::array({{{"action", "automatic-connect"},
                                  {"provider", "openai-compatible"},
                                  {"configured_system", "Use the configured provider instructions"},
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
            if (id == "ollama")
                steps.back()["measure_frames"] = true;
            Run run(argv[1], output / id, config, steps);
            report.push_back(run.finish());
            if (id == "ollama") {
                const auto& step = report.back().at("results").back();
                check(step.at("first_rendered_ms").get<double>() > 0 &&
                          step.at("first_rendered_ms") < step.at("completed_frame_ms"),
                      "Streaming reply reaches a DX11 frame before the completed reply");
            }
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
                                 {{"action", "draft"}, {"text", "silent"}, {"draft", "silent"}},
                                 {{"action", "click-send"},
                                  {"cancel_after_ms", 350},
                                  {"timeout_ms", 4000},
                                  {"messages", 2},
                                  {"error", "Cancelled"}},
                                 {{"action", "reconnect"}, {"same_session", true}},
                                 {{"action", "draft"}, {"text", "tool"}, {"draft", "tool"}},
                                 {{"action", "click-send"},
                                  {"reply", "Hello fixture"},
                                  {"counter", 42},
                                  {"draft", ""},
                                  {"streaming", true}}}));
            report.push_back(run.finish());
        }
        const std::string refusal =
            "The 'gpt-6.2-sol' model is not supported when using Codex with a ChatGPT account.";
        {
            Run run(argv[1], output / "settings-navigation", {{"provider", "demo"}},
                    Json::array({{{"action", "draft"}, {"text", "**Unsent**"}, {"page", 0}},
                                 {{"action", "click-settings"}, {"page", 1}},
                                 {{"action", "page"}, {"index", 2}, {"page", 2}},
                                 {{"action", "raw-log"}, {"enabled", true}, {"page", 2}},
                                 {{"action", "click-close"}, {"page", 0}, {"draft", "**Unsent**"}},
                                 {{"action", "help-key"}, {"page", 3}},
                                 {{"action", "close-key"},
                                  {"page", 0},
                                  {"draft", "**Unsent**"},
                                  {"messages", 0}}}));
            report.push_back(run.finish());
        }
        for (const std::string prompt : {"rpc-model-refusal", "partial-model-refusal"}) {
            const auto folder = output / prompt;
            private_directory(folder);
            const auto turns = folder / "turn-starts.txt";
            std::ofstream(turns, std::ios::trunc).close();
            const bool rpc = prompt == "rpc-model-refusal";
            Json native = {{"provider", "codex"},
                           {"model", "gpt-6.2-sol"},
                           {"process",
                            {{"executable", argv[2]},
                             {"arguments", {"--codex-fixture"}},
                             {"environment", {{"CYMCP_FIXTURE_TURNS_FILE", path_utf8(turns)}}}}}};
            Run run(argv[1], folder, native,
                    Json::array({{{"action", "connect"}},
                                 {{"action", "draft"}, {"text", prompt}, {"draft", prompt}},
                                 {{"action", "click-send"},
                                  {"error", "ProviderError"},
                                  {"turn_error_contains", refusal},
                                  {"provider_code", rpc ? "-32602" : "badRequest"},
                                  {"reply", rpc ? "" : "Partial answer."},
                                  {"messages", 2}},
                                 {{"action", "wait"},
                                  {"wait_ms", 250},
                                  {"error", "ProviderError"},
                                  {"messages", 2}}}));
            report.push_back(run.finish());
            check(report.back().at("model") == "gpt-6.2-sol",
                  "Chat Lab displays the same model whose turn was rejected");
            std::ifstream journal(run.journal);
            unsigned failures = 0;
            bool log_is_private = true;
            std::string line;
            while (std::getline(journal, line)) {
                log_is_private &= line.find("fixture-sensitive-token") == std::string::npos &&
                                  line.find("rpc-private-payload") == std::string::npos &&
                                  line.find(prompt) == std::string::npos &&
                                  line.find("Partial answer.") == std::string::npos;
                const auto entry = parse_json(line);
                if (entry.at("operation") == "generating" && entry.at("level") == "error") {
                    ++failures;
                    const auto& detail = entry.at("message").get_ref<const std::string&>();
                    check(
                        entry.at("code") == "ProviderError" && entry.at("provider") == "codex" &&
                            entry.at("provider_code") == (rpc ? "-32602" : "badRequest") &&
                            (rpc ? detail == refusal : detail.find(refusal) != std::string::npos),
                        "Journal file retains the refusal, SDK category and native provider code");
                }
            }
            check(log_is_private,
                  "Provider error logs exclude secrets, prompt and partial assistant text");
            check(failures == 1, "The failed turn appears once in the journal file");
            std::ifstream attempts(turns);
            check(std::getline(attempts, line) && !std::getline(attempts, line),
                  "A failed Codex turn makes exactly one generation request");
        }
        {
            const auto folder = output / "codex-account-model";
            private_directory(folder);
            const auto turns = folder / "turn-starts.txt";
            std::ofstream(turns, std::ios::trunc).close();
            Json native = {{"provider", "codex"},
                           {"model", "gpt-6.2-sol"},
                           {"process",
                            {{"executable", argv[2]},
                             {"arguments", {"--codex-fixture", "--fixture-account-models"}},
                             {"environment", {{"CYMCP_FIXTURE_TURNS_FILE", path_utf8(turns)}}}}}};
            Run run(argv[1], folder, native,
                    Json::array({{{"action", "connect"}, {"model", "gpt-6.2-sol"}},
                                 {{"action", "draft"}, {"text", "account-model-test"}},
                                 {{"action", "click-send"},
                                  {"error", "UnknownModel"},
                                  {"turn_error_contains", refusal},
                                  {"provider_code", "model_not_found"},
                                  {"reply", ""},
                                  {"messages", 2}},
                                 {{"action", "select-model"},
                                  {"id", "gpt-6-luna"},
                                  {"model", "gpt-6-luna"},
                                  {"same_session", true},
                                  {"messages", 2}},
                                 {{"action", "draft"}, {"text", "account-model-test"}},
                                 {{"action", "click-send"},
                                  {"reply", "Hello fixture"},
                                  {"model", "gpt-6-luna"},
                                  {"same_session", true},
                                  {"messages", 4}}}));
            report.push_back(run.finish());
            std::ifstream journal(run.journal);
            std::size_t failures = 0;
            for (std::string line; std::getline(journal, line);) {
                check(line.find("fixture-private-account") == std::string::npos,
                      "Adjacent provider metadata is excluded from the journal");
                const auto row = parse_json(line);
                if (row.at("code") == "UnknownModel" && row.at("operation") == "generating") {
                    ++failures;
                    check(row.at("message") == refusal &&
                              row.at("provider_code") == "model_not_found",
                          "A stringified Codex backend rejection retains only its explanation and "
                          "code");
                }
            }
            std::ifstream count(turns);
            check(failures == 1 && std::count(std::istreambuf_iterator<char>(count),
                                              std::istreambuf_iterator<char>(), '\n') == 2,
                  "Changing the model leaves both turns visible and sends only on explicit clicks");
        }
        struct McpScenario {
            const char* name;
            Json access;
            bool writable, deferred, tokenless;
        };
        const McpScenario mcp_scenarios[] = {
            {"mcp-default", nullptr, true, false, true},
            {"mcp-free", {{"access_profile", "free"}}, true, true, true},
            {"mcp-local-controlled", {{"access_profile", "local_controlled"}}, false, false, true},
            {"mcp-token-only", {{"access_profile", "token_only"}}, true, false, false},
            {"mcp-protected", {{"access_profile", "protected"}}, false, true, false},
            {"mcp-legacy-default", Json::object(), false, false, false},
            {"mcp-legacy-write", {{"allow_changes", true}}, true, true, false},
            {"mcp-legacy-tokenless", {{"mcp_without_token", true}}, false, true, true},
        };
        for (const auto& [name, access, writable, deferred, tokenless] : mcp_scenarios) {
            Json mcp_config;
            if (!access.is_null()) {
                auto commands = access;
                commands["mcp"] = true;
                if (deferred) {
                    commands["deferred_tools"] = true;
                    commands["notifications"] = true;
                    mcp_config["performance"] = {{"enabled", true}};
                }
                mcp_config["provider"] = "demo";
                mcp_config["app_commands"] = std::move(commands);
            }
            Run run(argv[1], output / name, mcp_config,
                    Json::array(
                        {{{"action", "wait"}, {"wait_ms", 1800}, {"counter", writable ? 42 : 0}}}),
                    true);
            Cancellation timeout;
            timeout.deadline = Clock::now() + std::chrono::seconds(5);
            ProcessLine line;
            check(run.process->next(line, timeout) && !line.stderr_stream,
                  "App exposes MCP handshake over private parent pipe");
            auto ready = parse_json(line.text);
            const auto endpoint = ready.at("endpoint").get<std::string>();
            HttpRequest advertisement;
            advertisement.url = endpoint.substr(0, endpoint.size() - 4) + "/.well-known/cymcp";
            const auto advertised = HttpClient().perform(advertisement, timeout);
            const auto metadata = parse_json(advertised.body);
            check(advertised.status == 200 && metadata.at("name") == "CyMCPSDK Chat Lab" &&
                      metadata.at("version") == version && metadata.at("requiresToken") == !tokenless,
                  "Real Chat Lab advertises its name, version and access profile without a token");
            const auto discovered = probe_local_mcp(endpoint, timeout, std::chrono::seconds(2));
            check(discovered.name == "CyMCPSDK Chat Lab" &&
                      (tokenless ? discovered.status == LocalMcpStatus::active
                                 : discovered.status == LocalMcpStatus::authentication_required && discovered.advertised),
                  "A hub can identify both free and protected real Chat Lab instances");
            HttpRequest request;
            request.method = "POST";
            request.url = ready.at("endpoint");
            request.headers["Content-Type"] = "application/json";
            request.body =
                R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"fixture","version":"1"}}})";
            const auto response = HttpClient().perform(request, timeout);
            const auto token = ready.at("token").get<std::string>();
            check(tokenless ? token.empty() && response.status == 200 &&
                                  parse_json(response.body).contains("result")
                            : !token.empty() && response.status == 401,
                  "The real app accepts missing credentials only when explicitly opened");
            if (!tokenless)
                request.headers["Authorization"] = "Bearer " + token;
            request.headers["Origin"] = "http://untrusted.invalid";
            check(HttpClient().perform(request).status == 403,
                  "Real app MCP rejects browser origins");
            request.headers.erase("Origin");
            request.headers["Host"] = "untrusted.invalid";
            check(HttpClient().perform(request, timeout).status == 403,
                  "Real app MCP rejects non-local Host headers in both access modes");
            request.headers.erase("Host");
            McpClient client(ready.at("endpoint"), token);
            client.connect(timeout);
            check(client.list_tools(timeout).size() == (deferred ? 5 : 2),
                  "External client discovers the app's actual command registry");
            check(client.list_resources(timeout).size() == (deferred ? 2 : 1) &&
                      client.read_resource("app://chat-lab/commands", timeout).contains("contents"),
                  "Real Chat Lab exposes its read-only MCP command resource");
            if (deferred)
                client.start_notifications(
                    {{"resourceSubscriptions", Json::array({"app://chat-lab/counter"})}}, timeout);
            auto invoke = [&](const std::string& name, const Json& arguments) {
                return deferred
                           ? client.call_tool("cy_tools_call",
                                              {{"name", name}, {"arguments", arguments}}, timeout)
                           : client.call_tool(name, arguments, timeout);
            };
            if (deferred)
                check(client.call_tool("cy_tools_search", Json::object(),
                                       timeout)["structuredContent"]["tools"]
                              .size() == 2,
                      "Compact Chat Lab still discovers every original app command");
            auto read_value = invoke("get_counter", Json::object());
            check(read_value.at("structuredContent").at("value") == 0,
                  "External MCP reads visible app state");
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
            check(invoke("get_counter", Json::object()).at("structuredContent").at("value") ==
                      (writable ? 41 : 0),
                  "Direct SDK changes are visible immediately through MCP");
            auto changed = invoke("set_counter", {{"value", 42}});
            check(changed.value("isError", false) == !writable,
                  "MCP mutations obey the app's live policy");
            check(invoke("get_counter", Json::object()).at("structuredContent").at("value") ==
                      (writable ? 42 : 0),
                  "MCP changes reach the real app state");
            check(invoke("set_counter", {{"value", 1000001}}).value("isError", false),
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
