#include <cstdlib>
#include <cymcp/core.hpp>
#include <iostream>
#include <iterator>
#ifdef CYMCP_FIXTURE_HTTP
#include <fstream>
#include <httplib.h>
#endif
using namespace cymcp;
void send(Json value) {
    std::cout << value.dump() << '\n' << std::flush;
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int n = 1; n < argc; ++n)
        args.push_back(path_utf8(std::filesystem::path(argv[n])));
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
#endif
    auto has = [&](const std::string& s) {
        return std::find(args.begin(), args.end(), s) != args.end();
    };
    if (has("--silent")) {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        return 0;
    }
    if (has("--overflow")) {
        std::cout << std::string(2 * 1024 * 1024, 'x') << '\n' << std::flush;
        return 0;
    }
    if (has("--empty-lines")) {
        std::cerr << std::string(1024 * 1024, '\n') << std::flush;
        return 0;
    }
    if (has("--echo-args")) {
        send(args);
        return 0;
    }
    if (has("--extension-fixture")) {
        auto argument = [&](const std::string& key) {
            auto found = std::find(args.begin(), args.end(), key);
            return found == args.end() || ++found == args.end() ? std::string() : *found;
        };
        bool buddy = has("--buddy");
        bool acp = has("--acp-fixture");
        if (has("--version")) {
            std::cout << "fixture 1.0.0\n";
            return 0;
        }
        if (has("--list-models") || has("models")) {
            if (has("--fixture-text-models"))
                std::cout << "Human readable catalogue\n";
            else
                send({{"models", {"fixture-model", "fixture-other"}}});
            return 0;
        }
        if (acp) {
            if (has("--devin-fixture") && (argument("--permission-mode") !=
                                               (has("--fixture-allow") ? "dangerous" : "normal") ||
                                           argument("--respect-workspace-trust") !=
                                               (has("--fixture-allow") ? "false" : "true") ||
                                           !has("acp")))
                return 49;
            std::string sid = "acp-fixture-session", model = "fixture-model", mode = "ask";
            auto config_options = [&] {
                return Json::array(
                    {{{"id", "model"},
                      {"category", "model"},
                      {"type", "select"},
                      {"currentValue", model},
                      {"options", Json::array({{{"value", "fixture-model"}, {"name", "First"}},
                                               {{"value", "fixture-other"}, {"name", "Other"}}})}},
                     {{"id", "mode"},
                      {"category", "mode"},
                      {"type", "select"},
                      {"currentValue", mode},
                      {"options", Json::array({{{"value", "ask"}, {"name", "Ask"}},
                                               {{"value", "plan"}, {"name", "Plan"}}})}}});
            };
            auto update = [&](Json data, std::string native = "") {
                send(
                    {{"jsonrpc", "2.0"},
                     {"method", "session/update"},
                     {"params", {{"sessionId", native.empty() ? sid : native}, {"update", data}}}});
            };
            Json pending;
            for (std::string input; std::getline(std::cin, input);) {
                auto message = parse_json(input);
                if (message.contains("result") && message.at("id") == "permission-1") {
                    auto outcome = message.at("result").at("outcome");
                    auto chosen = outcome.value("optionId", std::string());
                    bool allow = has("--fixture-allow");
                    if (chosen != (allow ? "allow-once" : "reject-once"))
                        return 50;
                    send({{"jsonrpc", "2.0"},
                          {"id", pending},
                          {"result", {{"stopReason", "end_turn"}}}});
                    continue;
                }
                if (message.contains("error") && message.at("id") == "unsupported-1") {
                    if (message.at("error").at("code") != -32601)
                        return 51;
                    send({{"jsonrpc", "2.0"},
                          {"id", pending},
                          {"result", {{"stopReason", "end_turn"}}}});
                    continue;
                }
                if (message.contains("error") && message.at("id") == "permission-1" &&
                    has("--fixture-foreign-permission")) {
                    if (message.at("error").at("code") != -32602)
                        return 56;
                    send({{"jsonrpc", "2.0"},
                          {"id", pending},
                          {"result", {{"stopReason", "end_turn"}}}});
                    continue;
                }
                auto method = message.value("method", std::string());
                auto params = message.value("params", Json::object());
                if (method == "session/cancel")
                    return 0;
                Json result = Json::object();
                if (method == "initialize") {
                    if (has("--fixture-silent-connect")) {
                        std::this_thread::sleep_for(std::chrono::minutes(5));
                        return 0;
                    }
                    if (params.at("protocolVersion") != 1 ||
                        params.at("clientCapabilities").at("terminal") != false)
                        return 52;
                    result = {
                        {"protocolVersion", has("--fixture-version2") ? 2 : 1},
                        {"agentCapabilities", {{"loadSession", !has("--fixture-no-resume")}}}};
                } else if (method == "session/new" || method == "session/load") {
                    if (has("--fixture-auth")) {
                        send({{"jsonrpc", "2.0"},
                              {"id", message.at("id")},
                              {"error", {{"code", -32000}, {"message", "Private account error"}}}});
                        continue;
                    }
                    if (!std::filesystem::u8path(params.at("cwd").get<std::string>())
                             .is_absolute() ||
                        !params.at("mcpServers").is_array())
                        return 53;
                    if (method == "session/load") {
                        if (params.at("sessionId") != sid)
                            return 54;
                        update({{"sessionUpdate", "agent_message_chunk"},
                                {"content", {{"type", "text"}, {"text", "OLD HISTORY"}}}});
                    }
                    if (has("--fixture-wrong-session"))
                        result["sessionId"] = "different-session";
                    else if (method == "session/new")
                        result["sessionId"] = sid;
                    if (!has("--fixture-no-models")) {
                        if (has("--fixture-legacy-models"))
                            result["models"] = {
                                {"currentModelId", model},
                                {"availableModels",
                                 Json::array({{{"modelId", "fixture-model"}, {"name", "First"}},
                                              {{"modelId", "fixture-other"}, {"name", "Other"}}})}};
                        else
                            result["configOptions"] = config_options();
                    }
                } else if (method == "session/set_config_option") {
                    if (params.at("configId") == "model")
                        model = params.at("value").get<std::string>();
                    else
                        mode = params.at("value").get<std::string>();
                    result["configOptions"] = config_options();
                } else if (method == "session/set_model")
                    model = params.at("modelId").get<std::string>();
                else if (method == "session/prompt") {
                    if (params.at("sessionId") != sid)
                        return 55;
                    if (has("--fixture-silent-turn"))
                        continue;
                    auto text = params.at("prompt").at(0).at("text").get<std::string>();
                    update({{"sessionUpdate", "agent_message_chunk"},
                            {"content", {{"type", "text"}, {"text", "WRONG SESSION"}}}},
                           "other-session");
                    update({{"sessionUpdate", "agent_message_chunk"},
                            {"content", {{"type", "text"}, {"text", text}}}});
                    update({{"sessionUpdate", "tool_call"},
                            {"toolCallId", "tool-1"},
                            {"status", "in_progress"}});
                    update({{"sessionUpdate", "tool_call_update"},
                            {"toolCallId", "tool-1"},
                            {"status", "completed"}});
                    update({{"sessionUpdate", "usage_update"}, {"used", 12}, {"size", 8192}});
                    if (has("--fixture-truncated"))
                        return 0;
                    if (has("--fixture-permission") || has("--fixture-foreign-permission")) {
                        pending = message.at("id");
                        send({{"jsonrpc", "2.0"},
                              {"id", "permission-1"},
                              {"method", "session/request_permission"},
                              {"params",
                               {{"sessionId",
                                 has("--fixture-foreign-permission") ? "other-session" : sid},
                                {"toolCall", {{"toolCallId", "tool-2"}}},
                                {"options",
                                 Json::array(
                                     {{{"optionId", "allow-always"}, {"kind", "allow_always"}},
                                      {{"optionId", "allow-once"}, {"kind", "allow_once"}},
                                      {{"optionId", "reject-once"}, {"kind", "reject_once"}}})}}}});
                        continue;
                    }
                    if (has("--fixture-unsupported-request")) {
                        pending = message.at("id");
                        send({{"jsonrpc", "2.0"},
                              {"id", "unsupported-1"},
                              {"method", "fs/write_text_file"},
                              {"params",
                               {{"sessionId", sid},
                                {"path", "/must-not-write"},
                                {"content", "blocked"}}}});
                        continue;
                    }
                    if (has("--fixture-fail")) {
                        send({{"jsonrpc", "2.0"},
                              {"id", message.at("id")},
                              {"error", {{"code", -32603}, {"message", "failure"}}}});
                        continue;
                    }
                    result["stopReason"] = has("--fixture-max-turns") ? "max_turn_requests"
                                           : has("--fixture-refusal") ? "refusal"
                                                                      : "end_turn";
                }
                send({{"jsonrpc", "2.0"},
                      {"id", has("--fixture-wrong-rpc") ? Json(9999) : message.at("id")},
                      {"result", result}});
            }
            return 0;
        }
        bool allow = has("--fixture-allow");
        if (buddy) {
            if (argument("--permission-mode") != (allow ? "bypassPermissions" : "dontAsk") ||
                !has("--strict-mcp-config"))
                return 44;
            const char* background = std::getenv("CODEBUDDY_CODE_DISABLE_BACKGROUND_TASKS");
            if (!background || std::string(background) != "1")
                return 45;
        } else if (allow != has("--yolo") || (!allow && !has("--plan")))
            return 46;
        if (has("--fixture-silent-turn")) {
            std::this_thread::sleep_for(std::chrono::minutes(5));
            return 0;
        }
        std::string prompt((std::istreambuf_iterator<char>(std::cin)), {});
        if (buddy)
            prompt =
                parse_json(prompt).at("message").at("content").at(0).at("text").get<std::string>();
        std::string sid = "extension-fixture-session";
        auto resume = argument("--resume");
        if (!resume.empty() && resume != sid)
            return 47;
        if (has("--fixture-wrong-session"))
            sid = "different-session";
        if (buddy) {
            send({{"type", "system"}, {"subtype", "init"}, {"session_id", sid}});
            for (int n = 0; n < 2; ++n)
                send({{"type", "assistant"},
                      {"session_id", sid},
                      {"message",
                       {{"id", "message-1"},
                        {"content", Json::array({{{"type", "text"}, {"text", "Partial"}}})}}}});
        } else {
            send({{"type", "event"}, {"event", {{"type", "run_start"}, {"sessionId", sid}}}});
            send({{"type", "event"}, {"event", {{"type", "text_delta"}, {"delta", "Partial"}}}});
        }
        if (has("--fixture-truncated"))
            return 0;
        if (has("--fixture-invalid-json")) {
            std::cout << "invalid-json\n";
            return 0;
        }
        Json result = {{"type", "result"},
                       {"subtype", has("--fixture-fail")        ? "error"
                                   : has("--fixture-max-turns") ? "max_turns"
                                                                : "success"},
                       {buddy ? "session_id" : "sessionId", sid},
                       {buddy ? "result" : "finalText", prompt},
                       {"usage", {{"output_tokens", 3}}}};
        if (has("--fixture-denied"))
            result["denied_actions"] = {"blocked action"};
        send(result);
        return has("--fixture-crash") ? 9 : 0;
    }
    if (has("--agent-fixture")) {
        bool agy = has("--agy");
        if (has("--version")) {
            std::cout << (has("--fixture-version2") ? "2.0.0" : "1.18.30") << '\n';
            return 0;
        }
        if (has("models")) {
            if (agy)
                send({{"models", {"fixture-model", "fixture-other"}}});
            else
                std::cout << "fixture/model\nfixture/other\n";
            return 0;
        }
        if (has("--fixture-silent-turn")) {
            std::this_thread::sleep_for(std::chrono::minutes(5));
            return 0;
        }
        auto argument = [&](const std::string& key) {
            auto found = std::find(args.begin(), args.end(), key);
            return found == args.end() || ++found == args.end() ? std::string() : *found;
        };
        bool allow = has("--fixture-allow");
        std::string prompt;
        if (agy) {
            if (has("--dangerously-skip-permissions") != allow ||
                (!allow && (!has("--sandbox") || argument("--mode") != "plan")))
                return 41;
            std::string input((std::istreambuf_iterator<char>(std::cin)), {});
            prompt = parse_json(input).at("message").at("content").get<std::string>();
        } else {
            const char* settings = std::getenv("OPENCODE_CONFIG_CONTENT");
            if (!settings ||
                parse_json(settings).at("permission").at("*") != (allow ? "allow" : "deny"))
                return 42;
            prompt = argument("--");
        }
        std::string sid = "agent-fixture-session";
        auto resume = argument(agy ? "--conversation" : "--session");
        if (!resume.empty() && resume != sid)
            return 43;
        if (has("--fixture-wrong-session"))
            sid = "different-session";
        if (agy) {
            send({{"event", "init"}, {"init", {{"conversation_id", sid}}}});
            send({{"event", "step_update"}, {"step_update", {{"text_delta", "Partial "}}}});
            if (has("--fixture-truncated"))
                return 0;
            send({{"event", "result"},
                  {"result",
                   {{"conversation_id", sid},
                    {"status", has("--fixture-fail") ? "ERROR" : "SUCCESS"},
                    {"response", prompt},
                    {"usage", {{"output_tokens", 2}}}}}});
        } else {
            send({{"type", "text"}, {"sessionID", sid}, {"part", {{"text", prompt}}}});
            if (has("--fixture-truncated"))
                return 0;
            if (has("--fixture-fail")) {
                send({{"type", "error"}, {"error", {{"message", "Provider refusal"}}}});
                return 0;
            }
            send({{"type", "step_finish"},
                  {"sessionID", sid},
                  {"part", {{"reason", "stop"}, {"tokens", {{"output", 2}}}}}});
        }
        return has("--fixture-crash") ? 9 : 0;
    }
#ifdef CYMCP_FIXTURE_HTTP
    if (has("--engine-fixture")) {
        auto option = [&](std::string name) {
            auto it = std::find(args.begin(), args.end(), name);
            return it == args.end() || ++it == args.end() ? std::string() : *it;
        };
        std::ifstream credentials(option("--api-key-file"));
        std::string token;
        credentials >> token;
        httplib::Server server;
        server.Get("/v1/models", [&](const auto& req, auto& res) {
            if (req.get_header_value("Authorization") != "Bearer " + token) {
                res.status = 401;
                return;
            }
            res.set_content("{\"data\":[{\"id\":\"fixture-local\"}]}", "application/json");
        });
        server.Post("/v1/chat/completions", [](const auto&, auto& res) {
            res.set_content("data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Local "
                            "fixture\"},\"finish_reason\":null}]}\n\ndata: "
                            "{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}"
                            "\n\ndata: [DONE]\n\n",
                            "text/event-stream");
        });
        return server.listen("127.0.0.1", std::stoi(option("--port"))) ? 0 : 9;
    }
#endif
    if (has("--claude-fixture")) {
        if (has("auth")) {
            send({{"loggedIn", true}});
            return 0;
        }
        std::string prompt((std::istreambuf_iterator<char>(std::cin)), {});
        send({{"type", "system"}, {"session_id", "claude-fixture-session"}});
        send({{"type", "stream_event"},
              {"event", {{"delta", {{"type", "text_delta"}, {"text", "Hello "}}}}}});
        send({{"type", "result"},
              {"subtype", has("--fixture-fail") ? "error_during_execution" : "success"},
              {"is_error", has("--fixture-fail")},
              {"result", "Hello fixture"},
              {"usage", {{"input_tokens", 1}, {"output_tokens", 2}}}});
        return has("--fixture-crash") ? 7 : 0;
    }
    if (has("--codex-fixture")) {
        std::string line;
        int turn = 0;
        std::string active;
        auto complete = [&] {
            send({{"method", "item/agentMessage/delta"},
                  {"params",
                   {{"threadId", "fixture-thread"},
                    {"turnId", active},
                    {"delta", "Hello fixture"}}}});
            send({{"method", "item/completed"},
                  {"params",
                   {{"threadId", "fixture-thread"},
                    {"turnId", active},
                    {"item",
                     {{"id", "message-" + active},
                      {"type", "agentMessage"},
                      {"text", "Hello fixture"}}}}}});
            send({{"method", "turn/completed"},
                  {"params",
                   {{"threadId", "fixture-thread"},
                    {"turn", {{"id", active}, {"status", "completed"}}}}}});
        };
        while (std::getline(std::cin, line)) {
            auto value = Json::parse(line);
            auto method = value.value("method", std::string());
            auto id = value.value("id", Json());
            Json result = Json::object();
            if (method.empty() && value.value("id", Json()) == 9000) {
                if (!value["result"].value("success", false))
                    return 8;
                complete();
                continue;
            }
            if (method == "initialized")
                continue;
            if (method == "account/read")
                result = {{"account", {{"type", "fixture"}}}};
            else if (method == "model/list")
                result = {{"data", Json::array({{{"id", "fixture-model"}}})},
                          {"nextCursor", nullptr}};
            else if (method == "thread/start" || method == "thread/resume")
                result = {{"thread", {{"id", "fixture-thread"}}}, {"model", "fixture-model"}};
            else if (method == "turn/start") {
                active = "turn-" + std::to_string(++turn);
                send({{"id", id}, {"result", {{"turn", {{"id", active}}}}}});
                auto prompt = value["params"]["input"][0]["text"].get<std::string>();
                if (prompt == "silent")
                    continue;
                if (prompt == "tool") {
                    send({{"id", 9000},
                          {"method", "item/tool/call"},
                          {"params",
                           {{"threadId", "fixture-thread"},
                            {"turnId", active},
                            {"tool", "sum"},
                            {"arguments", {{"a", 2}, {"b", 3}}}}}});
                    continue;
                }
                complete();
                continue;
            } else if (method == "turn/interrupt") {
                send({{"id", id}, {"result", Json::object()}});
                send({{"method", "turn/completed"},
                      {"params",
                       {{"threadId", "fixture-thread"},
                        {"turn", {{"id", active}, {"status", "interrupted"}}}}}});
                continue;
            }
            send({{"id", id}, {"result", result}});
        }
        return 0;
    }
    return 2;
}
