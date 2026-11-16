#include "test.hpp"
#include <cymcp/chat_ui.hpp>
#include <cyui/Core.h>
#include <fstream>
using namespace cymcp;
namespace {
struct Probe {
    std::atomic_int sends{0}, connects{0}, stops{0}, attachments{0};
    std::atomic_bool silent{false}, fail{false}, connecting{false}, flood{false}, oversized{false},
        large_thinking{false};
};
class ProviderFixture final : public Provider {
    std::shared_ptr<Probe> probe_;

  public:
    explicit ProviderFixture(std::shared_ptr<Probe> p) : probe_(std::move(p)) {}
    bool uses_host_history() const noexcept override {
        return true;
    }
    ProviderCapabilities capabilities() const override {
        return {true, true, true, true, true};
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
        return Json::array({{{"id", "fixture-model"}}});
    }
    Session start_session(std::string model, std::string, const CallContext& context,
                          std::string) override {
        context.cancellation.check();
        return {"fixture-id", model};
    }
    ChatResult send(Session&, Json& history, const CallContext& context) override {
        ++probe_->sends;
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
        if (probe_->fail)
            throw Error("FixtureFailure", "private-key-should-not-be-displayed");
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
        check(chat.state().messages.back().interrupted, "Partial row marked interrupted");
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
        check(chat.state().error_code == "FixtureFailure" &&
                  chat.state().messages.back().markdown == "**partial",
              "Failure retains partial text and normalized error only");
        check(chat.state().diagnostics.back().code == "FixtureFailure" &&
                  chat.state().diagnostics.back().level == "error",
              "Failed turn has a persistent error diagnostic");
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
        auto attachment = directory / "sample.txt";
        {
            std::ofstream file(attachment);
            file << "attachment";
        }
        chat.send("file", {attachment});
        wait(chat);
        check(probe->attachments == 1, "Attachment read and converted on the worker");
        auto user_row = chat.state().messages[chat.state().messages.size() - 2].markdown;
        check(user_row.find("sample.txt") != std::string::npos &&
                  user_row.find(path_utf8(directory)) == std::string::npos,
              "Display labels omit private attachment parent paths");
        chat.save();
        wait(chat);
        chat.disconnect();
        wait(chat);
        check(!chat.state().connected && chat.state().phase == "disconnected",
              "Disconnect keeps transcript and marks connection closed");
        connection.restore = true;
        chat.open(connection);
        wait(chat);
        check(chat.state().connected && !chat.state().messages.empty(),
              "Stored host history restored asynchronously");
        check(probe->sends == sends + 2, "Restore never resubmits messages");

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
