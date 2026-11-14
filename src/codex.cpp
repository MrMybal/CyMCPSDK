#include <cymcp/ai.hpp>
#include <set>
namespace cymcp {
struct CodexProvider::Impl {
    CodexOptions options;
    std::unique_ptr<Process> process;
    std::mutex mutex;
    std::int64_t counter = 0;
    std::deque<Json> pending;
    std::size_t pending_bytes = 0;
    std::map<std::string, std::string> tool_names;
    std::string active_session;
    explicit Impl(CodexOptions config) : options(std::move(config)) {}
    Json read(const Cancellation& cancel) {
        ProcessLine line;
        while (process->next(line, cancel)) {
            if (line.stderr_stream)
                continue;
            try {
                auto value = parse_json(line.text);
                if (!value.is_object())
                    throw Error("ProtocolError", "Expected provider message object");
                return value;
            } catch (const Json::exception&) {
                throw Error("MalformedJSON", "Invalid provider protocol JSON");
            }
        }
        throw Error("UnexpectedEOF", "Provider process closed its stream");
    }
    void server_request(const Json& request, const CallContext& context) {
        auto method = request.at("method").get<std::string>();
        auto params = request.value("params", Json::object());
        Json response = {{"id", request["id"]}};
        if (method == "item/tool/call" && options.tools) {
            bool success = true;
            Json output;
            try {
                context.cancellation.check();
                if (active_session.empty() ||
                    params.value("threadId", std::string()) != active_session)
                    throw Error("SessionInvalid", "Tool call belongs to another session");
                auto requested = params.at("tool").get<std::string>();
                auto found = tool_names.find(requested);
                if (found == tool_names.end())
                    throw Error("UnknownOperation", "Provider requested an unregistered tool");
                output = options.tools->invoke(found->second, params.at("arguments"), context);
            } catch (const Error& e) {
                success = false;
                output = {{"code", e.code}, {"message", e.what()}};
            } catch (...) {
                success = false;
                output = {{"code", "InvalidArguments"}};
            }
            response["result"] = {
                {"contentItems", Json::array({{{"type", "inputText"}, {"text", output.dump()}}})},
                {"success", success}};
        } else if (options.request_handler)
            response["result"] = options.request_handler(method, params, context);
        else if (method == "item/commandExecution/requestApproval" ||
                 method == "item/fileChange/requestApproval")
            response["result"] = {{"decision", options.approvals.allows(context, method, params)
                                                   ? "accept"
                                                   : "decline"}};
        else if (method == "item/permissions/requestApproval")
            response["result"] = {{"permissions", options.approvals.allows(context, method, params)
                                                      ? params.value("permissions", Json::object())
                                                      : Json::object()},
                                  {"scope", "turn"}};
        else
            response["error"] = {{"code", -32601},
                                 {"message", "Host did not implement this provider request"}};
        process->write(response.dump() + "\n", context.cancellation);
    }
    Json rpc(const std::string& method, Json params, const CallContext& context) {
        auto id = ++counter;
        process->write(
            Json({{"id", id}, {"method", method}, {"params", std::move(params)}}).dump() + "\n",
            context.cancellation);
        while (true) {
            auto value = read(context.cancellation);
            if (value.contains("method") && value.contains("id")) {
                server_request(value, context);
                continue;
            }
            if (value.value("id", Json()) == id) {
                if (value.contains("error"))
                    throw Error("ProviderError", "Provider RPC rejected: " + method);
                if (!value.contains("result"))
                    throw Error("ProtocolError", "Missing provider RPC result");
                return value["result"];
            }
            auto bytes = value.dump().size();
            if (pending.size() >= 1024 || pending_bytes + bytes > 4 * 1024 * 1024)
                throw Error("OutputOverflow", "Provider event backlog exceeds limit");
            pending_bytes += bytes;
            pending.push_back(std::move(value));
        }
    }
    Json next(const Cancellation& cancel) {
        if (pending.empty())
            return read(cancel);
        auto value = std::move(pending.front());
        pending.pop_front();
        pending_bytes -= value.dump().size();
        return value;
    }
};
CodexProvider::CodexProvider(CodexOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
CodexProvider::~CodexProvider() = default;
ProviderCapabilities CodexProvider::capabilities() const {
    return {true, true, static_cast<bool>(impl_->options.tools), true, false};
}
void CodexProvider::connect(const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + impl_->options.connect_timeout);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (p.process)
        throw Error("InvalidState", "Codex already connected");
    auto config = p.options.process;
    config.arguments.insert(config.arguments.end(), {"app-server", "--listen", "stdio://"});
    p.pending.clear();
    p.pending_bytes = 0;
    try {
        p.process = std::make_unique<Process>(config);
        p.rpc("initialize",
              {{"clientInfo", {{"name", "cymcp_sdk"}, {"version", version}}},
               {"capabilities", {{"experimentalApi", static_cast<bool>(p.options.tools)}}}},
              context);
        p.process->write("{\"method\":\"initialized\",\"params\":{}}\n");
        auto account = p.rpc("account/read", {{"refreshToken", false}}, context);
        if (!account.contains("account") || account["account"].is_null())
            throw Error("AuthenticationRequired", "Sign in using the provider's native login flow");
    } catch (...) {
        p.process.reset();
        throw;
    }
}
Json CodexProvider::list_models(const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + impl_->options.connect_timeout);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.process)
        throw Error("InvalidState", "Connect Codex first");
    Json models = Json::array(), params = Json::object();
    std::set<std::string> seen;
    for (int page = 0; page < 1000; ++page) {
        auto result = p.rpc("model/list", params, context);
        for (const auto& model : result.at("data")) {
            models.push_back(model);
            if (models.size() > 10000)
                throw Error("ResponseTooLarge", "Model catalogue exceeds limit");
        }
        auto cursor = result.value("nextCursor", Json());
        if (cursor.is_null() || cursor == "")
            return models;
        auto text = cursor.get<std::string>();
        if (!seen.insert(text).second)
            throw Error("ProtocolError", "Cyclic model pagination");
        params["cursor"] = text;
    }
    throw Error("ResponseTooLarge", "Too many model pages");
}
Session CodexProvider::start_session(std::string model, std::string resume,
                                     const CallContext& input, std::string system) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + impl_->options.connect_timeout);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.process)
        throw Error("InvalidState", "Connect Codex first");
    Json params = {{"approvalPolicy", p.options.approval_policy}, {"sandbox", p.options.sandbox}};
    if (!p.options.process.cwd.empty())
        params["cwd"] = path_utf8(p.options.process.cwd);
    if (!model.empty())
        params["model"] = model;
    if (!resume.empty())
        params["threadId"] = resume;
    if (!system.empty())
        params["developerInstructions"] = std::move(system);
    if (p.options.tools) {
        Json tools = Json::array();
        p.tool_names.clear();
        for (const auto& tool : p.options.tools->describe()) {
            auto original = tool["name"].get<std::string>();
            auto name = original;
            if (name.size() > 64 ||
                name.find_first_not_of(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") !=
                    std::string::npos)
                name = "cy_" + sha256(original).substr(0, 32);
            p.tool_names.emplace(name, original);
            tools.push_back(
                {{"type", "function"},
                 {"name", name},
                 {"description", "[" + original + "] " + tool["description"].get<std::string>()},
                 {"inputSchema", tool["inputSchema"]}});
        }
        if (resume.empty())
            params["dynamicTools"] = std::move(tools);
    }
    auto result = p.rpc(resume.empty() ? "thread/start" : "thread/resume", params, context);
    auto native = result.at("thread").at("id").get<std::string>();
    if (native.empty() || (!resume.empty() && native != resume))
        throw Error("SessionInvalid", "Provider returned a different session ID");
    auto actual_model = result.value("model", model);
    return {native, actual_model};
}
ChatResult CodexProvider::send(Session& session, Json& messages, const CallContext& input) {
    auto context = input;
    context.cancellation.deadline =
        std::min(context.cancellation.deadline, Clock::now() + impl_->options.turn_timeout);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.process || session.id.empty())
        throw Error("InvalidState", "Connect and start a Codex session first");
    if (messages.empty() || !messages.back()["content"].is_string())
        throw Error("Unsupported", "Codex connector currently accepts text input");
    std::string turn_id;
    p.active_session = session.id;
    struct SessionCleanup {
        Impl& p;
        ~SessionCleanup() {
            p.active_session.clear();
        }
    } session_cleanup{p};
    ChatResult result;
    std::string streamed;
    std::vector<std::string> order;
    std::map<std::string, std::string> final_items;
    try {
        auto turn = p.rpc(
            "turn/start",
            {{"threadId", session.id},
             {"input", Json::array({{{"type", "text"}, {"text", messages.back()["content"]}}})},
             {"approvalPolicy", p.options.approval_policy}},
            context);
        turn_id = turn.at("turn").at("id").get<std::string>();
        while (true) {
            context.cancellation.check();
            auto value = p.next(context.cancellation);
            if (value.contains("method") && value.contains("id")) {
                p.server_request(value, context);
                continue;
            }
            auto method = value.value("method", std::string());
            auto params = value.value("params", Json::object());
            if (params.contains("threadId") && params["threadId"] != session.id)
                continue;
            if (params.contains("turnId") && params["turnId"] != turn_id)
                continue;
            if (method == "item/agentMessage/delta") {
                auto text = params.at("delta").get<std::string>();
                streamed += text;
                context.emit("TextDelta", {{"text", text}});
            } else if (method == "item/reasoning/textDelta" ||
                       method == "item/reasoning/summaryTextDelta")
                context.emit("ThinkingDelta", {{"text", params.value("delta", std::string())}});
            else if (method == "item/started" || method == "item/completed") {
                auto item = params.at("item");
                bool completed = method == "item/completed";
                auto type = item.value("type", std::string());
                if (completed && type == "agentMessage") {
                    auto id = item.at("id").get<std::string>();
                    if (!final_items.count(id))
                        order.push_back(id);
                    final_items[id] = item.value("text", std::string());
                } else if (type == "commandExecution" || type == "fileChange" ||
                           type == "mcpToolCall" || type == "dynamicToolCall")
                    context.emit(completed ? "ToolCompleted" : "ToolStarted", item);
            } else if (method == "thread/tokenUsage/updated") {
                result.usage = params.value("tokenUsage", Json::object());
                context.emit("UsageUpdated", result.usage);
            } else if (method == "turn/completed" && params.at("turn").at("id") == turn_id) {
                auto final = params.at("turn");
                if (final.value("status", std::string()) != "completed")
                    throw Error("ProviderError", "Codex turn did not complete successfully");
                for (const auto& id : order)
                    result.text += final_items[id];
                if (order.empty())
                    result.text = streamed;
                result.finish_reason = "stop";
                messages.push_back({{"role", "assistant"}, {"content", result.text}});
                context.emit("TurnCompleted", {{"text", result.text}});
                return result;
            } else if (method == "error")
                context.emit("ProviderError", {{"code", "CodexNotification"}});
        }
    } catch (const Error& e) {
        if ((e.code == "Cancelled" || e.code == "Timeout") && !turn_id.empty()) {
            try {
                p.process->write(Json({{"id", ++p.counter},
                                       {"method", "turn/interrupt"},
                                       {"params", {{"threadId", session.id}, {"turnId", turn_id}}}})
                                     .dump() +
                                 "\n");
                Cancellation cleanup;
                cleanup.deadline = Clock::now() + std::chrono::seconds(2);
                while (true) {
                    auto v = p.next(cleanup);
                    if (v.contains("method") && v.contains("id")) {
                        p.process->write(
                            Json({{"id", v["id"]},
                                  {"error",
                                   {{"code", -32603}, {"message", "Turn cancelled by host"}}}})
                                .dump() +
                            "\n");
                        continue;
                    }
                    if (v.value("method", std::string()) == "turn/completed" &&
                        v["params"]["turn"]["id"] == turn_id)
                        break;
                }
            } catch (...) {
                p.process.reset();
            }
        } else
            p.process.reset();
        throw;
    } catch (...) {
        p.process.reset();
        throw;
    }
}
void CodexProvider::disconnect() noexcept {
    std::lock_guard lock(impl_->mutex);
    impl_->process.reset();
    impl_->pending.clear();
    impl_->pending_bytes = 0;
}
} // namespace cymcp
