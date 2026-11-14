#include <cymcp/ai.hpp>
#include <set>
namespace cymcp {
namespace {
constexpr std::size_t frame_limit = 1024 * 1024;
void http_status(int status) {
    if (status >= 200 && status < 300)
        return;
    throw Error(status == 401 || status == 403 ? "AuthenticationRequired"
                : status == 429                ? "RateLimited"
                                               : "ProviderError",
                "API request rejected with HTTP " + std::to_string(status));
}
HttpRequest request_for(const ApiOptions& options, const std::string& path) {
    HttpRequest request;
    request.url = options.base_url + path;
    request.timeout = options.timeout;
    request.headers = {{"Accept", "application/json"}, {"Content-Type", "application/json"}};
    if (options.protocol == ApiProtocol::AnthropicMessages) {
        request.headers["x-api-key"] = options.api_key;
        request.headers["anthropic-version"] = "2023-06-01";
    } else if (!options.api_key.empty())
        request.headers["Authorization"] = "Bearer " + options.api_key;
    return request;
}
Json get(const ApiOptions& options, const std::string& path, const CallContext& context) {
    auto response = HttpClient().perform(request_for(options, path), context.cancellation);
    http_status(response.status);
    return parse_json(response.body, 16 * 1024 * 1024);
}
std::string percent_encode(const std::string& value) {
    const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
            result += static_cast<char>(c);
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
// Incremental UTF-8 framing: bytes are decoded as JSON only after a complete frame.
class Frames {
    bool ndjson_;
    std::string line_, data_;
    std::function<void(const Json&)> consume_;
    void line() {
        if (!line_.empty() && line_.back() == '\r')
            line_.pop_back();
        if (ndjson_) {
            if (!line_.empty())
                consume_(parse_json(line_));
        } else if (line_.empty()) {
            if (!data_.empty()) {
                data_.pop_back();
                consume_(parse_json(data_));
                data_.clear();
            }
        } else if (line_.rfind("data:", 0) == 0)
            data_ += line_.substr(line_.size() > 5 && line_[5] == ' ' ? 6 : 5) + "\n";
        line_.clear();
    }

  public:
    Frames(bool ndjson, std::function<void(const Json&)> consume)
        : ndjson_(ndjson), consume_(std::move(consume)) {}
    void feed(const char* bytes, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            if (bytes[i] == '\n')
                line();
            else
                line_ += bytes[i];
            if (line_.size() > frame_limit || data_.size() > frame_limit)
                throw Error("ResponseTooLarge", "API stream frame exceeds limit");
        }
    }
    void finish() {
        if (ndjson_ && !line_.empty())
            line();
        if (!line_.empty() || !data_.empty())
            throw Error("UnexpectedEOF", "Truncated API stream frame");
    }
};
void stream(HttpRequest request, const ApiOptions& options, const Json& body,
            const CallContext& context, const std::function<void(const Json&)>& consume) {
    request.method = "POST";
    request.headers["Accept"] =
        options.protocol == ApiProtocol::Ollama ? "application/x-ndjson" : "text/event-stream";
    request.body = body.dump();
    if (request.body.size() > 8 * 1024 * 1024)
        throw Error("ContextTooLarge", "API request exceeds limit");
    Frames frames(options.protocol == ApiProtocol::Ollama, consume);
    std::exception_ptr failure;
    auto response =
        HttpClient().perform(request, context.cancellation, [&](const char* p, std::size_t n) {
            if (!failure)
                try {
                    frames.feed(p, n);
                } catch (...) {
                    failure = std::current_exception();
                }
            return true;
        });
    // An error body is not a generation stream and may contain private account data.
    http_status(response.status);
    if (failure)
        std::rethrow_exception(failure);
    frames.finish();
}
struct Tools {
    std::map<std::string, std::string> original;
    Json descriptions = Json::array();
    explicit Tools(const std::shared_ptr<const Registry>& registry) {
        if (!registry)
            return;
        for (auto tool : registry->describe()) {
            auto name = tool.at("name").get<std::string>();
            auto wire = name;
            if (wire.size() > 64 ||
                wire.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                    std::string::npos)
                wire = "cy_" + sha256(name).substr(0, 32);
            if (!original.emplace(wire, name).second)
                throw Error("InvalidOptions", "Tool wire-name collision");
            tool["name"] = wire;
            descriptions.push_back(std::move(tool));
        }
    }
    Json definitions(ApiProtocol protocol) const {
        Json result = Json::array();
        for (const auto& tool : descriptions) {
            Json function = {{"name", tool.at("name")}, {"description", tool.at("description")}};
            if (protocol == ApiProtocol::AnthropicMessages)
                function["input_schema"] = tool.at("inputSchema");
            else
                function["parameters"] = tool.at("inputSchema");
            if (protocol == ApiProtocol::OpenAIResponses) {
                function["type"] = "function";
                function["strict"] = false;
                result.push_back(function);
            } else if (protocol == ApiProtocol::AnthropicMessages)
                result.push_back(function);
            else
                result.push_back({{"type", "function"}, {"function", function}});
        }
        return result;
    }
    Json execute(const ApiOptions& options, const Json& call, const CallContext& context) const {
        auto wire = call.at("name").get<std::string>();
        auto id = call.at("id").get<std::string>();
        context.emit("ToolStarted", {{"id", id}, {"name", wire}});
        Json output;
        try {
            auto found = original.find(wire);
            if (!options.tools || found == original.end())
                throw Error("UnknownOperation", "Unknown API tool");
            auto args = call.at("arguments");
            if (args.is_string())
                args = parse_json(args.get<std::string>());
            output = options.tools->invoke(found->second, args, context);
        } catch (const Error& error) {
            if (error.code == "Cancelled" || error.code == "Timeout")
                throw;
            output = {{"error", {{"code", error.code}, {"message", error.what()}}}};
        } catch (const Json::exception&) {
            output = {{"error", {{"code", "InvalidArguments"}}}};
        }
        context.emit("ToolCompleted", {{"id", id}, {"name", wire}, {"output", output}});
        return output;
    }
};
std::string protocol_id(ApiProtocol protocol) {
    return protocol == ApiProtocol::OpenAIResponses     ? "responses"
           : protocol == ApiProtocol::AnthropicMessages ? "anthropic"
                                                        : "ollama";
}
bool native(const Json& message, const std::string& protocol) {
    return message.contains("_cymcp_native") &&
           message.at("_cymcp_native").value("protocol", std::string()) == protocol;
}
Json history_for(const Json& messages, ApiProtocol protocol, std::string& system) {
    Json history = Json::array();
    for (const auto& message : messages) {
        auto role = message.at("role").get<std::string>();
        auto content = message.value("content", Json(""));
        if (protocol == ApiProtocol::AnthropicMessages && role == "system") {
            system += detail::content_text(content) + "\n";
            continue;
        }
        if (role == "tool") {
            if (protocol == ApiProtocol::OpenAIResponses)
                history.push_back({{"type", "function_call_output"},
                                   {"call_id", message.at("tool_call_id")},
                                   {"output", content}});
            else if (protocol == ApiProtocol::AnthropicMessages) {
                Json block = {{"type", "tool_result"},
                              {"tool_use_id", message.at("tool_call_id")},
                              {"content", content}};
                if (!history.empty() && history.back().at("role") == "user" &&
                    history.back().at("content").is_array())
                    history.back()["content"].push_back(block);
                else
                    history.push_back({{"role", "user"}, {"content", Json::array({block})}});
            } else
                history.push_back(
                    {{"role", "tool"}, {"tool_name", message.at("name")}, {"content", content}});
        } else if (native(message, protocol_id(protocol))) {
            const auto& blocks = message.at("_cymcp_native").at("content");
            if (protocol == ApiProtocol::OpenAIResponses)
                for (const auto& item : blocks)
                    history.push_back(item);
            else if (protocol == ApiProtocol::AnthropicMessages)
                history.push_back({{"role", role}, {"content", blocks}});
            else
                history.push_back(blocks);
        } else if (protocol == ApiProtocol::Ollama && content.is_array()) {
            validate_content(content);
            Json images = Json::array();
            std::string text;
            for (const auto& block : content) {
                auto type = block.at("type").get<std::string>();
                if (type == "text")
                    text += block.at("text").get<std::string>();
                else if (type == "image")
                    images.push_back(block.at("data"));
                else if (type == "resource" && block.at("resource").contains("text"))
                    text += "\n" + block.at("resource").at("uri").get<std::string>() + "\n" +
                            block.at("resource").at("text").get<std::string>();
                else
                    throw Error("UnsupportedContent",
                                "Ollama chat supports text and embedded images");
            }
            Json wire_message = {{"role", role}, {"content", text}};
            if (!images.empty())
                wire_message["images"] = images;
            history.push_back(std::move(wire_message));
        } else {
            auto format =
                protocol == ApiProtocol::OpenAIResponses     ? detail::ContentFormat::Responses
                : protocol == ApiProtocol::AnthropicMessages ? detail::ContentFormat::Anthropic
                                                             : detail::ContentFormat::Ollama;
            history.push_back(
                {{"role", role}, {"content", detail::content_for(content, format, role)}});
        }
    }
    return history;
}
void append(std::string& text, const std::string& delta) {
    text += delta;
    if (text.size() > 8 * 1024 * 1024)
        throw Error("ResponseTooLarge", "API answer exceeds limit");
}
} // namespace
ApiProvider::ApiProvider(ApiOptions options)
    : Provider(options.protocol == ApiProtocol::OpenAIResponses     ? "openai"
               : options.protocol == ApiProtocol::AnthropicMessages ? "anthropic"
               : options.protocol == ApiProtocol::Ollama            ? "ollama"
                                                                    : "openrouter"),
      options_(std::move(options)) {
    if (options_.base_url.empty())
        switch (options_.protocol) {
        case ApiProtocol::OpenAIResponses:
            options_.base_url = "https://api.openai.com/v1";
            break;
        case ApiProtocol::AnthropicMessages:
            options_.base_url = "https://api.anthropic.com/v1";
            break;
        case ApiProtocol::Ollama:
            options_.base_url = "http://127.0.0.1:11434";
            break;
        case ApiProtocol::OpenRouter:
            options_.base_url = "https://openrouter.ai/api/v1";
            break;
        }
    while (!options_.base_url.empty() && options_.base_url.back() == '/')
        options_.base_url.pop_back();
    auto url = parse_url(options_.base_url);
    if (url.target.find('?') != std::string::npos || options_.max_tool_rounds < 1 ||
        options_.max_tool_rounds > 64 || options_.max_output_tokens < 1 ||
        options_.timeout.count() <= 0 || options_.timeout > std::chrono::hours(24))
        throw Error("InvalidOptions", "Invalid API options");
}
ProviderCapabilities ApiProvider::capabilities() const {
    bool chat = options_.protocol == ApiProtocol::OpenRouter;
    return {true,
            false,
            static_cast<bool>(options_.tools),
            true,
            true,
            true,
            chat,
            options_.protocol == ApiProtocol::OpenAIResponses ||
                options_.protocol == ApiProtocol::AnthropicMessages,
            false};
}
void ApiProvider::connect(const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + options_.timeout);
    context.cancellation.check();
    if (options_.protocol != ApiProtocol::Ollama && options_.api_key.empty())
        throw Error("AuthenticationRequired", "Supply an API key through the host application");
    if (options_.protocol == ApiProtocol::OpenRouter)
        get(options_, "/key", context);
    list_models(context);
}
Json ApiProvider::list_models(const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + options_.timeout);
    Json rows = Json::array();
    std::set<std::string> cursors;
    std::string path = options_.protocol == ApiProtocol::Ollama ? "/api/tags" : "/models";
    for (int page = 0; page < 128; ++page) {
        auto data = get(options_, path, context);
        auto entries =
            data.value(options_.protocol == ApiProtocol::Ollama ? "models" : "data", Json());
        if (!entries.is_array())
            throw Error("ProtocolError", "Missing API model catalogue");
        for (auto row : entries) {
            if (options_.protocol == ApiProtocol::Ollama)
                row["id"] = row.value("model", row.value("name", std::string()));
            if (!row.is_object() || !row.contains("id") || !row.at("id").is_string() ||
                row.at("id").get<std::string>().empty())
                throw Error("ProtocolError", "Invalid model catalogue entry");
            rows.push_back(std::move(row));
        }
        if (rows.dump().size() > 16 * 1024 * 1024)
            throw Error("ResponseTooLarge", "Model catalogue exceeds limit");
        if (options_.protocol != ApiProtocol::AnthropicMessages || !data.value("has_more", false))
            return rows;
        auto cursor = data.value("last_id", std::string());
        if (cursor.empty() || !cursors.insert(cursor).second)
            throw Error("ProtocolError", "Invalid model catalogue pagination");
        path = "/models?after_id=" + percent_encode(cursor);
    }
    throw Error("ProtocolError", "Too many model catalogue pages");
}
Session ApiProvider::start_session(std::string model, std::string resume,
                                   const CallContext& context, std::string) {
    context.cancellation.check();
    if (!resume.empty())
        throw Error("Unsupported",
                    "API conversation history is owned by the host, not a native CLI session");
    if (model.empty())
        throw Error("UnknownModel", "Select an exact model ID");
    return {random_token(16), std::move(model)};
}
ChatResult ApiProvider::send(Session& session, Json& messages, const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + options_.timeout);
    if (!messages.is_array() || messages.empty())
        throw Error("InvalidParameters", "Messages required");
    if (options_.protocol == ApiProtocol::OpenRouter) {
        HttpChatOptions opts{options_.base_url, options_.api_key, options_.tools,
                             options_.max_tool_rounds, options_.timeout};
        return HttpChatProvider(std::move(opts)).send(session, messages, context);
    }
    Tools tools(options_.tools);
    auto protocol = options_.protocol;
    for (std::size_t round = 0; round < options_.max_tool_rounds; ++round) {
        context.cancellation.check();
        ChatResult result;
        Json calls = Json::array(), content = Json::array();
        std::string system;
        auto history = history_for(messages, protocol, system);
        Json body = {{"model", session.model}, {"stream", true}};
        std::string path;
        if (protocol == ApiProtocol::OpenAIResponses) {
            path = "/responses";
            body["input"] = history;
            body["store"] = false;
            body["include"] = {"reasoning.encrypted_content"};
            body["max_output_tokens"] = options_.max_output_tokens;
        } else if (protocol == ApiProtocol::AnthropicMessages) {
            path = "/messages";
            body["messages"] = history;
            body["max_tokens"] = options_.max_output_tokens;
            if (!system.empty())
                body["system"] = system;
        } else {
            path = "/api/chat";
            body["messages"] = history;
            body["options"] = {{"num_predict", options_.max_output_tokens}};
        }
        if (!tools.descriptions.empty())
            body["tools"] = tools.definitions(protocol);
        bool terminal = false;
        std::string reason, thinking;
        std::map<int, Json> blocks;
        std::map<int, std::string> arguments;
        std::set<int> closed_blocks;
        stream(request_for(options_, path), options_, body, context, [&](const Json& data) {
            if (!data.is_object())
                throw Error("ProtocolError", "Expected API event object");
            auto type = data.value("type", std::string());
            if (type == "error" || type == "response.failed" || type == "response.incomplete" ||
                data.contains("error"))
                throw Error("ProviderError", "API generation failed");
            if (terminal)
                throw Error("ProtocolError", "API data after terminal result");
            std::string text;
            if (protocol == ApiProtocol::OpenAIResponses) {
                if (type == "response.output_text.delta")
                    text = data.at("delta").get<std::string>();
                else if (type == "response.reasoning_summary_text.delta")
                    context.emit("ThinkingDelta", {{"text", data.at("delta")}});
                else if (type == "response.completed") {
                    auto response = data.at("response");
                    if (response.value("status", std::string()) != "completed")
                        throw Error("ProviderError", "OpenAI response did not complete");
                    content = response.at("output");
                    if (!content.is_array())
                        throw Error("ProtocolError", "Invalid Responses output");
                    result.text.clear();
                    for (const auto& item : content) {
                        auto kind = item.value("type", std::string());
                        if (kind == "message")
                            for (const auto& part : item.at("content")) {
                                if (part.value("type", std::string()) == "output_text")
                                    append(result.text, part.at("text").get<std::string>());
                                else if (part.value("type", std::string()) == "refusal")
                                    append(result.text, part.at("refusal").get<std::string>());
                            }
                        else if (kind == "function_call")
                            calls.push_back({{"id", item.at("call_id")},
                                             {"name", item.at("name")},
                                             {"arguments", item.at("arguments")}});
                    }
                    result.usage = response.value("usage", Json::object());
                    reason = calls.empty() ? "stop" : "tool_calls";
                    terminal = true;
                }
            } else if (protocol == ApiProtocol::AnthropicMessages) {
                if (type == "message_start")
                    result.usage = data.at("message").value("usage", Json::object());
                else if (type == "content_block_start") {
                    int index = data.at("index").get<int>();
                    if (index < 0 || index >= 64 || blocks.contains(index))
                        throw Error("ProtocolError", "Invalid content block index");
                    blocks[index] = data.at("content_block");
                    if (blocks.at(index).value("type", std::string()) == "text")
                        text = blocks.at(index).value("text", std::string());
                } else if (type == "content_block_delta") {
                    int index = data.at("index").get<int>();
                    if (!blocks.contains(index) || closed_blocks.contains(index))
                        throw Error("ProtocolError", "Missing content block start");
                    auto& block = blocks.at(index);
                    auto delta = data.at("delta");
                    auto kind = delta.at("type").get<std::string>();
                    if (kind == "text_delta") {
                        text = delta.at("text").get<std::string>();
                        block["text"] = block.value("text", std::string()) + text;
                    } else if (kind == "input_json_delta") {
                        append(arguments[index], delta.at("partial_json").get<std::string>());
                    } else if (kind == "thinking_delta") {
                        auto value = delta.at("thinking").get<std::string>();
                        block["thinking"] = block.value("thinking", std::string()) + value;
                        context.emit("ThinkingDelta", {{"text", value}});
                    } else if (kind == "signature_delta")
                        block["signature"] = block.value("signature", std::string()) +
                                             delta.at("signature").get<std::string>();
                } else if (type == "content_block_stop") {
                    int index = data.at("index").get<int>();
                    if (!blocks.contains(index) || !closed_blocks.insert(index).second)
                        throw Error("ProtocolError", "Invalid content block stop");
                } else if (type == "message_delta") {
                    reason = data.at("delta").value("stop_reason", std::string());
                    if (data.contains("usage"))
                        result.usage.update(data.at("usage"));
                } else if (type == "message_stop") {
                    if (reason.empty() || closed_blocks.size() != blocks.size())
                        throw Error("ProtocolError", "Incomplete Anthropic message");
                    for (auto& [index, block] : blocks) {
                        if (block.value("type", std::string()) == "tool_use") {
                            if (arguments.contains(index))
                                block["input"] = parse_json(arguments.at(index));
                            calls.push_back({{"id", block.at("id")},
                                             {"name", block.at("name")},
                                             {"arguments", block.at("input")}});
                        }
                        content.push_back(block);
                    }
                    terminal = true;
                }
            } else {
                auto message = data.value("message", Json::object());
                text = message.value("content", std::string());
                if (message.contains("thinking")) {
                    auto delta = message.at("thinking").get<std::string>();
                    append(thinking, delta);
                    context.emit("ThinkingDelta", {{"text", delta}});
                }
                if (message.contains("tool_calls"))
                    for (const auto& call : message.at("tool_calls")) {
                        content.push_back(call);
                        calls.push_back({{"id", "ollama_" + std::to_string(calls.size())},
                                         {"name", call.at("function").at("name")},
                                         {"arguments", call.at("function").at("arguments")}});
                    }
                terminal = data.value("done", false);
                if (terminal) {
                    reason = data.value("done_reason", std::string("stop"));
                    for (auto key :
                         {"prompt_eval_count", "eval_count", "total_duration", "load_duration"})
                        if (data.contains(key))
                            result.usage[key] = data.at(key);
                }
            }
            if (!text.empty()) {
                append(result.text, text);
                context.emit("TextDelta", {{"text", text}});
            }
        });
        if (!terminal)
            throw Error("UnexpectedEOF", "API stream ended before its final result");
        if (calls.size() > 32)
            throw Error("ProtocolError", "Too many API tool calls");
        if (!calls.empty() &&
            ((protocol == ApiProtocol::AnthropicMessages && reason != "tool_use") ||
             (protocol == ApiProtocol::OpenAIResponses && reason != "tool_calls")))
            throw Error("ProtocolError", "Invalid tool completion reason");
        if (protocol == ApiProtocol::AnthropicMessages && calls.empty() && reason == "tool_use")
            throw Error("ProtocolError", "Missing Anthropic tool calls");
        if (protocol == ApiProtocol::Ollama) {
            Json assistant = {{"role", "assistant"}, {"content", result.text}};
            if (!thinking.empty())
                assistant["thinking"] = thinking;
            if (!content.empty())
                assistant["tool_calls"] = content;
            content = assistant;
        }
        messages.push_back(
            {{"role", "assistant"},
             {"content", result.text},
             {"_cymcp_native", {{"protocol", protocol_id(protocol)}, {"content", content}}}});
        result.finish_reason = reason;
        context.emit("UsageUpdated", result.usage);
        if (calls.empty()) {
            context.emit("TurnCompleted", {{"text", result.text}, {"finishReason", reason}});
            return result;
        }
        std::set<std::string> ids;
        for (const auto& call : calls) {
            auto id = call.at("id").get<std::string>();
            if (id.empty() || !ids.insert(id).second)
                throw Error("ProtocolError", "Missing or duplicate API tool call ID");
        }
        for (const auto& call : calls) {
            auto output = tools.execute(options_, call, context);
            messages.push_back({{"role", "tool"},
                                {"tool_call_id", call.at("id")},
                                {"name", call.at("name")},
                                {"content", output.dump()}});
        }
    }
    throw Error("ToolBudgetExceeded", "Maximum API tool rounds reached");
}
} // namespace cymcp
