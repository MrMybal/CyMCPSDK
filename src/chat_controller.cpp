#include <algorithm>
#include <cymcp/chat_ui.hpp>

namespace cymcp {
namespace {
// Display window only. The conversation itself is never cut or cancelled by these values: older
// rows leave the view and an oversized message is shown truncated.
constexpr std::size_t text_limit = 16 * 1024 * 1024, transcript_limit = 64 * 1024 * 1024,
                      row_limit = 4000;
const std::string display_cut = "\n\n*[...display truncated; the full text stays in the conversation]*";
std::string display_content(const Json& content) {
    if (content.is_string())
        return content.get<std::string>();
    std::string result;
    if (content.is_array())
        for (const auto& block : content) {
            const auto type = block.value("type", std::string());
            if (type == "text")
                result += block.value("text", std::string());
            else if (type == "image" || type == "audio")
                result += "\n\n[" + type + ": " + block.value("mimeType", std::string()) + "]\n";
            else if (type == "resource") {
                const auto& resource = block.at("resource");
                result +=
                    "\n\n[" + resource.value("name", resource.value("uri", std::string())) + "]\n";
            }
        }
    return result;
}
} // namespace
struct ChatController::Impl {
    struct Completion {
        std::string action, error, storage_error;
        Session session;
        ChatResult result;
        Json history = nullptr, models = nullptr;
        bool connected = false;
    };
    std::shared_ptr<ProviderRegistry> registry;
    ChatViewState view;
    ChatConnection connection;
    std::shared_ptr<Provider> provider;
    std::shared_ptr<Conversation> chat;
    std::unique_ptr<ConversationStore> store;
    // Producer-side coalescing: consecutive deltas merge, so a UI that stops polling (minimised
    // window) never makes a generation fail. Memory grows only with the streamed text itself.
    std::mutex events_mutex;
    std::deque<Event> events;
    void push_event(Event event) {
        std::lock_guard lock(events_mutex);
        const bool delta = event.type == "TextDelta" || event.type == "ThinkingDelta";
        if (delta && !events.empty() && events.back().type == event.type) {
            auto& text = events.back().data["text"].get_ref<std::string&>();
            text += event.data.at("text").get<std::string>();
            return;
        }
        if (!delta && events.size() >= 4096) {
            // Keep the newest activity; the view lists at most 128 rows anyway.
            for (auto it = events.begin(); it != events.end(); ++it)
                if (it->type != "TextDelta" && it->type != "ThinkingDelta") {
                    events.erase(it);
                    break;
                }
        }
        events.push_back(std::move(event));
    }
    std::vector<Event> drain_events() {
        std::lock_guard lock(events_mutex);
        std::vector<Event> batch(std::make_move_iterator(events.begin()),
                                 std::make_move_iterator(events.end()));
        events.clear();
        return batch;
    }
    Cancellation active;
    std::future<Completion> pending;
    std::uint64_t next_id = 1, assistant_id = 0;
    std::size_t displayed_bytes = 0;
    // Last member: joins before the worker's captured resources are destroyed.
    Executor worker{1, 2};

    explicit Impl(std::shared_ptr<ProviderRegistry> r)
        : registry(r ? std::move(r) : std::make_shared<ProviderRegistry>()) {}
    ~Impl() {
        active.cancel();
        // Cleanup is serialized after a possibly running operation.
        worker.post([this] {
            if (chat)
                chat->disconnect();
            else if (provider)
                provider->disconnect();
        });
    }
    void require_idle(bool connected = false) const {
        if (view.busy)
            throw Error("Busy", "Chat already has an active operation");
        if (connected && !view.connected)
            throw Error("InvalidState", "Connect the chat first");
    }
    void start(std::string action, std::function<void(Completion&, const CallContext&)> work) {
        require_idle();
        active = Cancellation{};
        CallContext context;
        context.cancellation = active;
        context.events = [this](const Event& event) {
            // Keep private native/configuration payloads out of the UI queue.
            if (event.type == "TextDelta" || event.type == "ThinkingDelta")
                push_event({event.type, {{"text", event.data.value("text", std::string())}}});
            else if (event.type == "ToolStarted" || event.type == "ToolCompleted") {
                auto name = event.data.value("name", std::string("tool"));
                if (name.size() > 256) {
                    auto end = std::size_t(256);
                    while (end && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
                        --end;
                    name = name.substr(0, end) + "...";
                }
                push_event({event.type, {{"name", std::move(name)}}});
            }
        };
        auto task = std::make_shared<std::packaged_task<Completion()>>(
            [this, action, context, work = std::move(work)] {
                Completion result;
                result.action = action;
                try {
                    if (action != "connecting")
                        context.cancellation.check();
                    work(result, context);
                } catch (const Error& error) {
                    result.error = error.code;
                } catch (...) {
                    result.error = "ProviderError";
                }
                if (chat) {
                    result.session = chat->session();
                    auto status = chat->status();
                    // needs_reconnect (failed restore/reconnect) is not a usable connection.
                    const auto phase = status.at("state").get<std::string>();
                    result.connected = phase != "disconnected" && phase != "needs_reconnect" &&
                                       phase != "connecting";
                    if (!result.error.empty())
                        result.result.metrics = status.at("metrics");
                }
                return result;
            });
        pending = task->get_future();
        worker.post([task] { (*task)(); });
        view.busy = true;
        view.phase = std::move(action);
        view.error_code.clear();
        view.storage_error.clear();
    }
    void persist(Completion& result) {
        if (!store || !chat)
            return;
        try {
            store->save(connection.history_id, *chat);
        } catch (const Error& error) {
            result.storage_error = error.code;
        } catch (...) {
            result.storage_error = "StorageError";
        }
    }
    ChatMessage* assistant() {
        for (auto it = view.messages.rbegin(); it != view.messages.rend(); ++it)
            if (it->id == assistant_id)
                return &*it;
        return nullptr;
    }
    // Drop the oldest rows from the view (never the live answer) to stay in the display window.
    void trim_view() {
        while ((displayed_bytes > transcript_limit || view.messages.size() > row_limit) &&
               view.messages.size() > 1 && view.messages.front().id != assistant_id) {
            displayed_bytes -= std::min(displayed_bytes, view.messages.front().markdown.size());
            view.messages.erase(view.messages.begin());
        }
    }
    void append(std::string& target, const std::string& text) {
        if (target.size() >= text_limit)
            return; // already marked as truncated for display
        if (text.size() > text_limit - target.size()) {
            auto keep = text_limit - target.size();
            while (keep && (static_cast<unsigned char>(text[keep]) & 0xc0) == 0x80)
                --keep;
            target += text.substr(0, keep) + display_cut;
            displayed_bytes += keep + display_cut.size();
        } else {
            target += text;
            displayed_bytes += text.size();
        }
        trim_view();
        ++view.revision;
    }
    void consume(const std::vector<Event>& batch) {
        for (const auto& event : batch) {
            if (event.type == "TextDelta") {
                if (auto* message = assistant())
                    append(message->markdown, event.data.at("text").get<std::string>());
            } else if (event.type == "ThinkingDelta")
                append(view.thinking, event.data.at("text").get<std::string>());
            else {
                if (view.activity.size() == 128)
                    view.activity.erase(view.activity.begin());
                view.activity.push_back({event.type, event.data.at("name").get<std::string>()});
            }
        }
    }
    void restore_rows(const Json& messages) {
        view.messages.clear();
        view.thinking.clear();
        displayed_bytes = 0;
        for (const auto& message : messages) {
            const auto role = message.at("role").get<std::string>();
            if (role != "user" && role != "assistant")
                continue;
            auto text = display_content(message.at("content"));
            if (text.size() > text_limit) {
                auto keep = text_limit;
                while (keep && (static_cast<unsigned char>(text[keep]) & 0xc0) == 0x80)
                    --keep;
                text = text.substr(0, keep) + display_cut;
            }
            displayed_bytes += text.size();
            view.messages.push_back({next_id++, role, std::move(text)});
        }
        trim_view();
        ++view.revision;
    }
};
ChatController::ChatController(std::shared_ptr<ProviderRegistry> registry)
    : impl_(std::make_unique<Impl>(std::move(registry))) {}
ChatController::~ChatController() = default;
const ChatViewState& ChatController::state() const noexcept {
    return impl_->view;
}
void ChatController::open(ChatConnection options) {
    auto& s = *impl_;
    s.require_idle();
    if (options.provider_id.empty() ||
        (!options.history_directory.empty() && options.history_id.empty()) ||
        (options.restore && options.history_directory.empty()))
        throw Error("InvalidOptions", "Provider and valid optional history configuration required");
    s.connection = std::move(options);
    auto revision = s.view.revision + 1;
    s.view = ChatViewState{};
    s.view.revision = revision;
    s.view.provider_id = s.connection.provider_id;
    s.view.model = s.connection.model;
    s.displayed_bytes = 0;
    s.assistant_id = 0;
    s.start("connecting", [&s](auto& result, const auto& context) {
        if (s.chat)
            s.chat->disconnect();
        s.chat.reset();
        s.provider.reset();
        s.store.reset();
        context.cancellation.check();
        s.provider = s.registry->create(s.connection.provider_id, s.connection.provider);
        s.chat = std::make_shared<Conversation>(s.provider, s.connection.model,
                                                s.connection.system_prompt);
        if (!s.connection.history_directory.empty())
            s.store = std::make_unique<ConversationStore>(s.connection.history_directory);
        if (s.connection.restore)
            s.chat->restore(s.store->load(s.connection.history_id), context);
        else
            s.chat->connect(s.connection.resume_id, context, s.connection.automatic_model,
                            s.connection.automatic_model ? &result.models : nullptr);
        result.history = s.chat->history();
    });
}
void ChatController::send(std::string text, std::vector<std::filesystem::path> attachments) {
    auto& s = *impl_;
    s.require_idle(true);
    if ((text.find_first_not_of(" \t\r\n") == std::string::npos && attachments.empty()) ||
        attachments.size() > 256)
        throw Error("InvalidContent", "Message must contain text or up to 256 attachments");
    std::string display = text;
    if (display.size() > text_limit)
        display = display.substr(0, text_limit) + display_cut;
    for (const auto& path : attachments)
        display += "\n\n[" + path_utf8(path.filename()) + "]";
    s.view.messages.push_back({s.next_id++, "user", std::move(display)});
    s.displayed_bytes += s.view.messages.back().markdown.size();
    s.assistant_id = s.next_id++;
    s.view.messages.push_back({s.assistant_id, "assistant", ""});
    s.displayed_bytes -= std::min(s.displayed_bytes, s.view.thinking.size());
    s.view.thinking.clear();
    ++s.view.revision;
    s.start("generating", [&s, text = std::move(text), attachments = std::move(attachments)](
                              auto& result, const auto& context) {
        Json content = text;
        if (!attachments.empty()) {
            content = Json::array({text_block(text)});
            for (const auto& path : attachments) {
                context.cancellation.check();
                content.push_back(attachment_from_file(path));
            }
            validate_content(content);
        }
        try {
            result.result = s.chat->send(std::move(content), context);
        } catch (...) {
            s.persist(result);
            throw;
        }
        s.persist(result);
    });
}
void ChatController::refresh_models() {
    auto& s = *impl_;
    s.require_idle(true);
    s.start("loading_models", [&s](auto& result, const auto& context) {
        result.models = s.provider->list_models(context);
    });
}
void ChatController::reconnect() {
    auto& s = *impl_;
    s.require_idle();
    if (s.view.session_id.empty())
        throw Error("InvalidState", "No session to reconnect");
    s.start("reconnecting", [&s](auto& result, const auto& context) {
        s.chat->reconnect(context);
        result.history = s.chat->history();
    });
}
void ChatController::save() {
    auto& s = *impl_;
    s.require_idle();
    if (s.view.session_id.empty() || s.connection.history_directory.empty())
        throw Error("InvalidState", "Configure history storage before saving");
    s.start("saving", [&s](auto& result, const auto&) { s.persist(result); });
}
void ChatController::disconnect() {
    auto& s = *impl_;
    s.require_idle();
    s.start("disconnecting", [&s](auto&, const auto&) {
        if (s.chat)
            s.chat->disconnect();
    });
}
void ChatController::cancel() noexcept {
    impl_->active.cancel();
}
void ChatController::poll() {
    auto& s = *impl_;
    // A bounded batch per frame; completion is applied after all queued deltas.
    s.consume(s.drain_events());
    if (!s.pending.valid() ||
        s.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    // A producer may have queued its final deltas after this frame's drain.
    s.consume(s.drain_events());
    auto result = s.pending.get();
    s.view.busy = false;
    s.view.connected = result.connected;
    s.view.session_id = result.session.id;
    if (!result.session.model.empty())
        s.view.model = result.session.model;
    s.view.storage_error = result.storage_error;
    if (!result.error.empty()) {
        s.view.metrics = std::move(result.result.metrics);
        if (s.view.error_code.empty())
            s.view.error_code = result.error;
        s.view.phase = result.error == "Cancelled" ? "interrupted" : "failed";
        if (auto* message = s.assistant(); message && result.action == "generating")
            message->interrupted = true;
    } else {
        s.view.phase = s.view.connected ? "ready" : "disconnected";
        if (result.action == "generating") {
            if (auto* message = s.assistant()) {
                // Authoritative final result replaces streamed/snapshot text.
                s.displayed_bytes -= message->markdown.size();
                message->markdown.clear();
                s.append(message->markdown, result.result.text);
            }
            s.view.metrics = result.result.metrics;
            s.view.usage = result.result.usage;
        }
        if (!result.history.is_null())
            s.restore_rows(result.history);
    }
    // A failed automatic connection may still return the catalogue to choose from.
    if (!result.models.is_null())
        s.view.models = std::move(result.models);
    s.assistant_id = 0;
    if (s.view.error_code == "ContextTooLarge")
        s.view.phase = "failed";
    ++s.view.revision;
}
} // namespace cymcp
