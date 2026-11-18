#include "mcp_headers.hpp"
#include <cymcp/mcp.hpp>
#include <optional>
#include <set>
namespace cymcp {
namespace {
using mcp_http::modern;
using mcp_http::legacy;
using mcp_http::version_key;
constexpr auto caps_key = "io.modelcontextprotocol/clientCapabilities";
Json rpc_error(const Json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}
Json meta() {
    return {{version_key, modern},
            {caps_key, Json::object()},
            {"io.modelcontextprotocol/clientInfo", {{"name", "CyMCPSDK"}, {"version", version}}}};
}
struct HeaderParameter {
    std::string name, type;
    Json::json_pointer path;
};
using HeaderParameters = std::vector<HeaderParameter>;
bool collect_headers(const Json& schema, const Json::json_pointer& path, bool reachable,
                     HeaderParameters& parameters, std::set<std::string>& names) {
    if (!schema.is_object())
        return true;
    if (const auto it = schema.find("x-mcp-header"); it != schema.end()) {
        if (!reachable || path.empty() || !it->is_string())
            return false;
        auto name = it->get<std::string>();
        constexpr auto tokens =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'*+-.^_`|~";
        if (name.empty() || name.find_first_not_of(tokens) != std::string::npos)
            return false;
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto type = schema.value("type", Json());
        if (!names.insert(lower).second ||
            (type != "string" && type != "integer" && type != "boolean"))
            return false;
        parameters.push_back({"Mcp-Param-" + name, type.get<std::string>(), path});
    }
    for (const auto& [key, child] : schema.items()) {
        if (key == "properties" || key == "patternProperties" || key == "$defs" ||
            key == "definitions" || key == "dependentSchemas") {
            if (!child.is_object())
                continue;
            for (const auto& [name, property] : child.items())
                if (!collect_headers(property, path / name, reachable && key == "properties",
                                     parameters, names))
                    return false;
        } else if (key == "items" || key == "prefixItems" || key == "allOf" || key == "anyOf" ||
                   key == "oneOf" || key == "not" || key == "if" || key == "then" ||
                   key == "else" || key == "additionalProperties" || key == "additionalItems" ||
                   key == "contains" || key == "propertyNames" || key == "unevaluatedProperties" ||
                   key == "unevaluatedItems") {
            if (child.is_array()) {
                for (const auto& item : child)
                    if (!collect_headers(item, path, false, parameters, names))
                        return false;
            } else if (!collect_headers(child, path, false, parameters, names))
                return false;
        }
    }
    return true;
}
std::string parameter_value(const HeaderParameter& header, const Json& value) {
    if (header.type == "string" && value.is_string())
        return mcp_http::header_value(value.get_ref<const std::string&>());
    if (header.type == "boolean" && value.is_boolean())
        return value.dump();
    constexpr std::int64_t safe = 9007199254740991LL;
    if (header.type == "integer" && value.is_number_integer() &&
        (value.is_number_unsigned()
             ? value.get<std::uint64_t>() <= static_cast<std::uint64_t>(safe)
             : value.get<std::int64_t>() >= -safe && value.get<std::int64_t>() <= safe))
        return value.dump();
    throw Error("InvalidParameters", "Cannot represent tool parameter in " + header.name);
}
} // namespace
struct McpServer::ActiveCall {
    McpServer& server;
    RequestKey key;
    CallContext context;
    bool registered = false;
    ActiveCall(McpServer& server, RequestKey key, CallContext context)
        : server(server), key(std::move(key)), context(std::move(context)) {}
    ~ActiveCall() {
        if (registered) {
            std::lock_guard lock(server.mutex_);
            server.active_.erase(key);
        }
    }
};
McpServer::McpServer(const Registry& registry, std::string name)
    : registry_(registry), name_(std::move(name)) {}
void McpServer::cancel_all() {
    std::lock_guard lock(mutex_);
    for (const auto& [id, cancel] : active_)
        cancel.cancel();
}
std::shared_ptr<McpServer::ActiveCall> McpServer::reserve(const Json& request,
                                                          const CallContext& input, Json& error) {
    if (!request.is_object() || request.value("method", Json()) != "tools/call" ||
        !request.contains("id") ||
        !(request["id"].is_string() || request["id"].is_number_integer()))
        return {};
    const auto& id = request["id"];
    auto call = std::make_shared<ActiveCall>(
        *this, RequestKey{{input.principal, input.transport_scope}, id.dump()}, input);
    call->context.cancellation = input.cancellation.child();
    call->context.request_id = id.dump();
    std::lock_guard lock(mutex_);
    if (active_.size() >= 128)
        error = rpc_error(id, -32001, "Server busy");
    else if (!active_.emplace(call->key, call->context.cancellation).second)
        error = rpc_error(id, -32600, "Request ID already active");
    else
        call->registered = true;
    return call;
}
Json McpServer::handle(const Json& request, const CallContext& input) {
    Json error;
    auto call = reserve(request, input, error);
    if (!error.is_null())
        return error;
    return dispatch(request, call ? call->context : input);
}
Json McpServer::dispatch(const Json& request, const CallContext& input) {
    Json id = nullptr;
    if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0" ||
        !request.contains("method") || !request["method"].is_string())
        return rpc_error(id, -32600, "Invalid JSON-RPC request");
    bool notification = !request.contains("id");
    if (!notification) {
        id = request["id"];
        if (!id.is_string() && !id.is_number_integer())
            return rpc_error(nullptr, -32600, "Invalid request ID");
    }
    auto method = request["method"].get<std::string>();
    Json params = request.value("params", Json::object());
    if (!params.is_object())
        return notification ? Json() : rpc_error(id, -32602, "Parameters must be an object");
    if (notification) {
        if (method == "notifications/cancelled" && params.contains("requestId")) {
            std::lock_guard lock(mutex_);
            auto it = active_.find(
                {{input.principal, input.transport_scope}, params["requestId"].dump()});
            if (it != active_.end())
                it->second.cancel();
        }
        return nullptr;
    }
    try {
        auto metadata = params.value("_meta", Json::object());
        if (!metadata.is_object())
            return rpc_error(id, -32602, "Invalid metadata");
        bool current = metadata.contains(version_key);
        if (current) {
            if (metadata[version_key] != modern) {
                auto reply = rpc_error(id, -32022, "Unsupported protocol version");
                reply["error"]["data"] = {
                    {"supported", Json::array({modern, legacy, mcp_http::june_2025})},
                    {"requested", metadata[version_key]}};
                return reply;
            }
            if (!metadata.contains(caps_key) || !metadata[caps_key].is_object())
                return rpc_error(id, -32602, "Client capabilities metadata required");
        }
        Json result;
        if (method == "server/discover") {
            if (!current)
                return rpc_error(id, -32602, "Discovery requires version metadata");
            result = {
                {"supportedVersions", Json::array({modern, legacy, mcp_http::june_2025})},
                {"capabilities", {{"tools", Json::object()}}},
                {"_meta",
                 {{"io.modelcontextprotocol/serverInfo", {{"name", name_}, {"version", version}}}}},
                {"ttlMs", 0},
                {"cacheScope", "private"}};
        } else if (method == "initialize") {
            if (!mcp_http::legacy_version(params.value("protocolVersion", std::string())))
                return rpc_error(id, -32602, "Supported legacy versions: 2025-11-25, 2025-06-18");
            {
                std::lock_guard lock(mutex_);
                initialized_ = true;
            }
            result = {{"protocolVersion", params["protocolVersion"]},
                      {"capabilities", {{"tools", Json::object()}}},
                      {"serverInfo", {{"name", name_}, {"version", version}}}};
        } else {
            if (!current) {
                std::lock_guard lock(mutex_);
                if (!initialized_)
                    return rpc_error(id, -32000, "Initialize the connection first");
            }
            if (method == "ping")
                result = Json::object();
            else if (method == "tools/list") {
                if (params.contains("cursor"))
                    return rpc_error(id, -32602,
                                     "This server returns its bounded tool catalogue in one page");
                result = {{"tools", registry_.describe()}};
                if (current) {
                    result["ttlMs"] = 0;
                    result["cacheScope"] = "private";
                }
            } else if (method == "tools/call") {
                if (!params.contains("name") || !params["name"].is_string() ||
                    (params.contains("arguments") && !params["arguments"].is_object()))
                    return rpc_error(id, -32602, "Tool name and object arguments required");
                try {
                    auto value = registry_.invoke(params["name"].get<std::string>(),
                                                  params.value("arguments", Json::object()), input);
                    result = {
                        {"content", Json::array({{{"type", "text"}, {"text", value.dump()}}})},
                        {"isError", false}};
                    if (value.is_object())
                        result["structuredContent"] = value;
                } catch (const Error& e) {
                    result = {
                        {"content",
                         Json::array(
                             {{{"type", "text"},
                               {"text", Json({{"code", e.code}, {"message", e.what()}}).dump()}}})},
                        {"isError", true}};
                }
            } else
                return rpc_error(id, -32601, "Method not implemented");
        }
        if (current)
            result["resultType"] = "complete";
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
    } catch (const Json::exception&) {
        return rpc_error(id, -32602, "Invalid method parameters");
    } catch (const Error& e) {
        return rpc_error(id, -32603, e.what());
    } catch (...) {
        return rpc_error(id, -32603, "Internal error");
    }
}
void McpServer::serve_stdio(std::istream& in, std::ostream& out, std::size_t workers) {
    std::mutex output;
    CallContext base;
    base.transport_scope = random_token(16);
    auto send = [&](const Json& result, const Cancellation* cancelled = nullptr) {
        if (!result.is_null()) {
            std::lock_guard lock(output);
            if (!cancelled || !cancelled->cancelled())
                out << result.dump() << '\n' << std::flush;
        }
    };
    Executor executor(workers);
    std::string line;
    line.reserve(4096);
    bool oversized = false;
    char ch;
    while (in.get(ch)) {
        if (ch != '\n') {
            if (line.size() < provider_message_limit)
                line += ch;
            else
                oversized = true;
            continue;
        }
        if (oversized) {
            send(rpc_error(nullptr, -32700, "Message exceeds the memory guard"));
            line.clear();
            oversized = false;
            continue;
        }
        if (line.empty())
            continue;
        Json request;
        try {
            request = parse_json(line, provider_message_limit);
        } catch (...) {
            send(rpc_error(nullptr, -32700, "Parse error"));
            line.clear();
            continue;
        }
        line.clear();
        if (request.is_object() && request.value("jsonrpc", Json()) == "2.0" &&
            !request.contains("id") &&
            request.value("method", Json()) == "notifications/cancelled") {
            // Order accepting cancellation against the last possible reply on this stream.
            std::lock_guard lock(output);
            dispatch(request, base);
            continue;
        }
        Json error;
        auto call = reserve(request, base, error);
        if (!error.is_null())
            send(error);
        else if (call) {
            auto id = request.at("id");
            try {
                executor.post([&, request = std::move(request), call] {
                    if (!call->context.cancellation.cancelled())
                        send(dispatch(request, call->context), &call->context.cancellation);
                });
            } catch (const Error&) {
                send(rpc_error(id, -32001, "Server busy"));
            }
        } else
            send(dispatch(request, base));
    }
    base.cancellation.cancel();
}
struct McpClient::Impl {
    ProcessOptions options;
    std::unique_ptr<Process> process;
    std::string url, token, protocol = modern;
    std::string session_id;
    std::int64_t counter = 0;
    bool connected = false;
    std::mutex mutex;
    using HeaderCatalogue = std::map<std::string, std::optional<HeaderParameters>>;
    std::optional<HeaderCatalogue> tool_headers;
    void close() noexcept {
        try {
            Cancellation grace;
            grace.deadline = Clock::now() + std::chrono::seconds(1);
            if (process) {
                process->close_input();
                ProcessLine discarded;
                // A server may flush diagnostics or answer an earlier cancelled request
                // during shutdown. Drain both pipes so a full output queue cannot block exit.
                while (process->next(discarded, grace)) {
                }
                process->wait(grace);
            }
#ifdef CYMCP_HAS_HTTP
            else if (!session_id.empty() && mcp_http::legacy_version(protocol)) {
                HttpRequest req;
                req.method = "DELETE";
                req.url = url;
                req.headers = {{"Authorization", "Bearer " + token},
                               {"MCP-Protocol-Version", protocol},
                               {"Mcp-Session-Id", session_id}};
                req.timeout = std::chrono::seconds(1);
                req.max_response_bytes = 4096;
                HttpClient().perform(req, grace);
            }
#endif
        } catch (...) {
            // A peer can ignore EOF or refuse DELETE. Owned processes are stopped below.
        }
        process.reset();
        session_id.clear();
        connected = false;
    }
    Json list_tools(const Cancellation&);
    Json exchange(std::string method, Json params, const Cancellation& cancel,
                  bool notify = false) {
        if (protocol == modern)
            params["_meta"] = meta();
        Json message = {{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}};
        auto id = ++counter;
        if (!notify)
            message["id"] = id;
        Json reply;
        if (process) {
            cancel.check();
            try {
                process->write(message.dump() + "\n", cancel);
            } catch (...) {
                // A partial JSON line cannot be resumed safely on this stream.
                connected = false;
                process.reset();
                throw;
            }
            if (notify)
                return nullptr;
            ProcessLine line;
            try {
                while (process->next(line, cancel)) {
                    if (line.stderr_stream)
                        continue;
                    Json next = parse_json(line.text, provider_message_limit);
                    // JSON-RPC answers an undeterminable request with id null: on this ordered
                    // channel it is the reply to the request just written.
                    if (next.is_object() && next.contains("error") && next.contains("id") &&
                        next.at("id").is_null() && !next.contains("method"))
                        next["id"] = id;
                    if (next.contains("method") && next.contains("id")) {
                        process->write(
                            rpc_error(next["id"], -32601, "Client request unsupported").dump() +
                            "\n");
                        continue;
                    }
                    if (next.value("id", Json()) == id) {
                        reply = std::move(next);
                        break;
                    }
                }
            } catch (const Error& e) {
                if (e.code == "Cancelled" || e.code == "Timeout") {
                    Json stop = {{"requestId", id}};
                    if (protocol == modern)
                        stop["_meta"] = meta();
                    const Json notification = {{"jsonrpc", "2.0"},
                                               {"method", "notifications/cancelled"},
                                               {"params", std::move(stop)}};
                    Cancellation delivery;
                    delivery.deadline = Clock::now() + std::chrono::seconds(1);
                    try {
                        process->write(notification.dump() + "\n", delivery);
                    } catch (...) {
                        connected = false;
                        process.reset();
                    }
                }
                throw;
            }
        } else {
#ifdef CYMCP_HAS_HTTP
            HttpRequest req;
            req.method = "POST";
            req.url = url;
            req.body = message.dump();
            // A tool may legitimately run for a long time: only the caller's cancellation and
            // deadline bound the exchange.
            req.timeout = std::chrono::milliseconds(0);
            req.max_response_bytes = provider_message_limit;
            req.headers = {{"Content-Type", "application/json"},
                           {"Accept", "application/json, text/event-stream"},
                           {"Authorization", "Bearer " + token},
                           {"MCP-Protocol-Version", protocol}};
            if (protocol == modern) {
                req.headers["Mcp-Method"] = method;
                const auto& fields = message.at("params");
                if (auto field = mcp_http::name_field(method))
                    req.headers["Mcp-Name"] =
                        mcp_http::header_value(fields.at(field).get_ref<const std::string&>());
                if (method == "tools/call" && tool_headers) {
                    auto tool = tool_headers->find(fields.at("name").get_ref<const std::string&>());
                    if (tool != tool_headers->end()) {
                        if (!tool->second)
                            throw Error("InvalidTool", "Tool has invalid HTTP header annotations");
                        const auto& arguments = fields.at("arguments");
                        for (const auto& header : *tool->second)
                            if (arguments.contains(header.path))
                                req.headers[header.name] =
                                    parameter_value(header, arguments.at(header.path));
                    }
                }
            }
            if (!session_id.empty())
                req.headers["Mcp-Session-Id"] = session_id;
            HttpResponse response;
            try {
                response = HttpClient().perform(req, cancel);
            } catch (const Error& e) {
                if (mcp_http::legacy_version(protocol) && method == "tools/call" &&
                    (e.code == "Cancelled" || e.code == "Timeout")) {
                    // Legacy peers may continue work after the request socket closes.
                    Cancellation delivery;
                    delivery.deadline = Clock::now() + std::chrono::seconds(1);
                    try {
                        exchange("notifications/cancelled", {{"requestId", id}}, delivery, true);
                    } catch (const Error&) {
                        // Preserve the caller's original cancellation or timeout.
                    }
                }
                throw;
            }
            if (method == "initialize" && mcp_http::legacy_version(protocol) &&
                response.status >= 200 && response.status < 300 &&
                response.headers.count("mcp-session-id"))
                session_id = response.headers.at("mcp-session-id");
            if (notify && response.status == 202)
                return nullptr;
            if (response.status < 200 || response.status >= 300) {
                try {
                    reply = parse_json(response.body, provider_message_limit);
                } catch (...) {
                    reply = nullptr;
                }
                // Represent any rejection as a JSON-RPC error for this request, so a server
                // that refuses server/discover (e.g. "not initialized", id null, or a plain HTTP
                // error) still lets connect() fall back to the legacy handshake.
                if (!reply.is_object() || !reply.contains("error"))
                    reply = rpc_error(id, -32600, "MCP HTTP request rejected");
                else if (!reply.contains("id") || reply.at("id").is_null())
                    reply["id"] = id;
            } else if (response.headers["content-type"].find("text/event-stream") !=
                       std::string::npos) {
                std::istringstream events(response.body);
                std::string line, data;
                while (std::getline(events, line)) {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.rfind("data:", 0) == 0)
                        data += line.substr(5) + "\n";
                    else if (line.empty() && !data.empty()) {
                        auto next = parse_json(data, provider_message_limit);
                        if (next.is_object() && next.contains("error") &&
                            next.value("id", Json()).is_null())
                            next["id"] = id;
                        if (next.value("id", Json()) == id)
                            reply = std::move(next);
                        data.clear();
                    }
                }
            } else {
                reply = parse_json(response.body, provider_message_limit);
                if (reply.is_object() && reply.contains("error") &&
                    reply.value("id", Json()).is_null())
                    reply["id"] = id;
            }
#else
            throw Error("Unsupported", "HTTP module disabled");
#endif
        }
        if (!reply.is_object() || reply.value("id", Json()) != id)
            throw Error("UnexpectedEOF", "No matching MCP response");
        return reply;
    }
    Json call(std::string method, Json params, const Cancellation& cancel) {
        auto response = exchange(std::move(method), std::move(params), cancel);
        if (response.contains("error"))
            throw Error("RemoteError", response["error"].dump());
        if (!response.contains("result"))
            throw Error("ProtocolError", "Missing MCP result");
        return response["result"];
    }
};
McpClient::McpClient(ProcessOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
}
#ifdef CYMCP_HAS_HTTP
McpClient::McpClient(std::string url, std::string token) : impl_(std::make_unique<Impl>()) {
    parse_url(url);
    impl_->url = std::move(url);
    impl_->token = std::move(token);
}
#endif
McpClient::~McpClient() {
    impl_->close();
}
void McpClient::connect(const Cancellation& input) {
    auto cancel = input;
    if (cancel.deadline == Clock::time_point::max())
        cancel.deadline = Clock::now() + std::chrono::seconds(30);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (p.connected)
        throw Error("InvalidState", "MCP client already connected");
    p.protocol = modern;
    p.tool_headers.reset();
    try {
        if (p.url.empty())
            p.process = std::make_unique<Process>(p.options);
        auto discovery = p.exchange("server/discover", Json::object(), cancel);
        if (discovery.contains("error")) {
            if (discovery["error"].value("code", 0) == -32022)
                throw Error("Unsupported",
                            "MCP server does not support the selected modern revision");
            p.protocol = legacy;
            const auto negotiated =
                p.call("initialize",
                       {{"protocolVersion", legacy},
                        {"capabilities", Json::object()},
                        {"clientInfo", {{"name", "CyMCPSDK"}, {"version", version}}}},
                       cancel);
            if (!negotiated.is_object() || !negotiated.contains("protocolVersion") ||
                !negotiated.at("protocolVersion").is_string())
                throw Error("ProtocolError", "Missing negotiated MCP protocol version");
            p.protocol = negotiated.at("protocolVersion").get<std::string>();
            if (!mcp_http::legacy_version(p.protocol))
                throw Error("Unsupported", "Peer negotiated an unsupported MCP protocol version");
            p.exchange("notifications/initialized", Json::object(), cancel, true);
        } else if (!discovery.contains("result") ||
                   !discovery["result"].contains("supportedVersions"))
            throw Error("ProtocolError", "Invalid discovery response");
        p.connected = true;
    } catch (...) {
        p.close();
        throw;
    }
}
Json McpClient::list_tools(const Cancellation& input) {
    auto cancel = input;
    if (cancel.deadline == Clock::time_point::max())
        cancel.deadline = Clock::now() + std::chrono::seconds(120);
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.connected)
        throw Error("InvalidState", "Connect MCP client first");
    return p.list_tools(cancel);
}
Json McpClient::Impl::list_tools(const Cancellation& cancel) {
    Json tools = Json::array();
    Json params = Json::object();
    std::set<std::string> seen;
    HeaderCatalogue headers;
    std::size_t count = 0;
    for (int page = 0; page < 1000; ++page) {
        auto result = call("tools/list", params, cancel);
        if (!result.contains("tools") || !result["tools"].is_array())
            throw Error("ProtocolError", "Missing tool catalogue");
        for (const auto& tool : result["tools"]) {
            if (++count > 10000)
                throw Error("ResponseTooLarge", "Tool catalogue exceeds limit");
            if (!url.empty() && protocol == modern) {
                if (!tool.is_object() || !tool.contains("name") || !tool.at("name").is_string() ||
                    !tool.contains("inputSchema"))
                    throw Error("ProtocolError", "Invalid MCP tool definition");
                auto& entry = headers[tool.at("name").get_ref<const std::string&>()];
                HeaderParameters parameters;
                std::set<std::string> names;
                if (!collect_headers(tool.at("inputSchema"), Json::json_pointer(), true, parameters,
                                     names)) {
                    entry.reset();
                    continue;
                }
                entry = std::move(parameters);
            }
            tools.push_back(tool);
        }
        auto cursor = result.value("nextCursor", std::string());
        if (cursor.empty()) {
            tool_headers = std::move(headers);
            return tools;
        }
        if (!seen.insert(cursor).second)
            throw Error("ProtocolError", "Cyclic tool pagination");
        params["cursor"] = cursor;
    }
    throw Error("ResponseTooLarge", "Too many catalogue pages");
}
Json McpClient::call_tool(const std::string& name, const Json& args, const Cancellation& input) {
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (!p.connected)
        throw Error("InvalidState", "Connect MCP client first");
    try {
        if (!p.url.empty() && p.protocol == modern && !p.tool_headers)
            p.list_tools(input);
        return p.call("tools/call", {{"name", name}, {"arguments", args}}, input);
    } catch (const Error& e) {
        if (e.code == "UnexpectedEOF" || e.code == "ProcessClosed")
            p.close();
        throw;
    }
}
} // namespace cymcp
