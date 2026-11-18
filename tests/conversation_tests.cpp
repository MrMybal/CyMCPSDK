#include "test.hpp"
#include <cymcp/ai.hpp>
#include <fstream>
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
        Json image = {{"type", "image"}, {"mimeType", "image/png"}, {"data", "aGVsbG8="}};
        Json audio = {{"type", "audio"}, {"mimeType", "audio/wav"}, {"data", "aGVsbG8="}};
        Json resource = {
            {"type", "resource"},
            {"resource",
             {{"uri", "attachment:spec"}, {"mimeType", "text/plain"}, {"text", "PRIVATE SPEC"}}}};
        Json pdf = {{"type", "resource"},
                    {"resource",
                     {{"uri", "attachment:pdf"},
                      {"name", "spec.pdf"},
                      {"mimeType", "application/pdf"},
                      {"blob", "aGVsbG8="}}}};
        auto content = Json::array({text_block("inspect"), image});
        validate_content(content);
        for (const std::string invalid : {"x", "====", "A===", "aGVsbG8=\n", "AA=A", "AB=="}) {
            auto bad = image;
            bad["data"] = invalid;
            expect_error("InvalidContent", [&] { validate_content(Json::array({bad})); });
        }
        expect_error("InvalidContent", [&] { validate_content(Json::array()); });
        auto bad = image;
        bad["mimeType"] = "text/plain";
        expect_error("InvalidContent", [&] { validate_content(Json::array({bad})); });
        expect_error("UnsupportedContent", [&] { detail::content_text(content); });
        auto directory =
            std::filesystem::temp_directory_path() / ("cymcp-chat-" + random_token(16));
        private_directory(directory);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        } cleanup{directory};
#ifdef __APPLE__
        {
            const auto alias_store =
                std::filesystem::path("/tmp") / ("cymcp-alias-" + random_token(16));
            ConversationStore alias(alias_store);
            const auto resolved = std::filesystem::canonical(alias_store);
            check(resolved.parent_path() == std::filesystem::canonical("/tmp"),
                  "macOS system temporary alias accepts a private conversation directory");
            std::filesystem::remove(resolved);
        }
#endif
#ifndef _WIN32
        {
            const auto link = directory / "history-alias";
            std::filesystem::create_directory_symlink(directory, link);
            expect_error("UnsafePath", [&] { ConversationStore store(link / "history"); });
            std::filesystem::remove(link);
        }
#endif
        {
            std::ofstream file(directory / "image.png", std::ios::binary);
            file << "hello";
        }
        auto file = attachment_from_file(directory / "image.png");
        check(file == image, "File snapshot preserves exact base64 bytes and MIME type");
        auto named_image = detail::content_from_files("", {directory / "image.png"}, {});
        check(
            named_image.at(0).at("name") == "image.png" &&
                detail::content_for(named_image, detail::ContentFormat::Acp) ==
                    Json::array({image}),
            "Markdown keeps the local image filename while ACP receives its portable image block");
        expect_error("ContextTooLarge",
                     [&] { attachment_from_file(directory / "image.png", "", 4); });
        {
            std::ofstream text(directory / "spec.txt");
            text << "hello spec";
        }
        check(attachment_from_file(directory / "spec.txt").at("resource").at("text") ==
                  "hello spec",
              "Text attachment embeds content");
        {
            ProviderConfig config;
            config.process = {
                fixture,
                {"--conversation-fixture", "--fixture-no-auth", "--fixture-user-settings"}};
            auto provider = registry.create("claude-code", config);
            Conversation chat(provider, "fixture-model");
            std::string auth;
            CallContext context;
            context.events = [&](const Event& event) {
                if (event.type == "ProviderReady")
                    auth = event.data.at("authentication").get<std::string>();
            };
            chat.connect("", context);
            check(auth == "unverified",
                  "Claude connect does not claim verified authentication from a runtime check");
            check(chat.send("native context").text == "native context",
                  "Claude can use native provider context without a blocking login probe");
            for (const std::string sources : {"", "user,project,local"}) {
                auto explicit_sources = config;
                explicit_sources.process.arguments = {
                    "--conversation-fixture",
                    sources.empty() ? "--fixture-no-settings" : "--fixture-project-settings"};
                explicit_sources.settings["setting_sources"] = sources;
                Conversation selected(registry.create("claude-code", explicit_sources),
                                      "fixture-model");
                selected.connect();
                check(selected.send("selected settings").text == "selected settings",
                      "Host can select or disable native setting sources explicitly");
            }
            for (const std::string sources :
                 {"unknown", "user,user", "user,", ",user", "user, project"}) {
                auto invalid_sources = config;
                invalid_sources.settings["setting_sources"] = sources;
                expect_error("InvalidOptions",
                             [&] { registry.create("claude-code", invalid_sources); });
            }
            auto unavailable = config;
            unavailable.process.arguments = {"--conversation-fixture", "--fixture-version-failure"};
            expect_error("ConnectionFailed",
                         [&] { registry.create("claude-code", unavailable)->connect(); });
            for (const auto& [flag, code] : std::vector<std::pair<std::string, std::string>>{
                     {"--fixture-auth-failure", "AuthenticationRequired"},
                     {"--fixture-rate-limit", "RateLimited"},
                     {"--fixture-fail", "ProviderError"}}) {
                auto failed = config;
                failed.process.arguments = {"--conversation-fixture", flag};
                failed.process.environment["ANTHROPIC_API_KEY"] = "claude-fixture-secret";
                Conversation rejected(registry.create("claude-code", failed), "fixture-model");
                rejected.connect();
                expect_error(code, [&] { rejected.send("native failure"); });
                const auto row = rejected.transcript().back();
                const auto detail = row.at("error_message").get<std::string>();
                const auto native = code == "RateLimited"              ? "rate_limit"
                                    : code == "AuthenticationRequired" ? "authentication_failed"
                                                                       : "error_during_execution";
                check(row.at("provider_code") == native &&
                          rejected.status().at("last_error") == code &&
                          row.at("content") == "native failure" &&
                          detail.find("native failure") == std::string::npos &&
                          detail.find("Private account diagnostic") == std::string::npos,
                      "Claude reports the native cause without copying content from the account or "
                      "turn");
                if (code != "ProviderError")
                    check(detail.starts_with("The provider refused this generation.") &&
                              detail.find("Credential:") != std::string::npos &&
                              detail.find("claude-fixture-secret") == std::string::npos,
                          "Claude terminal errors preserve the explanations and redact configured "
                          "secrets");
            }
        }
        for (const std::string id : {"command-code", "codebuddy"}) {
            ProviderConfig config;
            config.process = {fixture, {"--extension-fixture", "--fixture-fail"}};
            if (id == "codebuddy")
                config.process.arguments.push_back("--buddy");
            Conversation failed(registry.create(id, config), "fixture-model");
            failed.connect();
            expect_error("ProviderError", [&] { failed.send("private CLI output"); });
            const auto row = failed.transcript().back();
            check(row.at("provider_code") == "error" && row.at("content") == "Partial" &&
                      row.at("error_message").get<std::string>().find("private CLI output") ==
                          std::string::npos,
                  "Native CLI terminal failure keeps its subtype and partial reply");
            if (id == "command-code") {
                config.process.arguments.back() = "--fixture-native-interrupt";
                Conversation cancelled(registry.create(id, config), "fixture-model");
                cancelled.connect();
                expect_error("Cancelled", [&] { cancelled.send("cancelled turn"); });
                check(cancelled.transcript().back().at("content") == "Partial",
                      "A native Command Code interruption is cancelled and retains the partial "
                      "response");
            }
        }
        for (bool agy : {false, true}) {
            ProviderConfig config;
            config.process = {fixture, {"--conversation-fixture"}};
            if (agy)
                config.process.arguments.push_back("--agy");
            auto provider = registry.create(agy ? "antigravity" : "claude-code", config);
            Conversation chat(provider, "fixture-model");
            chat.connect();
            check(provider->capabilities().persistent_process,
                  "Connector advertises implemented persistent transport");
            int starts = 0, deltas = 0;
            CallContext context;
            context.events = [&](const Event& event) {
                if (event.type == "ProcessStarted")
                    ++starts;
                if (event.type == "TextDelta")
                    ++deltas;
            };
            auto first = chat.send("remember-marker", context);
            auto second = chat.send("recall", context);
            check(first.text == "remember-marker" && second.text == first.text,
                  "Two turns preserve in-process memory");
            check(first.usage.at("instance") == second.usage.at("instance") &&
                      second.usage.at("turn") == 2,
                  "Two turns use the same process instance");
            check(starts == 1 && deltas == 2,
                  "Persistent connector starts once and streams both turns");
            check(second.metrics.at("total_ms").get<double>() >= 0 &&
                      !second.metrics.at("first_token_ms").is_null(),
                  "Conversation measures first token and total time");
            check(chat.status().at("state") == "ready", "Conversation returns to ready state");
            auto snapshot = chat.checkpoint();
            ConversationStore store(directory / (agy ? "agy-history" : "claude-history"));
            store.save("main", chat);
            store.save("main", chat);
            check(store.load("main") == snapshot,
                  "Atomic checkpoint replacement round-trips native session and history");
            expect_error("UnsafePath", [&] { store.load("../outside"); });
            expect_error("UnsafePath", [&] { store.save("CON:bad", chat); });
            auto saved_history = chat.history();
            chat.disconnect();
            check(chat.status().at("state") == "disconnected",
                  "Explicit disconnect updates conversation state");
            chat.reconnect();
            check(chat.session().id == snapshot.at("session").at("id").get<std::string>() &&
                      chat.history() == saved_history,
                  "Reconnect keeps the exact native session and history");
            auto resumed = chat.send("after reconnect");
            check(resumed.text == "after reconnect" && resumed.usage.at("turn") == 1,
                  "Reconnect never resends a previous prompt");
            Conversation restored(registry.create(agy ? "antigravity" : "claude-code", config),
                                  "fixture-model");
            restored.restore(store.load("main"));
            check(restored.history() == saved_history,
                  "New conversation restores persisted history");
            check(restored.send("restored").text == "restored",
                  "Restored native conversation sends a new turn");
            auto wrong = snapshot;
            wrong["provider"] = "other";
            expect_error("SessionInvalid", [&] { restored.restore(wrong); });
            wrong = snapshot;
            wrong["version"] = 42;
            expect_error("UnsupportedVersion", [&] { restored.restore(wrong); });
            wrong = snapshot;
            wrong["messages"] = Json::array({{{"role", "administrator"}, {"content", "bad"}}});
            expect_error("InvalidCheckpoint", [&] { restored.restore(wrong); });
            if (!agy) {
                auto response = restored.send(content);
                check(response.usage.at("received").at(1).at("source").at("media_type") ==
                          "image/png",
                      "Claude persistent input receives native image source blocks");
                response = restored.send(Json::array({text_block("pdf"), pdf}));
                check(response.usage.at("received").at(1).at("type") == "document",
                      "Claude persistent input receives PDF documents");
            } else
                expect_error("UnsupportedContent", [&] { restored.send(content); });
            Executor executor(1);
            auto future = restored.send_async(executor, "__silent");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            expect_error("Busy", [&] { restored.checkpoint(); });
            restored.cancel();
            expect_error("Cancelled", [&] { future.get(); });
            check(restored.status().at("last_error") == "Cancelled",
                  "Cancellation is retained for recovery");
            restored.reconnect();
            check(restored.send("new explicit turn").text == "new explicit turn",
                  "Exact reconnect works after cancelling a silent persistent turn");
            store.erase("main");
            expect_error("FileError", [&] { store.load("main"); });
        }
        for (const std::string id : {"opencode", "gemini", "qwen-code", "kimi", "goose",
                                     "command-code-acp", "codebuddy-acp"}) {
            ProviderConfig config;
            config.process = {fixture,
                              {"--extension-fixture", "--acp-fixture", "--fixture-multimodal"}};
            config.process.arguments.push_back("--fixture-preset-" + id);
            auto provider = registry.create(id, config);
            Conversation chat(provider, "fixture-model");
            chat.connect();
            check(provider->capabilities().image_input && provider->capabilities().audio_input &&
                      provider->capabilities().file_input,
                  "ACP capabilities are negotiated for every preset");
            auto result = chat.send(
                Json::array({text_block("ACP attachments"), image, audio, resource, pdf}));
            check(result.text == "ACP attachments" && result.usage.at("received").size() == 5 &&
                      !result.usage.at("received").at(4).at("resource").contains("name"),
                  "ACP preserves all negotiated prompt content blocks");
            chat.reconnect();
            check(chat.send("next").text == "next", "ACP preset resumes its exact session");
        }
        {
            AcpOptions options;
            options.process = {fixture, {"--extension-fixture", "--acp-fixture", "--fixture-fail"}};
            options.process.environment["ACP_ACCESS_TOKEN"] = "fixture-acp-17";
            Conversation chat(std::make_shared<AcpProvider>(options), "fixture-model");
            chat.connect();
            try {
                chat.send("Partial answer");
                check(false, "ACP prompt rejection fails the turn");
            } catch (const Error& error) {
                const std::string detail = error.what();
                check(error.code == "ProviderError" && error.provider_code == "-32603" &&
                          detail.starts_with("The selected model is unavailable for account ") &&
                          detail.find("fixture-acp-17") == std::string::npos &&
                          detail.find("unrelated-private-trace") == std::string::npos,
                      "ACP RPC errors retain their explanation and mask process credentials");
                const auto row = chat.transcript().back();
                check(row.at("error_code") == error.code && row.at("provider_code") == "-32603" &&
                          row.at("error_message") == detail &&
                          row.at("content") == "Partial answer",
                      "ACP rejection reaches the failed turn alongside its partial response");
            }
        }
        ProviderConfig unsupported;
        unsupported.process = {fixture, {"--extension-fixture", "--acp-fixture"}};
        Conversation text_only(registry.create("acp", unsupported), "fixture-model");
        text_only.connect();
        expect_error("UnsupportedContent", [&] { text_only.send(content); });
        int host_calls = 0;
        for (bool terminal : {false, true})
            for (bool allow : {false, true})
                for (bool foreign : {false, true}) {
                    ProviderConfig config;
                    config.process = {
                        fixture,
                        {"--extension-fixture", "--acp-fixture", "--fixture-client-service"}};
                    if (terminal)
                        config.process.arguments.push_back("--fixture-client-terminal");
                    if (foreign)
                        config.process.arguments.push_back("--fixture-foreign-service");
                    config.settings["client_capabilities"] =
                        terminal ? Json{{"terminal", true}}
                                 : Json{{"fs", {{"readTextFile", true}}}};
                    config.settings["allow_all_actions"] = allow;
                    config.request_handler = [&](const std::string& method, const Json& params,
                                                 const CallContext&) {
                        ++host_calls;
                        check(params.at("sessionId") == "acp-fixture-session",
                              "Host service receives only the active session");
                        return method == "terminal/output"
                                   ? Json{{"output", "host output"}, {"truncated", false}}
                                   : Json{{"content", "host text"}};
                    };
                    Conversation chat(registry.create("acp", config), "fixture-model");
                    chat.connect();
                    auto before = host_calls;
                    auto result = chat.send("host services");
                    const auto& response = result.usage.at("host_response");
                    check((host_calls - before) == (allow && !foreign ? 1 : 0),
                          "Host services require explicit capability, policy and correct session");
                    check(response.contains(allow && !foreign ? "result" : "error"),
                          "ACP returns a correlated host service response");
                }
        auto no_handler = unsupported;
        no_handler.settings["client_capabilities"] = {{"terminal", true}};
        expect_error("InvalidOptions", [&] { registry.create("acp", no_handler); });
        {
            CodexOptions options;
            options.process = {fixture, {"--codex-fixture", "--fixture-check-image"}};
            Conversation codex(std::make_shared<CodexProvider>(options), "fixture-model");
            codex.connect();
            check(codex.send(content).text == "Hello fixture",
                  "Codex forwards portable images using native input URLs");
        }
        {
            CodexOptions options;
            options.process = {fixture, {"--codex-fixture"}};
            const auto turns = directory / "attempts.txt";
            options.process.environment["CYMCP_FIXTURE_TURNS_FILE"] = path_utf8(turns);
            Conversation codex(std::make_shared<CodexProvider>(options), "fixture-model");
            codex.connect();
            const std::string refusal =
                "The 'gpt-6.2-sol' model is not supported when using Codex with a ChatGPT account.";
            std::vector<Json> errors;
            std::string partial;
            CallContext observed;
            observed.events = [&](const Event& event) {
                if (event.type == "ProviderError")
                    errors.push_back(event.data);
                if (event.type == "TextDelta")
                    partial += event.data.at("text").get<std::string>();
            };
            for (const std::string prompt :
                 {"rpc-model-refusal", "partial-model-refusal", "completed-model-refusal"}) {
                errors.clear();
                partial.clear();
                try {
                    codex.send(prompt, observed);
                    throw std::runtime_error("Expected the model refusal");
                } catch (const Error& error) {
                    const std::string explanation = error.what();
                    check(error.code == "ProviderError" && explanation.starts_with(refusal),
                          "Both RPC and generation errors retain Codex's model refusal");
                    check(errors.size() == 1 && errors[0].at("code") == error.code &&
                              errors[0].at("message") == explanation &&
                              errors[0].dump().find("rpc-private-payload") == std::string::npos &&
                              explanation.find("fixture-sensitive-token") == std::string::npos,
                          "Only the sanitized provider explanation reaches error observers");
                    if (prompt == "rpc-model-refusal")
                        check(error.provider_code == "-32602" &&
                                  errors[0].at("rpc_code") == -32602 && explanation == refusal,
                              "Codex keeps the JSON-RPC error number alongside its message");
                    else
                        check(error.provider_code == "badRequest" &&
                                  errors[0].at("native_code") == "badRequest",
                              "Codex keeps the native error category alongside its message");
                    check(partial == (prompt == "partial-model-refusal" ? "Partial answer." : ""),
                          "A failed turn preserves exactly the text delivered before the failure");
                }
            }
            errors.clear();
            check(codex.send("retry-then-success", observed).text == "Hello fixture" &&
                      errors.empty(),
                  "A transient provider retry does not fail or resubmit the generation");
            expect_error("Cancelled", [&] { codex.send("native-interrupt", observed); });
            check(errors.empty() && codex.send("next").text == "Hello fixture",
                  "A native interruption is distinct from failure and leaves the session usable");
            std::ifstream attempts(turns);
            check(std::count(std::istreambuf_iterator<char>(attempts),
                             std::istreambuf_iterator<char>(), '\n') == 6,
                  "Codex receives exactly one turn/start per explicit send, including failures");
        }
        {
            CodexOptions options;
            options.process = {fixture, {"--codex-fixture", "--fixture-account-stale-default"}};
            Conversation codex(std::make_shared<CodexProvider>(options), "");
            codex.connect("", {}, true);
            check(codex.session().model == "gpt-6-luna" && codex.send("test").text == "Hello fixture",
                  "Automatic Codex startup uses the listed default when the profile model is absent");
        }
        {
            ProviderConfig config;
            config.process = {fixture, {"--codex-exec-fixture"}};
            const auto turns = directory / "exec-attempts.txt";
            config.process.environment["CYMCP_FIXTURE_TURNS_FILE"] = path_utf8(turns);
            Conversation chat(registry.create("codex-exec", config), "fixture");
            chat.connect();
            for (const std::string prompt : {"exec-error", "exec-turn-failed"}) {
                try {
                    chat.send(prompt);
                    throw std::runtime_error("Expected an exec model refusal");
                } catch (const Error& e) {
                    const std::string message = e.what();
                    check(e.code == "ProviderError" &&
                              message.find(
                                  "not supported when using Codex with a ChatGPT account.") !=
                                  std::string::npos &&
                              message.find("fixture-sensitive-token") == std::string::npos,
                          "Codex exec preserves the redacted reason from error and turn.failed");
                }
            }
            check(chat.send("exec-recovered").text == "exec-recovered",
                  "A recovered Codex exec error does not hide the successful result");
            std::ifstream attempts(turns);
            check(std::count(std::istreambuf_iterator<char>(attempts),
                             std::istreambuf_iterator<char>(), '\n') == 3,
                  "Codex exec only receives the requested generations");
        }
        {
            CodexOptions options;
            options.process = {fixture, {"--codex-fixture"}};
            auto provider = std::make_shared<CodexProvider>(options);
            Conversation codex(provider, "fixture-model");
            codex.connect();
            expect_error("RateLimited", [&] { codex.send("native-failure"); });
            const auto native_messages = codex.history();
            expect_error("Unsupported", [&] { codex.edit_message(0, "edit"); });
            expect_error("Unsupported", [&] { codex.delete_turn(0); });
            check(codex.history() == native_messages,
                  "Native provider history is unchanged by edit and delete requests");
            codex.reconnect();
            Executor executor(1);
            auto pending = codex.send_async(executor, "silent");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            provider->disconnect();
            expect_error("Cancelled", [&] { pending.get(); });
            codex.reconnect();
            check(codex.send("next").text == "Hello fixture",
                  "Codex reconnects after direct provider disconnect during a silent turn");
        }
        Server server;
        Json received;
        std::atomic<int> generations = 0;
        std::mutex received_mutex;
        auto capture = [&](const auto& request) {
            ++generations;
            std::lock_guard lock(received_mutex);
            received = parse_json(request.body, 8 * 1024 * 1024);
        };
        server.http.Get("/models", [](const auto&, auto& response) {
            response.set_content("{\"data\":[{\"id\":\"fixture-model\"}]}", "application/json");
        });
        server.http.Get("/key", [](const auto&, auto& response) {
            response.set_content("{}", "application/json");
        });
        server.http.Get("/api/tags", [](const auto&, auto& response) {
            response.set_content("{\"models\":[{\"name\":\"fixture-model\"}]}", "application/json");
        });
        server.http.Post("/responses", [&](const auto& request, auto& response) {
            capture(request);
            response.set_content(
                sse({{"type", "response.output_text.delta"}, {"delta", "answer"}}) +
                    sse({{"type", "response.completed"},
                         {"response",
                          {{"status", "completed"},
                           {"output",
                            Json::array({{{"type", "message"},
                                          {"role", "assistant"},
                                          {"content", Json::array({{{"type", "output_text"},
                                                                    {"text", "answer"}}})}}})}}}}),
                "text/event-stream");
        });
        server.http.Post("/messages", [&](const auto& request, auto& response) {
            capture(request);
            response.set_content(
                sse({{"type", "message_start"}, {"message", {{"usage", Json::object()}}}}) +
                    sse({{"type", "content_block_start"},
                         {"index", 0},
                         {"content_block", {{"type", "text"}, {"text", ""}}}}) +
                    sse({{"type", "content_block_delta"},
                         {"index", 0},
                         {"delta", {{"type", "text_delta"}, {"text", "answer"}}}}) +
                    sse({{"type", "content_block_stop"}, {"index", 0}}) +
                    sse({{"type", "message_delta"}, {"delta", {{"stop_reason", "end_turn"}}}}) +
                    sse({{"type", "message_stop"}}),
                "text/event-stream");
        });
        server.http.Post("/api/chat", [&](const auto& request, auto& response) {
            capture(request);
            response.set_content("{\"message\":{\"role\":\"assistant\",\"content\":\"answer\"},"
                                 "\"done\":true,\"done_reason\":\"stop\"}\n",
                                 "application/x-ndjson");
        });
        server.http.Post("/chat/completions", [&](const auto& request, auto& response) {
            capture(request);
            response.set_content(sse({{"choices", Json::array({{{"index", 0},
                                                                {"delta", {{"content", "answer"}}},
                                                                {"finish_reason", "stop"}}})}}) +
                                     "data: [DONE]\n\n",
                                 "text/event-stream");
        });
        server.start();
        for (const std::string id :
             {"openai", "anthropic", "ollama", "openrouter", "openai-compatible"}) {
            ProviderConfig config;
            config.api_key = "test-key-not-persisted";
            config.settings["base_url"] = server.url;
            Conversation chat(registry.create(id, config), "fixture-model");
            chat.connect();
            auto input = content;
            input.push_back(resource);
            if (id == "openai" || id == "anthropic")
                input.push_back(pdf);
            if (id == "openai-compatible" || id == "openrouter")
                input.push_back(audio);
            check(chat.send(input).text == "answer",
                  "API emits a terminal response to portable multimodal input");
            Json body;
            {
                std::lock_guard lock(received_mutex);
                body = received;
            }
            if (id == "openai")
                check(body.at("input").at(0).at("content").at(1).at("type") == "input_image" &&
                          body.at("input").at(0).at("content").at(3).at("type") == "input_file",
                      "Responses wire contains image and file inputs");
            else if (id == "anthropic")
                check(body.at("messages").at(0).at("content").at(1).at("source").at("data") ==
                              image.at("data") &&
                          body.at("messages").at(0).at("content").at(3).at("type") == "document",
                      "Messages wire contains image sources and documents");
            else if (id == "ollama")
                check(body.at("messages").at(0).at("images").at(0) == image.at("data") &&
                          body.at("messages")
                                  .at(0)
                                  .at("content")
                                  .get<std::string>()
                                  .find("PRIVATE SPEC") != std::string::npos,
                      "Ollama receives native images array and embedded text");
            else
                check(body.at("messages").at(0).at("content").at(1).at("type") == "image_url" &&
                          body.at("messages").at(0).at("content").at(3).at("type") == "input_audio",
                      "Chat completions wire contains image and audio blocks");
            auto saved = chat.checkpoint();
            check(saved.dump().find(config.api_key) == std::string::npos,
                  "Conversation checkpoints exclude provider credentials");
            chat.disconnect();
            Conversation restored(registry.create(id, config), "fixture-model");
            auto before_restore = generations.load();
            restored.restore(saved);
            check(generations.load() == before_restore,
                  "Restore never submits a generation or repeats an action");
            check(restored.history() == chat.history() &&
                      restored.session().id == chat.session().id,
                  "Stateless API restores host history and local conversation identity");
            check(restored.send("follow-up").text == "answer", "Restored API accepts a fresh turn");
            {
                std::lock_guard lock(received_mutex);
                body = received;
            }
            check(body.at(id == "openai" ? "input" : "messages").size() >= 3,
                  "API reconnect carries complete prior context");
            const auto incompatible =
                Json::array({(id == "openrouter" || id == "openai-compatible") ? pdf : audio});
            const auto successful = restored.history();
            const auto before_refusal = generations.load();
            expect_error("UnsupportedContent", [&] { restored.send(incompatible); });
            check(restored.history() == successful && generations.load() == before_refusal,
                  "A rejected attachment changes neither committed history nor the server");
            auto rejected_checkpoint = restored.checkpoint();
            check(restored.send("text after rejection").text == "answer",
                  "A text turn succeeds immediately after a rejected attachment");
            restored.restore(rejected_checkpoint);
            check(restored.send("text after restoring rejection").text == "answer",
                  "A checkpoint saved after a rejected attachment remains usable");

            // Earlier versions persisted both the incompatible input and later unsent retries.
            auto legacy = rejected_checkpoint;
            legacy["messages"].push_back({{"role", "user"}, {"content", incompatible}});
            legacy["messages"].push_back({{"role", "user"}, {"content", "never delivered"}});
            const auto before_repair = generations.load();
            restored.restore(legacy);
            check(restored.history() == successful && generations.load() == before_repair,
                  "Restore repairs only the rejected legacy suffix without generation");
            check(restored.send("next after migration").text == "answer",
                  "Migrated conversation accepts a new turn");

            if (id == "ollama") {
                Executor worker(1);
                std::promise<void> release;
                auto gate = release.get_future();
                worker.post([&] { gate.wait(); });
                auto pending = restored.send_async(worker, "queued turn");
                const auto queued = Clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                const auto waited =
                    std::chrono::duration<double, std::milli>(Clock::now() - queued).count();
                release.set_value();
                const auto metrics = pending.get().metrics;
                check(metrics.at("first_token_ms").get<double>() >= waited &&
                          metrics.at("total_ms").get<double>() >= waited,
                      "First token and total timing include work queued before dispatch");
            }
        }
        std::cout << checks << " conversation checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
