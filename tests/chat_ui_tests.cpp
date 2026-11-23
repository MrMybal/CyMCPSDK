#include "test.hpp"
#include <algorithm>
#include <cymcp/chat_ui.hpp>
#include <cyui/Core.h>
#include <cyui/modules/DocumentViews.h>
#include <fstream>
#ifdef CYMCP_CHAT_TEST_WIN32_CLIPBOARD
#include "chat_clipboard_fixture.hpp"
#endif
using namespace cymcp;
namespace {
constexpr const char* model_refusal =
    "The 'gpt-6.2-sol' model is not supported when using Codex with a ChatGPT account.";
struct Probe {
    std::atomic_int sends{0}, connects{0}, stops{0}, attachments{0};
    std::atomic_bool silent{false}, fail{false}, connecting{false}, flood{false}, oversized{false},
        large_thinking{false}, markdown_stream{false}, reject_model{false}, select_model{false};
};
class ProviderFixture final : public Provider {
    std::shared_ptr<Probe> probe_;

  public:
    explicit ProviderFixture(std::shared_ptr<Probe> p) : probe_(std::move(p)) {}
    bool uses_host_history() const noexcept override {
        return true;
    }
    ProviderCapabilities capabilities() const override {
        return {true, true, true, true, true, false, false, false, false, probe_->select_model};
    }
    void connect(const CallContext& context) override {
        context.cancellation.check();
        ++probe_->connects;
        while (probe_->connecting) {
            context.cancellation.check();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        context.emit("ProviderReady", {{"private", "MUST_NOT_APPEAR"}});
    }
    Json list_models(const CallContext&) override {
        if (probe_->select_model)
            return Json::array({{{"model", "fixture-model"}}, {{"model", "second-model"}}});
        return Json::array({{{"id", "fixture-model"}}});
    }
    Session start_session(std::string model, std::string, const CallContext& context,
                          std::string) override {
        context.cancellation.check();
        return {"fixture-id", model};
    }
    ChatResult send(Session&, Json& history, const CallContext& context) override {
        ++probe_->sends;
        if (probe_->reject_model)
            throw Error("ProviderError", model_refusal, "-32602");
        if (probe_->markdown_stream) {
            context.emit("ThinkingDelta",
                         {{"text", "https://fixture.invalid/thought\n\n![Thinking](thought.png)"}});
            std::string text = "## Résultat\n\n| Figure |\n| --- |\n| ![Table][initial] |\n\n"
                               "[initial]: table.png\n\nhttps://fixture.invalid/cached\n\n"
                               "```md\n![Literal](code.png)\n";
            context.emit("TextDelta", {{"text", text}});
            while (probe_->markdown_stream) {
                context.cancellation.check();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            const std::string tail = "```\n\n![Après][final]\n\n[final]: final.png\n";
            context.emit("TextDelta", {{"text", tail}});
            text += tail;
            history.push_back({{"role", "assistant"}, {"content", text}});
            return {text, "stop"};
        }
        if (probe_->flood)
            for (int n = 0; n < 3000; ++n)
                context.emit("ToolStarted", {{"name", "fixture-tool"}});
        if (probe_->oversized) {
            std::string text(16 * 1024 * 1024 + 1, 'x');
            context.emit("TextDelta", {{"text", text}});
            return {text, "stop"};
        }
        if (history.back().at("content").is_array())
            ++probe_->attachments;
        while (probe_->silent) {
            context.cancellation.check();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        context.emit("ThinkingDelta",
                     {{"text", probe_->large_thinking ? std::string(2 * 1024 * 1024 - 1024, 't')
                                                      : "a thought"}});
        context.emit("ToolStarted", {{"name", "read_demo"}, {"private", "MUST_NOT_APPEAR"}});
        context.emit("TextDelta", {{"text", "**partial"}});
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        context.cancellation.check();
        if (probe_->fail) {
            // A provider can commit a shorter snapshot before reporting a stream failure.
            history.push_back({{"role", "assistant"}, {"content", "**par"}});
            throw Error("FixtureFailure", std::string(model_refusal) +
                                              " Credentials echoed: SECRET_NOT_IN_HISTORY / "
                                              "private-key-should-not-be-displayed; "
                                              "![detail](private-diagnostic.png)");
        }
        context.emit("TextDelta", {{"text", "**"}});
        context.emit("ToolCompleted", {{"name", "read_demo"}});
        context.emit("TurnCompleted", {{"text", "**authoritative**"}});
        history.push_back({{"role", "assistant"}, {"content", "**authoritative**"}});
        return {"**authoritative**", "stop", {{"tokens", 7}}};
    }
    void disconnect() noexcept override {
        ++probe_->stops;
    }
};
void wait(ChatController& chat) {
    auto until = Clock::now() + std::chrono::seconds(8);
    do {
        chat.poll();
        if (!chat.state().busy)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (Clock::now() < until);
    throw std::runtime_error("Chat worker timeout");
}
bool rendered_texture(ImTextureID id) {
    for (const auto* list : ImGui::GetDrawData()->CmdLists)
        for (const auto& command : list->CmdBuffer)
            if (command.ElemCount && command.GetTexID() == id)
                return true;
    return false;
}
// Markdown emits glyphs straight into the draw list. Identify an actual rendered ASCII word
// by matching each quad to its glyph in the same font atlas that rendered the transcript.
bool rendered_word(ImFontBaked& font, std::string_view word, ImVec2* position = nullptr) {
    for (const auto* list : ImGui::GetDrawData()->CmdLists) {
        for (int start = 0; start + 4 * static_cast<int>(word.size()) <= list->VtxBuffer.Size;
             ++start) {
            bool match = true;
            int index = start;
            for (const unsigned char c : word) {
                const auto* glyph = font.FindGlyphNoFallback(c);
                const auto min = list->VtxBuffer[index].uv, max = list->VtxBuffer[index + 2].uv;
                if (!glyph || min.x != glyph->U0 || min.y != glyph->V0 || max.x != glyph->U1 ||
                    max.y != glyph->V1) {
                    match = false;
                    break;
                }
                index += 4;
            }
            if (match) {
                if (position) {
                    const auto a = list->VtxBuffer[start].pos, b = list->VtxBuffer[start + 2].pos;
                    *position = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
                }
                return true;
            }
        }
    }
    return false;
}
} // namespace
int main() {
    try {
        auto probe = std::make_shared<Probe>();
        auto registry = std::make_shared<ProviderRegistry>(false);
        registry->add(
            {"fixture", "Fixture", "fixture", "none", {}},
            [probe](const ProviderConfig&) { return std::make_shared<ProviderFixture>(probe); });
        ChatController chat(registry);
        expect_error("InvalidState", [&] { chat.send("hello"); });
        auto directory = std::filesystem::temp_directory_path() / ("cymcp-ui-" + random_token(16));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{directory};
        ChatConnection connection;
        connection.provider_id = "fixture";
        connection.model = "fixture-model";
        connection.history_directory = directory;
        connection.history_id = "chat";
        connection.provider.api_key = "SECRET_NOT_IN_HISTORY";
        connection.provider.process.environment["FIXTURE_ACCESS_TOKEN"] =
            "private-key-should-not-be-displayed";
        auto started = Clock::now();
        chat.open(connection);
        check(Clock::now() - started < std::chrono::milliseconds(100),
              "Connection scheduled without blocking the UI");
        check(chat.state().busy, "Controller reserves connecting operation immediately");
        expect_error("Busy", [&] { chat.open(connection); });
        wait(chat);
        check(chat.state().connected && chat.state().session_id == "fixture-id",
              "Native session metadata published");
        check(chat.state().diagnostics.size() >= 3 &&
                  chat.state().diagnostics.back().code == "Completed" &&
                  chat.state().diagnostics.back().timestamp_ms > 0,
              "Connection stages and timestamped completion reach structured diagnostics");
        chat.refresh_models();
        wait(chat);
        check(chat.state().models.at(0).at("id") == "fixture-model",
              "Catalogue populated from provider");
        expect_error("InvalidContent", [&] { chat.send(" \n"); });
        chat.send("hello");
        expect_error("Busy", [&] { chat.send("duplicate"); });
        bool streamed = false;
        while (chat.state().busy) {
            chat.poll();
            streamed |= chat.state().busy && chat.state().messages.back().markdown == "**partial";
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(streamed, "Partial Markdown reaches the UI before turn completion");
        check(chat.state().messages.size() == 2 &&
                  chat.state().messages.back().markdown == "**authoritative**",
              "Final snapshot replaces deltas without duplication");
        check(chat.state().thinking == "a thought", "Thinking stream retained separately");
        check(chat.state().activity.size() == 2, "Tool lifecycle reaches the activity panel");
        check(chat.state().metrics.contains("first_token_ms") &&
                  chat.state().usage.at("tokens") == 7,
              "Measured timings and usage published");
        auto first_id = chat.state().messages.front().id;
        ConversationStore store(directory);
        check(store.load("chat").dump().find("SECRET_NOT_IN_HISTORY") == std::string::npos,
              "API key excluded from history");
        probe->silent = true;
        chat.send("cancel silently");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        started = Clock::now();
        chat.cancel();
        wait(chat);
        check(Clock::now() - started < std::chrono::seconds(1),
              "Silent generation cancellation completes promptly");
        check(chat.state().phase == "interrupted" && chat.state().error_code == "Cancelled",
              "Cancelled turn never reported successful");
        check(chat.state().metrics.contains("total_ms"),
              "Failed turns retain their measured timings");
        check(chat.state().messages.back().interrupted && chat.state().messages.back().error_code.empty(),
              "User cancellation marks the row interrupted without a provider failure");
        probe->silent = false;
        int sends = probe->sends;
        chat.reconnect();
        wait(chat);
        check(probe->sends == sends, "Reconnection does not submit a generation");
        check(chat.state().messages.front().id != first_id,
              "Restored rows receive fresh stable IDs");
        probe->fail = true;
        chat.send("fail");
        wait(chat);
        const auto failed_row = chat.state().messages.back();
        check(chat.state().error_code == "FixtureFailure" && chat.state().phase == "failed" &&
                  failed_row.markdown == "**partial" && !failed_row.interrupted &&
                  failed_row.error_code == chat.state().error_code &&
                  failed_row.error_message == chat.state().error_message &&
                  failed_row.error_message.find(model_refusal) != std::string::npos,
              "Failed assistant row retains its partial answer and the provider explanation");
        check(chat.state().diagnostics.back().code == "FixtureFailure" &&
                  chat.state().diagnostics.back().level == "error" &&
                  chat.state().diagnostics.back().message == failed_row.error_message,
              "Failed turn has a persistent diagnostic with the same sanitized detail");
        bool private_diagnostics = true;
        for (const auto& row : chat.state().diagnostics)
            private_diagnostics &=
                row.message.find("private-key-should-not-be-displayed") == std::string::npos &&
                row.message.find("MUST_NOT_APPEAR") == std::string::npos &&
                row.message.find("SECRET_NOT_IN_HISTORY") == std::string::npos &&
                row.message.find("**partial") == std::string::npos;
        check(private_diagnostics,
              "Diagnostic records omit private native payloads, secrets and response text");
        probe->fail = false;
        const auto sends_after_failure = probe->sends.load();
        chat.reconnect();
        wait(chat);
        check(probe->sends == sends_after_failure &&
                  chat.state().messages.back().markdown == failed_row.markdown &&
                  chat.state().messages.back().error_message == failed_row.error_message &&
                  chat.state().messages.back().error_code == failed_row.error_code,
              "Reconnecting preserves the failed turn and partial output without regeneration");
        auto attachment = directory / "sample.txt";
        {
            std::ofstream file(attachment);
            file << "attachment";
        }
        {
            std::vector<std::filesystem::path> files{attachment, attachment};
            auto payload = detail::content_from_files("line\n\"note\"", files, {});
            auto exact = payload.dump().size();
            check(detail::json_size(payload, exact) == exact &&
                      detail::content_from_files("line\n\"note\"", files, {}, exact).size() == 3,
                  "Cumulative budget includes delimiters, names and escaped text");
            expect_error("ContextTooLarge", [&] {
                detail::content_from_files("line\n\"note\"", files, {}, exact - 1);
            });
            files.push_back(directory / "file-that-must-not-be-read");
            expect_error("ContextTooLarge", [&] {
                detail::content_from_files("line\n\"note\"", files, {}, exact - 1);
            });
            ChatController many(registry);
            ChatConnection config;
            config.provider_id = "fixture";
            config.model = "fixture-model";
            many.open(config);
            wait(many);
            files.assign(256, attachment);
            for (const std::string prompt : {"", "Read these files"}) {
                many.send(prompt, files);
                wait(many);
                check(many.state().error_code.empty() &&
                          many.state()
                                  .messages[many.state().messages.size() - 2]
                                  .attachments.size() == 256,
                      "256 attachments reach the provider with or without an accompanying message");
            }
            files.push_back(attachment);
            expect_error("InvalidContent", [&] { many.send("", files); });
        }
        const auto previous_attachments = probe->attachments.load();
        chat.send("file", {attachment});
        wait(chat);
        check(probe->attachments == previous_attachments + 1,
              "Attachment read and converted on the worker");
        const auto& user_row = chat.state().messages[chat.state().messages.size() - 2];
        check(user_row.markdown == "file" && user_row.attachments.size() == 1 &&
                  user_row.attachments[0].name == "sample.txt" &&
                  user_row.attachments[0].bytes == 10 && user_row.attachments[0].content &&
                  user_row.attachments[0].source.find(path_utf8(directory)) == std::string::npos,
              "Native attachment cards retain embedded bytes and omit private parent paths");
        chat.save();
        wait(chat);
        chat.disconnect();
        wait(chat);
        check(!chat.state().connected && chat.state().phase == "disconnected",
              "Disconnect keeps transcript and marks connection closed");
        const auto sends_before_restore = probe->sends.load();
        connection.restore = true;
        std::filesystem::remove(attachment);
        chat.open(connection);
        wait(chat);
        check(chat.state().connected && !chat.state().messages.empty(),
              "Stored host history restored asynchronously");
        check(chat.state().messages[chat.state().messages.size() - 2].attachments.size() == 1 &&
                  chat.state().messages[chat.state().messages.size() - 2].attachments[0].bytes ==
                      10,
              "Restored history rebuilds attachment cards from embedded contents without files");
        const auto& restored_rows = chat.state().messages;
        const auto restored_failure = std::find_if(restored_rows.begin(), restored_rows.end(),
            [](const ChatMessage& row) { return row.error_code == "FixtureFailure"; });
        check(restored_failure != restored_rows.end() && !restored_failure->interrupted &&
                  restored_failure->markdown == failed_row.markdown &&
                  restored_failure->error_message == failed_row.error_message,
              "Stored conversation restores the partial answer and safe failure on the same row");
        {
            std::ofstream file(attachment);
            file << "attachment";
        }
        check(probe->sends == sends_before_restore, "Restore never resubmits messages");

        probe->connecting = true;
        chat.open(connection);
        chat.cancel();
        wait(chat);
        check(!chat.state().connected && chat.state().session_id.empty(),
              "Cancelled new connection never inherits metadata from the previous session");
        probe->connecting = false;
        connection.restore = false;
        chat.open(connection);
        wait(chat);
        probe->flood = true;
        chat.send("flood");
        // Deliberately stop UI consumption: deltas coalesce instead of failing the turn.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait(chat);
        check(chat.state().error_code.empty() && chat.state().phase == "ready" &&
                  chat.state().activity.size() <= 128,
              "Slow UI never fails a generation and keeps bounded activity");
        check(chat.state().diagnostics.size() <= 1000,
              "Long tool activity keeps bounded diagnostic memory");
        probe->flood = false;
        probe->oversized = true;
        chat.send("large");
        wait(chat);
        check(chat.state().error_code.empty() && chat.state().phase == "ready" &&
                  chat.state().messages.back().markdown.size() <= 16 * 1024 * 1024 + 256 &&
                  chat.state().messages.back().markdown.find("display truncated") !=
                      std::string::npos,
              "Oversized answer completes; only its display is truncated");
        probe->oversized = false;
        auto broken_storage = connection;
        broken_storage.history_id = "../invalid";
        chat.open(broken_storage);
        wait(chat);
        chat.send("successful response, failed storage");
        wait(chat);
        check(chat.state().phase == "ready" && chat.state().error_code.empty() &&
                  !chat.state().storage_error.empty(),
              "Storage error remains distinct from successful generation");
        {
            ChatController bounded(registry);
            auto ephemeral = connection;
            ephemeral.history_directory.clear();
            ephemeral.history_id.clear();
            bounded.open(ephemeral);
            wait(bounded);
            probe->large_thinking = true;
            for (int n = 0; n < 5; ++n) {
                bounded.send("short turn");
                wait(bounded);
            }
            check(bounded.state().error_code.empty() && bounded.state().messages.size() == 10,
                  "Replaced thinking text releases its display budget across turns");
            probe->large_thinking = false;
        }

        {
            auto api_probe = std::make_shared<Probe>();
            auto api_registry = std::make_shared<ProviderRegistry>(false);
            api_registry->add({"test-api", "Test API", "http", "api-key", {}},
                              [api_probe](const ProviderConfig&) {
                                  return std::make_shared<ProviderFixture>(api_probe);
                              });
            ChatController api_chat(api_registry);
            ChatConnection api_connection;
            api_connection.provider_id = "test-api";
            api_connection.automatic_model = true;
            api_chat.open(api_connection);
            wait(api_chat);
            check(api_chat.state().phase == "choosing_model" && !api_chat.state().connected &&
                      api_chat.state().models.size() == 1 && api_probe->sends == 0,
                  "Remote API exposes its catalogue and asks for a model without submitting "
                  "generation");
            check(api_chat.state().diagnostics.back().code == "UnknownModel" &&
                      api_chat.state().diagnostics.back().level == "info",
                  "Expected model selection is explained as a setup step in diagnostics");
            api_connection.model = "fixture-model";
            api_chat.open(api_connection);
            wait(api_chat);
            api_chat.send("use selected model");
            wait(api_chat);
            check(api_chat.state().phase == "ready" && api_probe->sends == 1,
                  "Selecting an API model connects and enables a real conversation turn");
        }
        // Render the actual panel without an OS renderer, including all CyUI themes.
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1100, 900};
        io.DeltaTime = 1.f / 60;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        cyui::apply();
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(ImTextureID(1));
        {
            ChatController failed_chat(registry);
            ChatConnection config;
            config.provider_id = "fixture";
            config.model = "fixture-model";
            config.provider = connection.provider;
            failed_chat.open(config);
            wait(failed_chat);
            cyui::DocumentServices services;
            bool diagnostic_image_requested = false;
            services.image = [&](const std::string&) {
                diagnostic_image_requested = true;
                return cyui::ImageData{};
            };
            ChatPanelOptions options;
            options.document_services = &services;
            ChatPanel panel(failed_chat, options);
            probe->reject_model = true;
            const auto sends_before = probe->sends.load();
            failed_chat.send("Refuse model before first token");
            wait(failed_chat);
            const auto failed_id = failed_chat.state().messages.back().id;
            check(failed_chat.state().messages.back().markdown.empty() &&
                      failed_chat.state().messages.back().error_message == model_refusal &&
                      failed_chat.state().messages.back().provider_code == "-32602" &&
                      failed_chat.state().provider_code == "-32602" &&
                      failed_chat.state().diagnostics.back().provider_code == "-32602" &&
                      !failed_chat.state().messages.back().interrupted,
                  "Immediate refusal retains the native RPC code on the state, row and diagnostic");
            probe->reject_model = false;
            failed_chat.refresh_models(); // clears the global error without generating
            wait(failed_chat);
            auto draw = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize({1100, 900});
                ImGui::Begin("Provider refusal", nullptr, ImGuiWindowFlags_NoSavedSettings);
                auto* font = ImGui::GetFontBaked();
                panel.draw();
                ImGui::End();
                ImGui::Render();
                return font;
            };
            draw();
            auto* font = draw();
            check(failed_chat.state().error_message.empty() && rendered_word(*font, "gpt-6.2-sol") &&
                      rendered_word(*font, "ChatGPT") && rendered_word(*font, "ProviderError") &&
                      rendered_word(*font, "-32602"),
                  "Transcript visibly renders the provider code, refused model and account explanation");
            check(probe->sends == sends_before + 1,
                  "Polling, rendering and refreshing after failure never regenerate");
            failed_chat.send("Explicit next turn");
            wait(failed_chat);
            check(probe->sends == sends_before + 2 && failed_chat.state().messages[1].id == failed_id &&
                      failed_chat.state().messages[1].error_message == model_refusal &&
                      failed_chat.state().messages.back().error_code.empty(),
                  "An explicit next turn leaves the refusal on its original assistant row");
            probe->fail = true;
            failed_chat.send("Return diagnostic containing Markdown");
            wait(failed_chat);
            probe->fail = false;
            for (int n = 0; n < 3; ++n)
                font = draw();
            check(!diagnostic_image_requested && rendered_word(*font, "private-diagnostic.png"),
                  "Provider diagnostic renders as literal text without activating image services");
        }
        {
            ChatController markdown_chat(registry);
            ChatConnection config;
            config.provider_id = "fixture";
            config.model = "fixture-model";
            markdown_chat.open(config);
            wait(markdown_chat);
            cyui::DocumentServices services;
            bool code_requested = false, preview_requested = false;
            ImVec2 thought_link{};
            std::string opened;
            services.image = [&](const std::string& target) {
                if (target == "thought.png")
                    return cyui::ImageData{ImTextureID(44), 24, 16};
                if (target == "table.png" || target == "final.png")
                    return cyui::ImageData{ImTextureID(target == "table.png" ? 41 : 42), 24, 16};
                code_requested |= target == "code.png";
                return cyui::ImageData{};
            };
            services.preview = [&](const std::string& target) {
                if (target == "https://fixture.invalid/thought") {
                    // The preceding item is the Markdown line for which CyUI asks for a card.
                    const auto begin = ImGui::GetItemRectMin();
                    thought_link = {begin.x + 5, begin.y + ImGui::GetFontSize() / 2};
                    return cyui::DocumentLinkPreview{};
                }
                preview_requested |= target == "https://fixture.invalid/cached";
                cyui::DocumentLinkPreview card;
                card.title = "Cached preview";
                card.image = {ImTextureID(43), 40, 12};
                return card;
            };
            ChatPanelOptions options;
            options.document_services = &services;
            options.open_link = [&](const std::string& target) { opened = target; };
            ChatPanel default_panel(markdown_chat, options);
            options.link_previews = true;
            ChatPanel preview_panel(markdown_chat, options);
            auto frame = [&](ChatPanel& panel, bool expand_activity = false) {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize({1100, 900});
                ImGui::Begin("Streamed Markdown", nullptr, ImGuiWindowFlags_NoSavedSettings);
                if (expand_activity) {
                    cyui::ScopedID panel_id("Chat");
                    ImGui::BeginChild("##chat-panel");
                    ImGui::GetStateStorage()->SetBool(
                        ImGui::GetID(cyui::trLabel("Activity").c_str()), true);
                    ImGui::EndChild();
                } else {
                    panel.draw();
                }
                ImGui::End();
                ImGui::Render();
            };
            probe->markdown_stream = true;
            markdown_chat.send("Render the sample");
            const auto until = Clock::now() + std::chrono::seconds(8);
            do {
                frame(default_panel);
                if (rendered_texture(41))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (Clock::now() < until);
            check(markdown_chat.state().busy && rendered_texture(41) && !rendered_texture(42),
                  "Streaming GFM table renders a reference image before the code fence closes");
            check(!preview_requested && !rendered_texture(43),
                  "A host preview cache is only consulted when enabled");
            frame(preview_panel);
            check(preview_requested && rendered_texture(43),
                  "An opted-in cached link card renders");
            probe->markdown_stream = false;
            wait(markdown_chat);
            frame(preview_panel);
            check(
                rendered_texture(41) && rendered_texture(42) && !code_requested,
                "Closing the fence renders the final reference image and preserves code literals");
            frame(preview_panel, true);
            frame(preview_panel);
            frame(preview_panel);
            check(rendered_texture(44),
                  "Expanded thinking resolves Markdown images through the host");
            io.AddMousePosEvent(thought_link.x, thought_link.y);
            frame(preview_panel);
            io.AddMouseButtonEvent(0, true);
            frame(preview_panel);
            io.AddMouseButtonEvent(0, false);
            frame(preview_panel);
            check(opened == "https://fixture.invalid/thought",
                  "Activating a thinking link uses the host's normal link callback");
        }
        {
            ChatController layout_chat(registry);
            probe->select_model = true;
            ChatConnection config;
            config.provider_id = "fixture";
            config.model = "fixture-model";
            layout_chat.open(config);
            wait(layout_chat);
            const auto gif_path = directory / "badge one.gif";
            {
                const char gif[] = "GIF89a\x01\0\x01\0\x80\0\0\0\0\0\xff\xff\xff"
                                   "!\xf9\x04\x01\0\0\0\0,\0\0\0\0\x01\0\x01\0\0\x02\x02" "D\x01\0;";
                std::ofstream(gif_path, std::ios::binary).write(gif, sizeof(gif) - 1);
            }
            const std::string posted =
                "Needle in the conversation [Open](./sample.txt)\n\n![Local][picture]\n\n"
                "[picture]: ./badge%20one.gif";
            layout_chat.send(posted, {gif_path, attachment});
            wait(layout_chat);
            auto posted_row = layout_chat.state().messages.front();
            const auto image_source = posted_row.attachments.front().source;
            check(find_chat_attachment(posted_row, "./badge%20one.gif") &&
                      !find_chat_attachment(posted_row, "../badge%20one.gif") &&
                      !find_chat_attachment(posted_row, "sub%2Fbadge%20one.gif"),
                  "Only portable filenames attached to this row resolve");
            posted_row.attachments.push_back(posted_row.attachments.front());
            check(!find_chat_attachment(posted_row, "badge%20one.gif"),
                  "Ambiguous attachment names never pick an arbitrary file");
            cyui::DocumentServices services;
            int posted_images = 0, draft_images = 0;
            services.image = [&](const std::string& source) {
                if (source == image_source) {
                    ++posted_images;
                    return cyui::ImageData{79, 16, 12};
                }
                return source == "draft.png" ? cyui::ImageData{77, 24, 16} : cyui::ImageData{};
            };
            ChatPanelOptions options;
            options.document_services = &services;
            std::uint64_t opened_file = 0;
            options.open_attachment = [&](const ChatAttachment& chosen, bool) { opened_file = chosen.id; };
            options.preview_attachment = [&](const auto& file) {
                if (file != gif_path)
                    return cyui::ImageData{};
                ++draft_images;
                return cyui::ImageData{78, 48, 32};
            };
            ChatPanel panel(layout_chat, options);
            ImVec2 window_size{720, 550};
            auto draw = [&] {
                posted_images = draft_images = 0;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize(window_size);
                ImGui::Begin("Chat controls", nullptr, ImGuiWindowFlags_NoSavedSettings);
                auto* font = ImGui::GetFontBaked();
                panel.draw();
                ImGui::End();
                ImGui::Render();
                return font;
            };
            auto click = [&](ImVec2 point) {
                io.AddMousePosEvent(point.x, point.y);
                draw();
                io.AddMouseButtonEvent(0, true);
                draw();
                io.AddMouseButtonEvent(0, false);
                draw();
            };
            const std::string draft = "![Draft](draft.png) ![Local](./badge%20one.gif)";
            panel.set_draft(draft);
            panel.add_attachments({gif_path});
            draw();
            auto* font = draw();
            ImVec2 model{}, status{}, menu{};
            const bool has_model = rendered_word(*font, "fixture-model", &model);
            const bool has_status = rendered_word(*font, "Ready", &status);
            const auto footer_description =
                "Provider/model and status sit together in the footer without the old toolbar. "
                "Model " + std::to_string(model.x) + ", " + std::to_string(model.y) +
                " and status " + std::to_string(status.x) + ", " + std::to_string(status.y);
            check(has_model && has_status && model.y > 475 &&
                      std::abs(model.y - status.y) < 15 && status.x > model.x && status.x - model.x < 220 &&
                      !rendered_word(*font, "Reconnect") && !rendered_word(*font, "Stop"),
                  footer_description.c_str());
            check(panel.pending_attachments().size() == 1 && rendered_texture(78) &&
                      !rendered_texture(77),
                  "Pending attachment is visible before submission");
            check(rendered_texture(79) && posted_images >= 2 &&
                      layout_chat.state().messages.front().markdown == posted,
                  "The inline reference and attachment card use the saved texture without rewriting the source");
            ImVec2 selection_start{}, selection_end{};
            check(rendered_word(*font, "Needle", &selection_start) &&
                      rendered_word(*font, "authoritative", &selection_end) &&
                      selection_end.y > selection_start.y,
                  "Both messages are visible for the selection test");
            auto& platform = ImGui::GetPlatformIO();
            const auto copy = platform.Platform_SetClipboardTextFn;
            auto* clipboard_data = platform.Platform_ClipboardUserData;
            std::string selected_text;
            platform.Platform_ClipboardUserData = &selected_text;
            platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* value) {
                *static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData) =
                    value;
            };
            io.AddMousePosEvent(selection_start.x - 1.f, selection_start.y);
            draw();
            io.AddMouseButtonEvent(0, true);
            draw();
            io.AddMousePosEvent(selection_end.x + 50.f, selection_end.y);
            draw();
            io.AddMouseButtonEvent(0, false);
            draw();
            const auto copy_modifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            io.AddKeyEvent(copy_modifier, true);
            io.AddKeyEvent(ImGuiKey_C, true);
            draw();
            io.AddKeyEvent(ImGuiKey_C, false);
            io.AddKeyEvent(copy_modifier, false);
            draw();
            platform.Platform_SetClipboardTextFn = copy;
            platform.Platform_ClipboardUserData = clipboard_data;
            check(selected_text.starts_with("Needle in the conversation ") &&
                      selected_text.find("authoritative") == std::string::npos,
                  "Dragging into the next message copies only text from the originating message");
            const auto sends_before_model_change = probe->sends.load();
            click(model);
            draw();
            font = draw();
            ImVec2 other_model{};
            check(rendered_word(*font, "second-model", &other_model),
                  "Clicking the current model opens the catalogue in the footer");
            click(other_model);
            wait(layout_chat);
            font = draw();
            check(layout_chat.state().model == "second-model" && probe->sends == sends_before_model_change,
                  "Choosing another model preserves the draft and sends nothing automatically");
            probe->select_model = false;
            ImVec2 open_file{};
            check(rendered_word(*font, "Open", &open_file), "The sibling file link is visible");
            click(open_file);
            check(opened_file == posted_row.attachments[1].id,
                  "Activating an inline relative link opens its own row's attachment");
            ImVec2 original{}, reply{};
            check(rendered_word(*font, "Needle", &original), "The original user row is visible");
            io.AddMousePosEvent(original.x, original.y);
            draw();
            io.AddMouseButtonEvent(1, true);
            draw();
            io.AddMouseButtonEvent(1, false);
            draw();
            check(rendered_word(*font, "Reply", &reply) && rendered_word(*font, "Copy"),
                  "The transcript row menu exposes native reply and copy actions");
            click(reply);
            draw();
            draw();
            check(rendered_word(*font, "Replying") && panel.draft() == draft,
                  "Replying marks the original row without overwriting the pending draft");
            io.AddKeyEvent(ImGuiKey_Escape, true);
            draw();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            draw();
            check(!rendered_word(*font, "Replying") && panel.draft() == draft,
                  "Escape cancels a reply and keeps its draft");
            check(rendered_word(*font, "...", &menu), "Chat options have a visible footer control");
            click(menu);
            draw();
            ImVec2 preview{};
            check(rendered_word(*font, "Preview", &preview) && rendered_word(*font, "Reconnect"),
                  "The compact menu exposes draft preview and connection commands");
            click(preview);
            draw();
            check(rendered_texture(77) && draft_images >= 2,
                  "The draft preview renders both a host image and its own pending attachment");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_F, true);
            draw();
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddKeyEvent(ImGuiKey_F, false);
            draw();
            draw();
            io.AddInputCharactersUTF8("nEeDle");
            draw();
            draw();
            check(rendered_word(*font, "result") && panel.draft() == draft,
                  "Ctrl+F locates a transcript row without modifying the composer");
            probe->silent = true;
            layout_chat.send("wait for stop");
            draw();
            ImVec2 stop{};
            check(rendered_word(*font, "Stop", &stop) && stop.x > 600 && stop.y < status.y &&
                      stop.y > status.y - 55,
                  "Stop appears at the send end of the composer");
            window_size = {410, 270};
            draw();
            draw();
            check(rendered_word(*font, "Stop", &stop) && stop.x < 410 && stop.y < 240 &&
                      rendered_word(*font, "Generating", &status) && status.y < 270 && status.y > stop.y,
                  "The composer, Stop and status remain visible in a compact panel");
            click(stop);
            wait(layout_chat);
            probe->silent = false;
            draw();
            check(layout_chat.state().phase == "interrupted" && !rendered_word(*font, "Stop"),
                  "The inline Stop control cancels the active turn");
            const auto sends_before_recall = probe->sends.load();
            panel.set_draft("");
            draw();
            draw();
            io.AddKeyEvent(ImGuiKey_UpArrow, true);
            draw();
            io.AddKeyEvent(ImGuiKey_UpArrow, false);
            draw();
            check(panel.draft() == "wait for stop" && probe->sends == sends_before_recall,
                  "Up recalls the last user message as an unsent draft in a native session");
            std::uint64_t edited = 0;
            options.edit_message = [&](std::uint64_t id, const std::string&) { edited = id; };
            options.delete_message = [](std::uint64_t) {};
            ChatPanel editable(layout_chat, options);
            auto draw_edit = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize({720, 550});
                ImGui::Begin("Editable conversation", nullptr, ImGuiWindowFlags_NoSavedSettings);
                editable.draw();
                ImGui::End();
                ImGui::Render();
            };
            editable.set_draft("");
            draw_edit();
            draw_edit();
            io.AddKeyEvent(ImGuiKey_UpArrow, true);
            draw_edit();
            io.AddKeyEvent(ImGuiKey_UpArrow, false);
            draw_edit();
            draw_edit();
            check(rendered_word(*font, "Cancel") && editable.draft().empty(),
                  "An editable host opens the native row editor on Up");
            io.AddKeyEvent(ImGuiKey_Enter, true);
            draw_edit();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            draw_edit();
            check(
                edited ==
                        layout_chat.state().messages[layout_chat.state().messages.size() - 2].id &&
                    probe->sends == sends_before_recall,
                "Saving the recalled edit calls the host for the original row");
        }
        {
            ChatController input_chat(registry);
            auto input_connection = connection;
            input_connection.history_id = "composer-test";
            input_connection.restore = false;
            input_chat.open(input_connection);
            wait(input_chat);
            ChatPanelOptions input_options;
#ifdef CYMCP_CHAT_TEST_WIN32_CLIPBOARD
            ChatClipboardFixture clipboard;
            clipboard.check_formats(directory);
            input_options.paste = [&](auto& text, auto& paths) { return clipboard.paste(text, paths); };
#endif
            ChatPanel input_panel(input_chat, input_options);
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowSize({1100, 900});
                ImGui::Begin("Native composer test", nullptr, ImGuiWindowFlags_NoSavedSettings);
                input_panel.draw();
                ImGui::End();
                ImGui::Render();
            };
            input_panel.set_draft("Native composer UTF-8: é 中文");
            frame();
            frame();
            frame();
            const auto before = probe->sends.load();
            io.AddKeyEvent(ImGuiMod_Shift, true);
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
            frame();
            check(probe->sends == before && input_panel.draft().find('\n') != std::string::npos,
                  "Native Shift+Enter inserts a line without submitting to the provider");
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            wait(input_chat);
            check(probe->sends == before + 1 && input_panel.draft().empty() &&
                      input_chat.state().messages.front().markdown.find("中文") !=
                          std::string::npos,
                  "Native Enter submits exactly once, preserves UTF-8 and clears the sent draft");
#ifdef CYMCP_CHAT_TEST_WIN32_CLIPBOARD
            input_panel.set_draft("");
            frame();
            frame();
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_V, true);
            frame();
            io.AddKeyEvent(ImGuiKey_V, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            check(clipboard.attempts == 1 && input_panel.pending_attachments().size() == 1 &&
                      input_panel.draft().empty() && probe->sends == before + 1,
                  "Ctrl+V stages a screenshot in the focused composer without sending it");
            const auto pasted = input_panel.pending_attachments().front();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            wait(input_chat);
            const auto& pasted_row =
                input_chat.state().messages[input_chat.state().messages.size() - 2];
            check(pasted_row.attachments.size() == 1 &&
                      pasted_row.attachments[0].mime_type == "image/png" &&
                      pasted_row.attachments[0].content && probe->sends == before + 2,
                  "Enter submits the pasted image once from the composer");
            const auto content = *pasted_row.attachments.front().content;
            clipboard.host.discard_unused(input_panel.pending_attachments());
            check(!std::filesystem::exists(pasted),
                  "Pasted image staging file is released after embedding");
            input_chat.save();
            wait(input_chat);
            input_connection.restore = true;
            input_chat.open(input_connection);
            wait(input_chat);
            const auto& restored =
                input_chat.state().messages[input_chat.state().messages.size() - 2];
            check(
                restored.attachments.size() == 1 && restored.attachments.front().content &&
                    *restored.attachments.front().content == content,
                "Pasted screenshot restores byte-for-byte without the original clipboard or file");
            clipboard.format = CF_UNICODETEXT;
            auto& platform = ImGui::GetPlatformIO();
            auto previous_text = platform.Platform_GetClipboardTextFn;
            platform.Platform_GetClipboardTextFn = [](ImGuiContext*) { return "**Markdown collé**"; };
            input_panel.set_draft("");
            frame();
            frame();
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_V, true);
            frame();
            io.AddKeyEvent(ImGuiKey_V, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            platform.Platform_GetClipboardTextFn = previous_text;
            check(input_panel.draft() == "**Markdown collé**" && input_panel.pending_attachments().empty(),
                  "Ordinary Unicode Markdown uses the native text paste once");
#endif
            input_panel.add_attachments({attachment});
            input_panel.set_draft("");
            frame();
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            wait(input_chat);
            check(input_chat.state()
                          .messages[input_chat.state().messages.size() - 2]
                          .attachments.size() == 1,
                  "Native composer submits an attachment-only message");
            probe->silent = true;
            input_chat.send("wait");
            input_panel.set_draft("Keep this draft while streaming");
            frame();
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            check(input_panel.draft() == "Keep this draft while streaming",
                  "Submitting during an active turn keeps the draft instead of dropping text");
            input_chat.cancel();
            wait(input_chat);
            probe->silent = false;
        }
        {
            ChatPanel panel(chat);
            panel.set_draft("A UTF-8 draft: é è 中文");
            panel.set_draft(std::string(1024 * 1024, 'x')); // large pastes are not truncated
            panel.set_draft("A UTF-8 draft: é è 中文");
            for (const auto& theme : cyui::builtInThemes()) {
                cyui::setTheme(theme.key.c_str());
                for (const char* locale : {"en", "fr"}) {
                    cyui::setLocale(locale);
                    ImGui::NewFrame();
                    ImGui::SetNextWindowSize({1100, 900});
                    ImGui::Begin("Chat test", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    panel.draw();
                    ImGui::End();
                    ImGui::Render();
                    check(ImGui::GetDrawData()->TotalVtxCount > 0,
                          "Panel renders in each theme and language");
                }
            }
        }
        ImGui::DestroyContext();
        {
            ChatController editor(registry);
            auto config = connection;
            config.history_id = "edited-chat";
            config.restore = false;
            editor.open(config);
            wait(editor);
            auto editable_file = directory / "editable-attachment.txt";
            {
                std::ofstream file(editable_file);
                file << "attachment";
            }
            editor.send("before editing", {editable_file});
            wait(editor);
            const auto first = editor.state().messages.front();
            check(editor.state().editable_history && first.user_turn == 0 &&
                      first.attachments.size() == 1,
                  "Committed host prompts can be edited, including prompts with attachments");
            probe->fail = true;
            editor.send("later failure");
            wait(editor);
            probe->fail = false;
            const auto failed_user = editor.state().messages[2];
            const auto failed_reply = editor.state().messages[3];
            const auto before_edits = probe->sends.load();
            editor.edit_message(first.id, "revised prompt");
            expect_error("Busy", [&] { editor.delete_turn(failed_user.id); });
            wait(editor);
            const auto checkpoint = store.load(config.history_id);
            const auto& edited = editor.state().messages.front();
            check(edited.id == first.id && edited.markdown == "revised prompt" &&
                      edited.attachments[0].id == first.attachments[0].id &&
                      edited.attachments[0].content == first.attachments[0].content &&
                      checkpoint.at("messages").dump().find("revised prompt") !=
                          std::string::npos &&
                      checkpoint.at("messages").dump().find("before editing") == std::string::npos,
                  "Editing preserves attachment identity and persists the revised content");
            expect_error("InvalidParameters", [&] { editor.delete_turn(failed_reply.id); });
            editor.delete_turn(first.id);
            wait(editor);
            check(editor.state().messages.size() == 2 &&
                      editor.state().messages.front().id == failed_user.id &&
                      editor.state().messages.front().user_turn == 0 &&
                      editor.state().messages.back().id == failed_reply.id &&
                      editor.state().messages.back().error_message == failed_reply.error_message,
                  "Deleting an earlier turn preserves subsequent row IDs and diagnostics");
            const auto after = store.load(config.history_id);
            check(after.at("failed_turns").size() == 1 &&
                      after.at("failed_turns")[0].at("after") == after.at("messages").size() &&
                      after.dump().find("revised prompt") == std::string::npos,
                  "Removing a turn reanchors subsequent failures in the saved checkpoint");
            config.restore = true;
            editor.open(config);
            wait(editor);
            check(editor.state().messages.size() == 2 &&
                      editor.state().messages.front().user_turn == 0 &&
                      editor.state().messages.back().error_code == failed_reply.error_code,
                  "Restored user rows remain editable after earlier deletions");
            editor.delete_turn(editor.state().messages.front().id);
            wait(editor);
            check(editor.state().messages.empty() &&
                      store.load(config.history_id).at("failed_turns").empty() &&
                      probe->sends == before_edits,
                  "Deleting the failed turn removes its response and never regenerates");
            check(std::any_of(editor.state().diagnostics.begin(), editor.state().diagnostics.end(),
                              [](const auto& row) { return row.code == "FixtureFailure"; }),
                  "Historical journal events survive deletion of the corresponding turn");

            Conversation restored(registry->create(config.provider_id, config.provider),
                                  config.model);
            auto with_tools = checkpoint;
            auto& messages = with_tools["messages"];
            const auto user = std::find_if(messages.begin(), messages.end(), [](const Json& row) {
                return row.at("role") == "user";
            });
            const auto insert_at = user - messages.begin() + 1;
            messages.insert(
                messages.begin() + insert_at,
                Json{{"role", "assistant"},
                     {"content", ""},
                     {"tool_calls",
                      Json::array({{{"id", "call-1"},
                                    {"type", "function"},
                                    {"function", {{"name", "fixture"}, {"arguments", "{}"}}}}})}});
            messages.insert(
                messages.begin() + insert_at + 1,
                Json{{"role", "tool"}, {"content", "tool output"}, {"tool_call_id", "call-1"}});
            with_tools["failed_turns"][0]["after"] = messages.size();
            restored.restore(with_tools);
            restored.edit_message(0, "");
            check(restored.history()[insert_at - 1].at("content").size() == 1 &&
                      restored.history()[insert_at - 1].at("content")[0] ==
                          *first.attachments[0].content,
                  "A prompt may keep only its attachment after editing");
            expect_error("InvalidContent", [&] { restored.edit_message(1, " \t"); });
            restored.delete_turn(1);
            check(restored.history().at(insert_at).at("tool_calls")[0].at("id") == "call-1" &&
                      restored.history().at(insert_at + 1).at("tool_call_id") == "call-1",
                  "Deleting another turn preserves complete tool call/result pairs");
            restored.delete_turn(0);
            check(
                restored.user_turn_count() == 0 &&
                    restored.history().dump().find("call-1") == std::string::npos,
                "Deleting a turn removes its tool calls and their corresponding results together");
        }
        // Destruction during a silent turn must cancel and join its owned worker.
        {
            ChatController closing(registry);
            connection.restore = false;
            closing.open(connection);
            wait(closing);
            probe->silent = true;
            closing.send("close");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        probe->silent = false;
        check(probe->stops > 0, "Owned provider disconnected on destruction");
        std::cout << checks << " chat UI checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
