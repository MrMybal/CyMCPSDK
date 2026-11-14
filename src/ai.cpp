#include <cymcp/ai.hpp>
#include <set>
#include <sstream>
namespace cymcp {
namespace {
class SseDecoder {
    std::string line_, data_;
    std::function<void(const std::string&)> event_;

  public:
    explicit SseDecoder(std::function<void(const std::string&)> cb) : event_(std::move(cb)) {}
    void feed(const char* bytes, std::size_t size) {
        for (std::size_t n = 0; n < size; ++n) {
            if (bytes[n] == '\n') {
                if (!line_.empty() && line_.back() == '\r')
                    line_.pop_back();
                if (line_.empty()) {
                    if (!data_.empty()) {
                        if (data_.back() == '\n')
                            data_.pop_back();
                        event_(data_);
                        data_.clear();
                    }
                } else if (line_.rfind("data:", 0) == 0) {
                    auto start = line_.size() > 5 && line_[5] == ' ' ? 6 : 5;
                    data_ += line_.substr(start) + "\n";
                }
                line_.clear();
            } else
                line_ += bytes[n];
            if (line_.size() > 1024 * 1024 || data_.size() > 1024 * 1024)
                throw Error("ResponseTooLarge", "SSE event exceeds limit");
        }
    }
    void finish() {
        if (!line_.empty() || !data_.empty())
            throw Error("UnexpectedEOF", "Truncated SSE event");
    }
};
HttpRequest model_request(const HttpChatOptions& options, const std::string& path) {
    HttpRequest request;
    request.url = options.base_url + path;
    request.timeout = options.timeout;
    request.headers = {{"Accept", "application/json"}, {"Content-Type", "application/json"}};
    if (!options.api_key.empty())
        request.headers["Authorization"] = "Bearer " + options.api_key;
    return request;
}
std::string latest_text(const Json& messages) {
    if (messages.empty() || !messages.back().contains("content") ||
        !messages.back()["content"].is_string())
        throw Error("Unsupported", "This connector currently accepts text input");
    return messages.back()["content"].get<std::string>();
}
} // namespace
HttpChatProvider::HttpChatProvider(HttpChatOptions opts)
    : Provider("openai-compatible"), options_(std::move(opts)) {
    while (!options_.base_url.empty() && options_.base_url.back() == '/')
        options_.base_url.pop_back();
    parse_url(options_.base_url);
    if (parse_url(options_.base_url).target.find('?') != std::string::npos ||
        options_.max_tool_rounds < 1 || options_.max_tool_rounds > 64 ||
        options_.timeout.count() <= 0 || options_.timeout > std::chrono::hours(24))
        throw Error("InvalidOptions", "Invalid tool round budget");
}
ProviderCapabilities HttpChatProvider::capabilities() const {
    return {true, false, static_cast<bool>(options_.tools), true, true, true, true, false, false};
}
void HttpChatProvider::connect(const CallContext& context) {
    context.cancellation.check();
    parse_url(options_.base_url);
}
Json HttpChatProvider::list_models(const CallContext& context) {
    auto response = HttpClient().perform(model_request(options_, "/models"), context.cancellation);
    if (response.status != 200)
        throw Error(response.status == 401 || response.status == 403 ? "AuthenticationRequired"
                    : response.status == 429                         ? "RateLimited"
                                                                     : "ProviderError",
                    "Model catalogue request failed");
    auto result = parse_json(response.body, 16 * 1024 * 1024);
    if (!result.contains("data") || !result["data"].is_array())
        throw Error("ProtocolError", "Missing model catalogue");
    return result["data"];
}
Session HttpChatProvider::start_session(std::string model, std::string resume,
                                        const CallContext& context, std::string) {
    context.cancellation.check();
    if (!resume.empty())
        throw Error(
            "Unsupported",
            "HTTP chat sessions are owned by the application; restore their messages explicitly");
    if (model.empty())
        throw Error("UnknownModel", "Select an exact model ID");
    return {random_token(16), std::move(model)};
}
ChatResult HttpChatProvider::send(Session& session, Json& messages, const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + options_.timeout);
    if (!messages.is_array() || messages.empty())
        throw Error("InvalidParameters", "Messages required");
    std::map<std::string, std::string> names;
    for (std::size_t round = 0; round < options_.max_tool_rounds; ++round) {
        context.cancellation.check();
        ChatResult result;
        Json calls = Json::array();
        bool terminal = false, done = false;
        auto request = model_request(options_, "/chat/completions");
        request.method = "POST";
        request.headers["Accept"] = "text/event-stream";
        auto wire_messages = messages;
        for (auto& message : wire_messages) {
            message.erase("_cymcp_native");
            message["content"] =
                detail::content_for(message.at("content"), detail::ContentFormat::ChatCompletions,
                                    message.at("role").get<std::string>());
        }
        Json payload = {{"model", session.model},
                        {"messages", wire_messages},
                        {"stream", true},
                        {"stream_options", {{"include_usage", true}}}};
        if (options_.tools) {
            Json tools = Json::array();
            for (const auto& tool : options_.tools->describe()) {
                auto name = tool.at("name").get<std::string>();
                auto wire = name;
                if (wire.size() > 64 ||
                    wire.find_first_not_of(
                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                        std::string::npos)
                    wire = "cy_" + sha256(name).substr(0, 32);
                auto found = names.find(wire);
                if (found != names.end() && found->second != name)
                    throw Error("InvalidOptions", "Tool wire-name collision");
                names[wire] = name;
                tools.push_back({{"type", "function"},
                                 {"function",
                                  {{"name", wire},
                                   {"description", tool["description"]},
                                   {"parameters", tool["inputSchema"]}}}});
            }
            if (!tools.empty())
                payload["tools"] = tools;
        }
        request.body = payload.dump();
        if (request.body.size() > 8 * 1024 * 1024)
            throw Error("ContextTooLarge", "Conversation exceeds request limit");
        SseDecoder decoder([&](const std::string& data) {
            if (done)
                throw Error("ProtocolError", "Data after terminal SSE marker");
            if (data == "[DONE]") {
                done = true;
                return;
            }
            auto event = parse_json(data);
            if (event.contains("error"))
                throw Error("ProviderError", "Provider returned an error");
            if (event.contains("usage") && !event["usage"].is_null()) {
                result.usage = event["usage"];
                context.emit("UsageUpdated", result.usage);
            }
            if (!event.contains("choices") || !event["choices"].is_array())
                throw Error("ProtocolError", "Missing chat choices");
            for (const auto& choice : event["choices"]) {
                if (choice.value("index", 0) != 0)
                    continue;
                if (terminal)
                    throw Error("ProtocolError", "Choice after its finish reason");
                auto delta = choice.value("delta", Json::object());
                if (delta.contains("content") && delta["content"].is_string()) {
                    auto text = delta["content"].get<std::string>();
                    result.text += text;
                    context.emit("TextDelta", {{"text", text}});
                }
                if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string())
                    context.emit("ThinkingDelta", {{"text", delta["reasoning_content"]}});
                if (delta.contains("tool_calls"))
                    for (const auto& part : delta["tool_calls"]) {
                        auto index = part.at("index").get<int>();
                        if (index < 0 || index >= 32)
                            throw Error("ProtocolError", "Invalid tool call index");
                        while (calls.size() <= static_cast<std::size_t>(index))
                            calls.push_back({{"id", ""},
                                             {"type", "function"},
                                             {"function", {{"name", ""}, {"arguments", ""}}}});
                        auto& call = calls[index];
                        if (part.contains("id"))
                            call["id"] = part["id"];
                        if (part.contains("function")) {
                            auto fn = part["function"];
                            for (auto key : {"name", "arguments"})
                                if (fn.contains(key))
                                    call["function"][key] =
                                        call["function"][key].get<std::string>() +
                                        fn[key].get<std::string>();
                        }
                    }
                if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                    result.finish_reason = choice["finish_reason"].get<std::string>();
                    terminal = true;
                }
            }
        });
        std::exception_ptr failure;
        auto response = HttpClient().perform(request, context.cancellation,
                                             [&](const char* data, std::size_t size) {
                                                 if (!failure)
                                                     try {
                                                         decoder.feed(data, size);
                                                     } catch (...) {
                                                         failure = std::current_exception();
                                                     }
                                                 return true;
                                             });
        if (response.status < 200 || response.status >= 300)
            throw Error(response.status == 401 || response.status == 403 ? "AuthenticationRequired"
                        : response.status == 429                         ? "RateLimited"
                                                                         : "ProviderError",
                        "Chat request rejected with HTTP " + std::to_string(response.status));
        if (failure)
            std::rethrow_exception(failure);
        decoder.finish();
        if (!terminal || !done)
            throw Error("UnexpectedEOF", "Provider stream ended before completion");
        Json assistant = {{"role", "assistant"}, {"content", result.text}};
        if (calls.empty()) {
            if (result.finish_reason == "tool_calls")
                throw Error("ProtocolError", "Missing tool calls");
            messages.push_back(std::move(assistant));
            context.emit("TurnCompleted",
                         {{"text", result.text}, {"finishReason", result.finish_reason}});
            return result;
        }
        if (!options_.tools || result.finish_reason != "tool_calls")
            throw Error("ProtocolError", "Unexpected tool call result");
        std::set<std::string> ids;
        for (const auto& call : calls) {
            auto id = call.at("id").get<std::string>();
            if (id.empty() || !ids.insert(id).second)
                throw Error("ProtocolError", "Missing or duplicate tool call ID");
        }
        assistant["tool_calls"] = calls;
        messages.push_back(assistant);
        for (const auto& call : calls) {
            auto name = call["function"]["name"].get<std::string>();
            auto id = call["id"].get<std::string>();
            if (id.empty())
                throw Error("ProtocolError", "Missing tool call ID");
            context.emit("ToolStarted", {{"id", id}, {"name", name}});
            Json output;
            try {
                auto args = parse_json(call["function"]["arguments"].get<std::string>());
                auto found = names.find(name);
                if (found == names.end())
                    throw Error("UnknownOperation", "Unknown API tool");
                output = options_.tools->invoke(found->second, args, context);
            } catch (const Error& e) {
                if (e.code == "Cancelled" || e.code == "Timeout")
                    throw;
                output = {{"error", {{"code", e.code}, {"message", e.what()}}}};
            } catch (const Json::exception&) {
                output = {{"error", {{"code", "InvalidArguments"}}}};
            }
            messages.push_back(
                {{"role", "tool"}, {"tool_call_id", id}, {"content", output.dump()}});
            context.emit("ToolCompleted", {{"id", id}, {"name", name}, {"output", output}});
        }
    }
    throw Error("ToolBudgetExceeded", "Maximum tool rounds reached");
}
struct Conversation::State {
    std::shared_ptr<Provider> provider;
    std::string model;
    Session session;
    bool connected = false;
    std::string phase = "disconnected", last_error;
    Json metrics = Json::object();
    std::atomic_bool reserved{false};
    std::atomic_bool disconnect_requested{false};
    Json messages = Json::array();
    std::mutex turn, history, cancellation_mutex;
    Cancellation active;
    State(std::shared_ptr<Provider> p, std::string m)
        : provider(std::move(p)), model(std::move(m)) {}
};
Conversation::Conversation(std::shared_ptr<Provider> provider, std::string model,
                           std::string system)
    : state_(std::make_shared<State>(std::move(provider), std::move(model))) {
    if (!state_->provider)
        throw Error("InvalidOptions", "Provider required");
    if (!system.empty())
        state_->messages.push_back({{"role", "system"}, {"content", std::move(system)}});
}
void Conversation::connect(std::string resume, const CallContext& input) {
    auto& s = *state_;
    auto context = reserve(input);
    struct Release {
        State& s;
        ~Release() {
            s.reserved.store(false);
        }
    } release{s};
    std::unique_lock turn(s.turn, std::try_to_lock);
    if (!turn.owns_lock())
        throw Error("Busy", "Conversation busy");
    context.cancellation.check();
    s.disconnect_requested.store(false);
    {
        std::lock_guard history(s.history);
        s.connected = false;
        s.phase = "connecting";
    }
    auto started = Clock::now();
    try {
        s.provider->connect(context);
        std::string system;
        {
            std::lock_guard history(s.history);
            for (const auto& message : s.messages)
                if (message.value("role", std::string()) == "system" &&
                    message["content"].is_string())
                    system += message["content"].get<std::string>() + "\n";
        }
        auto session = s.provider->start_session(s.model, std::move(resume), context, system);
        std::lock_guard lock(s.history);
        context.cancellation.check();
        if (s.disconnect_requested.load())
            throw Error("Cancelled", "Conversation was disconnected during connection");
        s.session = std::move(session);
        s.connected = true;
        s.phase = "ready";
        s.last_error.clear();
        s.metrics["connect_ms"] =
            std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    } catch (...) {
        s.provider->disconnect();
        std::lock_guard history(s.history);
        s.phase = "disconnected";
        throw;
    }
}
CallContext Conversation::reserve(const CallContext& original) {
    if (state_->reserved.exchange(true))
        throw Error("Busy", "Conversation already has a scheduled or active turn");
    CallContext context = original;
    context.cancellation = original.cancellation.child();
    {
        std::lock_guard lock(state_->cancellation_mutex);
        state_->active = context.cancellation;
    }
    return context;
}
ChatResult Conversation::send(Json content, const CallContext& original) {
    return send_reserved(std::move(content), reserve(original));
}
ChatResult Conversation::send_reserved(Json content, const CallContext& context) {
    auto& s = *state_;
    struct Release {
        State& state;
        ~Release() {
            state.reserved.store(false);
        }
    } release{s};
    std::unique_lock turn(s.turn, std::try_to_lock);
    if (!turn.owns_lock())
        throw Error("Busy", "Conversation busy");
    context.cancellation.check();
    Json messages;
    Session session;
    {
        std::lock_guard lock(s.history);
        if (!s.connected)
            throw Error("InvalidState", "Connect conversation first");
        messages = s.messages;
        session = s.session;
    }
    validate_content(content);
    messages.push_back({{"role", "user"}, {"content", std::move(content)}});
    if (messages.dump().size() > 8 * 1024 * 1024)
        throw Error("ContextTooLarge", "Conversation exceeds memory budget");
    auto commit = [&] {
        std::lock_guard lock(s.history);
        s.messages = messages;
        s.session = session;
    };
    const auto started = Clock::now();
    double first_token = -1;
    std::size_t event_count = 0;
    CallContext measured = context;
    measured.events = [&](const Event& event) {
        ++event_count;
        if (first_token < 0 && event.type == "TextDelta" &&
            !event.data.value("text", std::string()).empty())
            first_token = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        context.emit(event.type, event.data);
    };
    auto metrics = [&] {
        return Json{
            {"first_token_ms", first_token < 0 ? Json() : Json(first_token)},
            {"total_ms", std::chrono::duration<double, std::milli>(Clock::now() - started).count()},
            {"events", event_count}};
    };
    {
        std::lock_guard lock(s.history);
        s.phase = "busy";
        s.last_error.clear();
    }
    try {
        measured.emit("TurnStarted", {{"sessionId", session.id}});
        auto result = s.provider->send(session, messages, measured);
        context.cancellation.check();
        commit();
        result.metrics = metrics();
        {
            std::lock_guard lock(s.history);
            s.metrics = result.metrics;
            s.phase = s.disconnect_requested.load() ? "disconnected" : "ready";
        }
        context.emit("TurnMetrics", result.metrics);
        return result;
    } catch (const Error& e) {
        commit();
        {
            std::lock_guard lock(s.history);
            s.metrics = metrics();
            s.last_error = e.code;
            s.phase = s.disconnect_requested.load() ? "disconnected"
                      : e.code == "Cancelled"       ? "interrupted"
                                                    : "failed";
        }
        if (context.events)
            context.events(
                {e.code == "Cancelled" ? "TurnCancelled" : "TurnFailed", {{"code", e.code}}});
        throw;
    } catch (...) {
        commit();
        {
            std::lock_guard lock(s.history);
            s.metrics = metrics();
            s.last_error = "ProviderError";
            s.phase = s.disconnect_requested.load() ? "disconnected" : "failed";
        }
        if (context.events)
            context.events({"TurnFailed", {{"code", "ProviderError"}}});
        throw;
    }
}
std::future<ChatResult> Conversation::send_async(Executor& executor, Json content,
                                                 CallContext context) {
    context = reserve(context);
    try {
        auto self = *this;
        auto task = std::make_shared<std::packaged_task<ChatResult()>>(
            [self, content = std::move(content), context = std::move(context)]() mutable {
                return self.send_reserved(std::move(content), context);
            });
        auto result = task->get_future();
        executor.post([task] { (*task)(); });
        return result;
    } catch (...) {
        state_->reserved.store(false);
        throw;
    }
}
void Conversation::cancel() noexcept {
    std::lock_guard lock(state_->cancellation_mutex);
    state_->active.cancel();
}
Json Conversation::history() const {
    std::lock_guard lock(state_->history);
    return state_->messages;
}
Session Conversation::session() const {
    std::lock_guard lock(state_->history);
    return state_->session;
}
void Conversation::disconnect() noexcept {
    state_->disconnect_requested.store(true);
    cancel();
    // Provider disconnect is cancellation-aware, including silent subprocesses.
    state_->provider->disconnect();
    std::lock_guard lock(state_->history);
    state_->connected = false;
    state_->phase = "disconnected";
}
Json Conversation::status() const {
    std::lock_guard lock(state_->history);
    return {
        {"state", state_->phase}, {"last_error", state_->last_error}, {"metrics", state_->metrics}};
}
Json Conversation::checkpoint() const {
    auto& s = *state_;
    std::lock_guard lock(s.history);
    if (s.reserved.load())
        throw Error("Busy", "Cannot snapshot a scheduled or active turn");
    return {{"format", "cymcp-conversation"},
            {"version", 1},
            {"provider", s.provider->identity()},
            {"history_mode", s.provider->uses_host_history() ? "host" : "native"},
            {"session", {{"id", s.session.id}, {"model", s.session.model}}},
            {"model", s.model},
            {"messages", s.messages},
            {"last_error", s.last_error},
            {"metrics", s.metrics}};
}
void Conversation::restore(const Json& snapshot, const CallContext& input) {
    auto context = reserve(input);
    auto& s = *state_;
    struct Release {
        State& s;
        ~Release() {
            s.reserved.store(false);
        }
    } release{s};
    std::unique_lock turn(s.turn, std::try_to_lock);
    if (!turn.owns_lock())
        throw Error("Busy", "Conversation busy");
    Json messages;
    std::string id, model, native_model, system, previous_error;
    try {
        if (snapshot.dump().size() > 16 * 1024 * 1024)
            throw Error("ContextTooLarge", "Checkpoint exceeds limit");
        if (snapshot.at("format") != "cymcp-conversation" || snapshot.at("version") != 1)
            throw Error("UnsupportedVersion", "Unsupported conversation checkpoint");
        if (snapshot.at("provider") != s.provider->identity() ||
            snapshot.at("history_mode") != (s.provider->uses_host_history() ? "host" : "native"))
            throw Error("SessionInvalid", "Checkpoint belongs to a different provider/transport");
        messages = snapshot.at("messages");
        if (snapshot.contains("metrics") && !snapshot.at("metrics").is_object())
            throw Error("InvalidCheckpoint", "Invalid checkpoint metrics");
        if (!messages.is_array() || messages.size() > 100000)
            throw Error("InvalidCheckpoint", "Invalid message history");
        for (const auto& message : messages) {
            auto role = message.at("role").get<std::string>();
            if (role != "system" && role != "user" && role != "assistant" && role != "tool")
                throw Error("InvalidCheckpoint", "Invalid message role");
            validate_content(message.at("content"));
            if (role == "system")
                system += detail::content_text(message.at("content")) + "\n";
            if (role == "tool")
                message.at("tool_call_id").get<std::string>();
        }
        model = snapshot.at("model").get<std::string>();
        native_model = snapshot.at("session").at("model").get<std::string>();
        id = snapshot.at("session").at("id").get<std::string>();
        previous_error = snapshot.value("last_error", std::string());
        if (!s.model.empty() && s.model != model)
            throw Error("SessionInvalid", "Checkpoint model differs from requested model");
        if (!s.provider->uses_host_history() && id.empty())
            throw Error("SessionInvalid", "Native checkpoint has no exact session ID");
    } catch (const Json::exception&) {
        throw Error("InvalidCheckpoint", "Invalid conversation checkpoint");
    }
    s.disconnect_requested.store(false);
    {
        std::lock_guard history(s.history);
        s.connected = false;
        s.phase = "connecting";
    }
    s.provider->disconnect();
    try {
        context.cancellation.check();
        s.provider->connect(context);
        if (!s.provider->uses_host_history() && !s.provider->capabilities().resume)
            throw Error("Unsupported", "Provider cannot reload this exact native session");
        auto session =
            s.provider->start_session(native_model.empty() ? model : native_model,
                                      s.provider->uses_host_history() ? "" : id, context, system);
        if (!s.provider->uses_host_history() && session.id != id)
            throw Error("SessionInvalid", "Provider replaced the native session");
        if (s.provider->uses_host_history())
            session.id = id.empty() ? session.id : id;
        {
            std::lock_guard history(s.history);
            context.cancellation.check();
            if (s.disconnect_requested.load())
                throw Error("Cancelled", "Conversation was disconnected during restore");
            s.model = model;
            s.session = std::move(session);
            s.messages = std::move(messages);
            s.metrics = snapshot.value("metrics", Json::object());
            s.connected = true;
            s.phase = "ready";
            s.last_error = previous_error;
        }
        context.emit("ConversationRestored",
                     {{"sessionId", s.session.id}, {"previous_error", previous_error}});
    } catch (...) {
        s.provider->disconnect();
        std::lock_guard history(s.history);
        s.connected = false;
        s.phase = "needs_reconnect";
        throw;
    }
}
void Conversation::reconnect(const CallContext& context) {
    restore(checkpoint(), context);
}
} // namespace cymcp
