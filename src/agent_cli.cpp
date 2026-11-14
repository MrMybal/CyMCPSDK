#include <cymcp/ai.hpp>
#include <sstream>
namespace cymcp {
namespace {
std::string probe(ProcessOptions config, const std::vector<std::string>& args,
                  const CallContext& context) {
    config.arguments.insert(config.arguments.end(), args.begin(), args.end());
    Process process(config);
    process.close_input();
    std::string output;
    ProcessLine line;
    while (process.next(line, context.cancellation)) {
        if (line.stderr_stream)
            continue;
        if (output.size() + line.text.size() + 1 > 1024 * 1024)
            throw Error("ResponseTooLarge", "CLI discovery exceeds limit");
        output += line.text + "\n";
    }
    if (process.wait(context.cancellation) != 0)
        throw Error("ConnectionFailed",
                    "CLI discovery failed; check the native installation/login");
    return output;
}
void native_id(Session& session, const Json& value, const char* key) {
    if (!value.contains(key))
        return;
    auto id = value.at(key).get<std::string>();
    if (id.empty() || (!session.id.empty() && id != session.id))
        throw Error("SessionInvalid", "CLI returned a different or empty session ID");
    session.id = std::move(id);
}
} // namespace
AgentCliProvider::AgentCliProvider(AgentCliOptions options) : options_(std::move(options)) {
    if (!options_.native_settings.is_object() || options_.connect_timeout.count() <= 0 ||
        options_.turn_timeout.count() <= 0 || options_.connect_timeout > std::chrono::hours(24) ||
        options_.turn_timeout > std::chrono::hours(24))
        throw Error("InvalidOptions", "Invalid CLI options");
    if (options_.protocol != AgentCliProtocol::CodeBuddy &&
        (!options_.mcp_config_file.empty() || !options_.allowed_tools.empty()))
        throw Error("Unsupported", "These tool configuration fields belong to CodeBuddy CLI");
    if (options_.protocol == AgentCliProtocol::CommandCode && !options_.native_settings.empty())
        throw Error("Unsupported",
                    "Use an explicit Command Code argument prefix for native configuration");
}
ProviderCapabilities AgentCliProvider::capabilities() const {
    bool buddy = options_.protocol == AgentCliProtocol::CodeBuddy;
    return {true, true,
            !buddy || options_.allow_all_actions || !options_.mcp_config_file.empty() ||
                !options_.allowed_tools.empty(),
            !buddy, false};
}
CallContext AgentCliProvider::begin(const CallContext& input, std::chrono::milliseconds timeout) {
    auto context = input;
    context.cancellation = input.cancellation.child();
    context.cancellation.deadline = std::min(context.cancellation.deadline, Clock::now() + timeout);
    std::lock_guard lock(cancellation_mutex_);
    active_ = context.cancellation;
    return context;
}
void AgentCliProvider::connect(const CallContext& input) {
    std::lock_guard lock(mutex_);
    connected_ = false;
    auto context = begin(input, options_.connect_timeout);
    auto native_version = probe(options_.process, {"--version"}, context);
    if (options_.protocol == AgentCliProtocol::OpenCode) {
        auto start = native_version.find_first_of("0123456789");
        if (start == std::string::npos || native_version.compare(start, 2, "1.") != 0)
            throw Error("UnsupportedVersion", "This OpenCode adapter targets CLI 1.x JSONL");
    }
    connected_ = true;
    context.emit("ProviderReady", {{"authentication", "unverified"}});
}
Json AgentCliProvider::list_models(const CallContext& input) {
    std::lock_guard lock(mutex_);
    auto context = begin(input, options_.connect_timeout);
    if (options_.protocol == AgentCliProtocol::CodeBuddy)
        throw Error("Unsupported", "CodeBuddy CLI model selection uses an explicit model ID");
    auto output = probe(options_.process,
                        options_.protocol == AgentCliProtocol::CommandCode
                            ? std::vector<std::string>{"--list-models"}
                            : std::vector<std::string>{"models"},
                        context);
    Json rows = Json::array();
    if (options_.protocol == AgentCliProtocol::Antigravity ||
        options_.protocol == AgentCliProtocol::CommandCode) {
        auto first = output.find_first_not_of(" \r\n\t");
        if (first != std::string::npos && (output[first] == '[' || output[first] == '{')) {
            auto data = parse_json(output);
            rows = data.is_array() ? data : data.value("models", data.value("data", Json::array()));
            if (!rows.is_array())
                throw Error("ProtocolError", "Invalid CLI catalogue");
            for (auto& row : rows)
                if (row.is_string())
                    row = {{"id", row.get<std::string>()}};
        } else if (options_.protocol == AgentCliProtocol::Antigravity) {
            std::istringstream stream(output);
            for (std::string line; std::getline(stream, line);) {
                auto tab = line.find('\t');
                if (tab != std::string::npos && tab > 0 && line.substr(0, tab) != "ID" &&
                    line.substr(0, tab) != "MODEL")
                    rows.push_back({{"id", line.substr(0, tab)}, {"name", line.substr(tab + 1)}});
            }
        } else
            throw Error("Unsupported",
                        "Command Code returned a text catalogue; supply an exact model ID");
    } else {
        std::istringstream stream(output);
        for (std::string line; std::getline(stream, line);)
            if (!line.empty() && line.find('/') != std::string::npos &&
                line.find_first_of(" \t\r\x1b") == std::string::npos)
                rows.push_back({{"id", line}});
    }
    if (rows.empty())
        throw Error("ModelDiscoveryFailed", "No model IDs found in CLI catalogue");
    for (const auto& row : rows)
        if (!row.is_object() || !row.contains("id") || !row["id"].is_string() ||
            row["id"].get<std::string>().empty())
            throw Error("ProtocolError", "Model catalogue contains an invalid ID");
    return rows;
}
Session AgentCliProvider::start_session(std::string model, std::string resume,
                                        const CallContext& context, std::string) {
    context.cancellation.check();
    std::lock_guard lock(mutex_);
    if (!connected_)
        throw Error("InvalidState", "Connect CLI before opening a session");
    return {std::move(resume), std::move(model)};
}
ChatResult AgentCliProvider::send(Session& session, Json& messages, const CallContext& input) {
    std::lock_guard lock(mutex_);
    if (!connected_)
        throw Error("InvalidState", "Connect CLI first");
    auto context = begin(input, options_.turn_timeout);
    if (!messages.is_array() || messages.empty() || !messages.back().at("content").is_string())
        throw Error("Unsupported", "This CLI adapter accepts text messages");
    auto prompt = messages.back().at("content").get<std::string>();
    // These CLIs have no portable system-role flag. Supply first-turn context as text.
    if (session.id.empty()) {
        std::string instructions;
        for (const auto& message : messages)
            if (message.value("role", std::string()) == "system") {
                if (!message.at("content").is_string())
                    throw Error("Unsupported", "System context must be text");
                instructions += message.at("content").get<std::string>() + "\n";
            }
        if (!instructions.empty())
            prompt = "Application instructions:\n" + instructions + "\nUser message:\n" + prompt;
    }
    auto config = options_.process;
    auto& args = config.arguments;
    bool agy = options_.protocol == AgentCliProtocol::Antigravity;
    bool command = options_.protocol == AgentCliProtocol::CommandCode;
    bool buddy = options_.protocol == AgentCliProtocol::CodeBuddy;
    if (agy) {
        args.insert(args.end(), {"--input-format", "stream-json", "--output-format", "stream-json",
                                 "--disable-slash-commands"});
        if (options_.allow_all_actions)
            args.push_back("--dangerously-skip-permissions");
        else
            args.insert(args.end(), {"--mode", "plan", "--sandbox"});
        if (!options_.native_settings.empty())
            args.insert(args.end(), {"--settings", options_.native_settings.dump()});
        if (!session.id.empty())
            args.insert(args.end(), {"--conversation", session.id});
    } else if (command) {
        args.insert(args.end(), {"-p", "--output-format", "json", "--skip-onboarding"});
        args.push_back(options_.allow_all_actions ? "--yolo" : "--plan");
        if (!options_.native_settings.empty())
            throw Error("Unsupported",
                        "Use an explicit Command Code argument prefix for native configuration");
        if (!session.id.empty())
            args.insert(args.end(), {"--resume", session.id});
    } else if (buddy) {
        args.insert(args.end(),
                    {"-p", "--input-format", "stream-json", "--output-format", "stream-json",
                     "--verbose", "--strict-mcp-config", "--setting-sources", ""});
        config.environment["CODEBUDDY_CODE_DISABLE_BACKGROUND_TASKS"] = "1";
        if (!options_.mcp_config_file.empty())
            args.insert(
                args.end(),
                {"--mcp-config", path_utf8(std::filesystem::absolute(options_.mcp_config_file))});
        if (!options_.native_settings.empty())
            args.insert(args.end(), {"--settings", options_.native_settings.dump()});
        if (options_.allow_all_actions)
            args.insert(args.end(), {"--permission-mode", "bypassPermissions",
                                     "--dangerously-skip-permissions"});
        else {
            args.insert(args.end(), {"--permission-mode", "dontAsk", "--tools"});
            std::string tools;
            for (const auto& name : options_.allowed_tools) {
                if (!tools.empty())
                    tools += ",";
                tools += name;
            }
            args.push_back(tools);
        }
        if (!session.id.empty())
            args.insert(args.end(), {"--resume", session.id});
    } else {
        args.insert(args.end(), {"run", "--format", "json"});
        if (!session.id.empty())
            args.insert(args.end(), {"--session", session.id});
        auto settings = options_.native_settings;
        settings["permission"] = {{"*", options_.allow_all_actions ? "allow" : "deny"}};
        config.environment["OPENCODE_CONFIG_CONTENT"] = settings.dump();
    }
    if (!session.model.empty())
        args.insert(args.end(), {"--model", session.model});
    if (!agy && !command && !buddy)
        args.insert(args.end(), {"--", prompt});
    Process process(config);
    if (agy)
        process.write(Json({{"event", "user"}, {"message", {{"content", prompt}}}}).dump() + "\n",
                      context.cancellation);
    else if (buddy)
        process.write(Json({{"type", "user"},
                            {"message",
                             {{"role", "user"},
                              {"content", Json::array({{{"type", "text"}, {"text", prompt}}})}}}})
                              .dump() +
                          "\n",
                      context.cancellation);
    else if (command)
        process.write(prompt, context.cancellation);
    process.close_input();
    ChatResult result;
    bool completed = false;
    std::map<std::string, std::string> snapshots;
    std::size_t snapshot_bytes = 0;
    ProcessLine line;
    while (process.next(line, context.cancellation)) {
        if (line.stderr_stream || line.text.empty())
            continue;
        auto data = parse_json(line.text);
        if (!data.is_object())
            throw Error("ProtocolError", "Expected CLI event object");
        auto kind = data.value(agy ? "event" : "type", std::string());
        auto payload = agy ? data.value(kind, Json::object()) : data;
        if (payload.is_object())
            native_id(session, payload,
                      agy       ? "conversation_id"
                      : command ? "sessionId"
                      : buddy   ? "session_id"
                                : "sessionID");
        if (agy)
            native_id(session, data, "conversation_id");
        if (kind == "error" || kind == "turn.failed" || data.contains("error"))
            throw Error("ProviderError", "CLI reported a provider error");
        if (completed)
            throw Error("ProtocolError", "CLI emitted events after its terminal result");
        std::string text;
        if (command && kind == "event") {
            auto event = data.at("event");
            auto event_type = event.at("type").get<std::string>();
            native_id(session, event, "sessionId");
            if (event_type == "text_delta")
                text = event.value("delta", event.value("text", std::string()));
            else if (event_type == "thinking_delta")
                context.emit("ThinkingDelta",
                             {{"text", event.value("delta", event.value("text", std::string()))}});
            else if (event_type == "tool_running")
                context.emit("ToolStarted", event);
            else if (event_type == "tool_completed" || event_type == "tool_errored" ||
                     event_type == "tool_hook_blocked")
                context.emit("ToolCompleted", event);
            else if (event_type == "tool_denied")
                throw Error("AccessDenied", "Command Code denied an action");
            else if (event_type == "run_error" || event_type == "interrupted")
                throw Error("ProviderError", "Command Code run failed");
            else
                context.emit("ProviderEvent", event);
        } else if ((command || buddy) && kind == "result") {
            auto subtype = data.value("subtype", std::string());
            if (subtype == "max_turns" || subtype == "error_max_turns")
                throw Error("TurnLimitExceeded", "Native agent turn limit reached");
            if (subtype != "success" || data.value("is_error", false) ||
                (data.contains("denied_actions") && !data.at("denied_actions").empty()))
                throw Error("ProviderError", "Native agent turn failed");
            result.text = data.at(command ? "finalText" : "result").get<std::string>();
            result.usage = data.value("usage", Json::object());
            completed = true;
        } else if (buddy && kind == "assistant") {
            const auto& message = data.at("message");
            std::string snapshot;
            for (const auto& block : message.at("content")) {
                auto type = block.value("type", std::string());
                if (type == "text")
                    snapshot += block.at("text").get<std::string>();
                else if (type == "tool_use")
                    context.emit("ToolStarted", block);
            }
            if (!snapshot.empty()) {
                auto key = message.value("id", data.value("uuid", std::string("assistant")));
                auto& previous = snapshots[key];
                snapshot_bytes -= previous.size();
                snapshot_bytes += snapshot.size();
                if (snapshot_bytes > 8 * 1024 * 1024 || snapshots.size() > 256)
                    throw Error("ResponseTooLarge", "CLI snapshots exceed limit");
                if (snapshot.rfind(previous, 0) == 0)
                    text = snapshot.substr(previous.size());
                else
                    context.emit("TextSnapshot", {{"id", key}, {"text", snapshot}});
                previous = std::move(snapshot);
            }
        } else if (buddy && kind == "user") {
            for (const auto& block : data.at("message").at("content"))
                if (block.is_object() && block.value("type", std::string()) == "tool_result")
                    context.emit("ToolCompleted", block);
        } else if (buddy && kind == "stream_event") {
            // Some CLI versions emit raw partial messages in addition to assistant snapshots.
            context.emit("ProviderEvent", data);
        } else if (buddy && kind == "system" && data.contains("usage")) {
            context.emit("UsageUpdated", data.at("usage"));
        } else if (agy && kind == "step_update") {
            text = payload.value("text_delta", std::string());
            if (payload.contains("tool_info"))
                context.emit(payload.value("state", std::string()) == "DONE" ? "ToolCompleted"
                                                                             : "ToolStarted",
                             payload.at("tool_info"));
            if (payload.contains("usage"))
                context.emit("UsageUpdated", payload.at("usage"));
        } else if (!agy && !command && !buddy && (kind == "text" || kind == "reasoning")) {
            auto part = data.value("part", Json::object());
            if (kind == "text")
                text = part.value("text", std::string());
            else
                context.emit("ThinkingDelta", {{"text", part.value("text", std::string())}});
        } else if (!agy && !command && !buddy && kind == "tool_use") {
            auto part = data.value("part", Json::object());
            auto status = part.value("state", Json::object()).value("status", std::string());
            context.emit(
                status == "completed" || status == "error" ? "ToolCompleted" : "ToolStarted", part);
        } else if (agy && kind == "result") {
            if (payload.value("status", std::string()) != "SUCCESS")
                throw Error("ProviderError", "Antigravity turn failed");
            result.text = payload.at("response").get<std::string>();
            result.usage = payload.value("usage", Json::object());
            completed = true;
        } else if (!agy && !command && !buddy && kind == "step_finish") {
            auto part = data.value("part", Json::object());
            result.usage = part.value("tokens", Json::object());
            completed = part.value("reason", std::string()) == "stop";
        } else
            context.emit("ProviderEvent", data);
        if (!text.empty()) {
            result.text += text;
            if (result.text.size() > 8 * 1024 * 1024)
                throw Error("ResponseTooLarge", "CLI answer exceeds limit");
            context.emit("TextDelta", {{"text", text}});
        }
    }
    if (process.wait(context.cancellation) != 0)
        throw Error("ProcessCrash", "CLI exited with an error");
    if (!completed)
        throw Error("UnexpectedEOF", "CLI stream ended before its final result");
    if (session.id.empty())
        throw Error("ProtocolError", "Missing native CLI session ID");
    result.finish_reason = "stop";
    messages.push_back({{"role", "assistant"}, {"content", result.text}});
    context.emit("UsageUpdated", result.usage);
    context.emit("TurnCompleted", {{"text", result.text}});
    return result;
}
void AgentCliProvider::disconnect() noexcept {
    {
        std::lock_guard lock(cancellation_mutex_);
        active_.cancel();
    }
    std::lock_guard lock(mutex_);
    connected_ = false;
}
} // namespace cymcp
