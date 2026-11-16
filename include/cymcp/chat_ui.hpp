#pragma once
#include "ai.hpp"
#include <cstdint>

namespace cyui {
struct DocumentServices;
}
namespace cymcp {
// Optional CyMCP::ChatUI module. The host owns the ImGui context and renderer.
struct ChatConnection {
    std::string provider_id, model, system_prompt, resume_id;
    ProviderConfig provider;
    std::filesystem::path history_directory;
    std::string history_id;
    bool restore = false;
    bool automatic_model = false;
};
struct ChatAttachment {
    std::uint64_t id = 0;
    std::string name, source, mime_type;
    std::uint64_t bytes = 0;
    // An immutable portable content block, loaded on the worker. No local path or URL fetching.
    std::shared_ptr<const Json> content;
};
struct ChatMessage {
    std::uint64_t id = 0;
    std::string role, markdown;
    bool interrupted = false;
    std::vector<ChatAttachment> attachments;
};
struct ChatActivity {
    std::string type, name;
};
struct ChatDiagnostic {
    std::uint64_t id = 0;
    std::int64_t timestamp_ms = 0;
    std::string level, provider_id, operation, code, message;
    double elapsed_ms = 0;
};
struct ChatViewState {
    std::string phase = "disconnected", provider_id, model, session_id, error_code, storage_error;
    bool busy = false, connected = false;
    std::uint64_t revision = 0;
    std::vector<ChatMessage> messages;
    std::vector<ChatActivity> activity;
    // Bounded, structured diagnostics. No prompts, responses, keys or native session IDs.
    std::deque<ChatDiagnostic> diagnostics;
    std::string thinking;
    Json models = Json::array(), metrics = Json::object(), usage = Json::object();
};
// Call methods and inspect state on one host/UI thread. All provider and file I/O
// runs on the owned worker. poll() transfers bounded events without waiting.
// Destruction cancels and joins the worker; destroy before unloading providers.
class ChatController {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit ChatController(std::shared_ptr<ProviderRegistry> = {});
    ~ChatController();
    ChatController(const ChatController&) = delete;
    ChatController& operator=(const ChatController&) = delete;
    void open(ChatConnection);
    void send(std::string text, std::vector<std::filesystem::path> attachments = {});
    void refresh_models();
    void reconnect();
    void save();
    void disconnect();
    void cancel() noexcept;
    void poll();
    const ChatViewState& state() const noexcept;
};
struct ChatPanelOptions {
    // Called on the UI thread, after explicit user activation. The host decides
    // whether/how to open links and choose files; the module never uses a shell.
    std::function<void(const std::string&)> open_link;
    std::function<std::vector<std::filesystem::path>()> pick_attachments;
    const cyui::DocumentServices* document_services = nullptr;
    std::function<void(const ChatAttachment&, bool save)> open_attachment;
    // Optional host clipboard import; return true when consumed. Native text paste works otherwise.
    std::function<bool(std::string&, std::vector<std::filesystem::path>&)> paste;
    // Both callbacks are required to enable native edit/delete actions on user rows.
    // The host owns provider-compatible mutations; the panel never rewrites a native session.
    std::function<void(std::uint64_t, const std::string&)> edit_message;
    std::function<void(std::uint64_t)> delete_message;
};
class ChatPanel {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit ChatPanel(ChatController&, ChatPanelOptions = {});
    ~ChatPanel();
    ChatPanel(const ChatPanel&) = delete;
    ChatPanel& operator=(const ChatPanel&) = delete;
    // Draw inside the host's current ImGui window, between NewFrame/Render.
    // Dimensions are screen pixels; zero fills the available region.
    void draw(const char* id = "Chat", float width = 0, float height = 0);
    void set_draft(std::string);
    const std::string& draft() const noexcept;
    void add_attachments(std::vector<std::filesystem::path>);
};
} // namespace cymcp
