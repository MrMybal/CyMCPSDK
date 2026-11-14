#include <cymcp/ai.hpp>
namespace cymcp {
ClaudeProvider::ClaudeProvider(ClaudeOptions options) : options_(std::move(options)) {
    if (!options_.mcp_servers.is_object())
        throw Error("InvalidOptions", "MCP server configuration must be an object");
}
ProviderCapabilities ClaudeProvider::capabilities() const {
    return {true, true,
            !options_.mcp_servers.empty() || !options_.allowed_tools.empty() ||
                options_.allow_all_actions,
            false, false};
}
void ClaudeProvider::connect(const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + options_.connect_timeout);
    std::lock_guard lock(mutex_);
    auto config = options_.process;
    config.arguments.insert(config.arguments.end(), {"auth", "status"});
    Process probe(config);
    probe.close_input();
    std::string output;
    ProcessLine line;
    while (probe.next(line, context.cancellation))
        if (!line.stderr_stream)
            output += line.text;
    auto code = probe.wait(context.cancellation);
    try {
        if (code || !parse_json(output).value("loggedIn", false))
            throw Error("AuthenticationRequired", "Sign in using Claude's native login flow");
    } catch (const Json::exception&) {
        throw Error("ProtocolError", "Cannot read Claude authentication status");
    }
}
Session ClaudeProvider::start_session(std::string model, std::string resume,
                                      const CallContext& context, std::string) {
    context.cancellation.check();
    return {std::move(resume), std::move(model)};
}
ChatResult ClaudeProvider::send(Session& session, Json& messages, const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + options_.turn_timeout);
    std::lock_guard lock(mutex_);
    if (messages.empty() || !messages.back()["content"].is_string())
        throw Error("Unsupported", "Claude CLI connector currently accepts text input");
    auto config = options_.process;
    config.arguments.insert(config.arguments.end(),
                            {"-p", "--output-format", "stream-json", "--verbose",
                             "--include-partial-messages", "--strict-mcp-config", "--mcp-config",
                             Json({{"mcpServers", options_.mcp_servers}}).dump(),
                             "--setting-sources", ""});
    if (options_.allow_all_actions)
        config.arguments.push_back("--dangerously-skip-permissions");
    else {
        config.arguments.insert(config.arguments.end(),
                                {"--permission-mode", "dontAsk", "--tools"});
        std::string tools;
        for (const auto& tool : options_.allowed_tools) {
            if (!tools.empty())
                tools += ",";
            tools += tool;
        }
        config.arguments.push_back(tools);
    }
    if (!session.model.empty())
        config.arguments.insert(config.arguments.end(), {"--model", session.model});
    if (!session.id.empty())
        config.arguments.insert(config.arguments.end(), {"--resume", session.id});
    // Explicit system messages are forwarded once for this one-shot invocation.
    std::string system;
    for (const auto& message : messages)
        if (message.value("role", std::string()) == "system" && message["content"].is_string()) {
            system += message["content"].get<std::string>() + "\n";
        }
    if (!system.empty())
        config.arguments.insert(config.arguments.end(), {"--append-system-prompt", system});
    Process process(config);
    process.write(messages.back()["content"].get<std::string>(), context.cancellation);
    process.close_input();
    ProcessLine line;
    ChatResult result;
    bool completed = false;
    while (process.next(line, context.cancellation)) {
        if (line.stderr_stream)
            continue;
        Json value;
        try {
            value = parse_json(line.text);
        } catch (...) {
            throw Error("MalformedJSON", "Invalid Claude stream JSON");
        }
        if (!value.is_object())
            throw Error("ProtocolError", "Expected Claude event object");
        if (value.contains("session_id")) {
            auto native = value.at("session_id").get<std::string>();
            if (!session.id.empty() && session.id != native)
                throw Error("SessionInvalid", "Claude returned a different session ID");
            session.id = native;
        }
        auto type = value.value("type", std::string());
        if (type == "stream_event") {
            auto event = value.value("event", Json::object());
            auto delta = event.value("delta", Json::object());
            if (delta.value("type", std::string()) == "text_delta")
                context.emit("TextDelta", {{"text", delta.value("text", std::string())}});
            else if (delta.value("type", std::string()) == "thinking_delta")
                context.emit("ThinkingDelta", {{"text", delta.value("thinking", std::string())}});
        } else if (type == "result") {
            if (completed)
                throw Error("ProtocolError", "Duplicate Claude result");
            if (value.value("is_error", false) ||
                value.value("subtype", std::string()) != "success")
                throw Error("ProviderError", "Claude turn failed");
            result.text = value.at("result").get<std::string>();
            result.usage = value.value("usage", Json::object());
            result.finish_reason = "stop";
            completed = true;
        } else if (type == "assistant" && value.contains("message")) {
            for (const auto& item : value["message"].value("content", Json::array()))
                if (item.value("type", std::string()) == "tool_use")
                    context.emit("ToolStarted", item);
        } else if (type == "user" && value.contains("message")) {
            for (const auto& item : value["message"].value("content", Json::array()))
                if (item.is_object() && item.value("type", std::string()) == "tool_result")
                    context.emit("ToolCompleted", item);
        }
    }
    if (process.wait(context.cancellation) != 0)
        throw Error("ProcessCrash", "Claude exited with an error");
    if (!completed)
        throw Error("UnexpectedEOF", "Claude stream ended before its final result");
    if (session.id.empty())
        throw Error("ProtocolError", "Missing native Claude session ID");
    messages.push_back({{"role", "assistant"}, {"content", result.text}});
    context.emit("UsageUpdated", result.usage);
    context.emit("TurnCompleted", {{"text", result.text}});
    return result;
}
} // namespace cymcp
