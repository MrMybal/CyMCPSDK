// Included after the local HTTP fixture in providers_tests.cpp.
void deferred_provider_tests() {
    auto tools = std::make_shared<Registry>(Policy::allow_all());
    int executions = 0;
    for (int n = 0; n < 80; ++n)
        tools->add({"command_" + std::to_string(n),
                    "Application command",
                    {{"type", "object"}},
                    {{"type", "object"}},
                    true,
                    [&](const auto&, const auto&) {
                        ++executions;
                        return Json{{"value", 42}};
                    }});
    Server server;
    server.http.Get("/models", [](const auto&, auto& response) {
        response.set_content("{\"data\":[{\"id\":\"fixture-model\"}],\"has_more\":false}",
                             "application/json");
    });
    server.http.Get("/api/tags", [](const auto&, auto& response) {
        response.set_content("{\"models\":[{\"name\":\"fixture-model\"}]}", "application/json");
    });
    server.http.Get("/key", [](const auto&, auto& response) {
        response.set_content("{}", "application/json");
    });
    int stage = 0;
    bool full = false;
    server.http.Post("/.*", [&](const auto& request, auto& response) {
        auto body = parse_json(request.body);
        check(body.at("tools").size() == (full ? 80 : 5),
              "API sends only the chosen tool exposure");
        const bool responses = request.path == "/responses";
        const bool anthropic = request.path == "/messages";
        const bool ollama = request.path == "/api/chat";
        const auto& history = body.at(responses ? "input" : "messages");
        if (stage > 0 && !full) {
            auto result = history.back();
            std::string serialized;
            if (responses)
                serialized = result.at("output");
            else if (anthropic)
                serialized = result.at("content").at(0).at("content");
            else
                serialized = result.at("content");
            const auto value = parse_json(serialized);
            check(stage == 1   ? value.at("tools").at(0).at("name") == "command_77"
                  : stage == 2 ? value.at("name") == "command_77" && value.contains("inputSchema")
                               : value.at("value") == 42,
                  "API preserves each lazy discovery and execution result");
        }
        const bool call = stage < 3 && !full;
        const auto name = stage == 0   ? "cy_tools_search"
                          : stage == 1 ? "cy_tools_describe"
                                       : "cy_tools_call";
        const Json args = stage == 0 ? Json{{"query", "command_77"}} : Json{{"name", "command_77"}};
        ++stage;
        std::string frames;
        if (responses) {
            Json output = call
                              ? Json::array({{{"type", "function_call"},
                                              {"name", name},
                                              {"call_id", "lazy"},
                                              {"arguments", args.dump()}}})
                              : Json::array({{{"type", "message"},
                                              {"role", "assistant"},
                                              {"content", Json::array({{{"type", "output_text"},
                                                                        {"text", "Complete"}}})}}});
            frames = sse({{"type", "response.completed"},
                          {"response", {{"status", "completed"}, {"output", output}}}});
        } else if (anthropic) {
            frames = sse({{"type", "message_start"}, {"message", Json::object()}}) +
                     sse({{"type", "content_block_start"},
                          {"index", 0},
                          {"content_block", call ? Json{{"type", "tool_use"},
                                                        {"id", "lazy"},
                                                        {"name", name},
                                                        {"input", args}}
                                                 : Json{{"type", "text"}, {"text", ""}}}});
            if (!call)
                frames += sse({{"type", "content_block_delta"},
                               {"index", 0},
                               {"delta", {{"type", "text_delta"}, {"text", "Complete"}}}});
            frames += sse({{"type", "content_block_stop"}, {"index", 0}}) +
                      sse({{"type", "message_delta"},
                           {"delta", {{"stop_reason", call ? "tool_use" : "end_turn"}}}}) +
                      sse({{"type", "message_stop"}});
        } else if (ollama) {
            Json message =
                call ? Json{{"content", ""},
                            {"tool_calls",
                             Json::array({{{"function", {{"name", name}, {"arguments", args}}}}})}}
                     : Json{{"content", "Complete"}};
            frames = Json({{"message", message}, {"done", false}}).dump() +
                     "\n{\"message\":{\"content\":\"\"},\"done\":true,\"done_reason\":\"stop\"}\n";
        } else {
            Json delta =
                call ? Json{{"tool_calls",
                             Json::array(
                                 {{{"index", 0},
                                   {"id", "lazy"},
                                   {"function", {{"name", name}, {"arguments", args.dump()}}}}})}}
                     : Json{{"content", "Complete"}};
            frames = sse({{"choices",
                           Json::array(
                               {{{"index", 0}, {"delta", delta}, {"finish_reason", nullptr}}})}}) +
                     sse({{"choices",
                           Json::array({{{"index", 0},
                                         {"delta", Json::object()},
                                         {"finish_reason", call ? "tool_calls" : "stop"}}})}}) +
                     "data: [DONE]\n\n";
        }
        response.set_content(frames, ollama ? "application/x-ndjson" : "text/event-stream");
    });
    server.start();
    ProviderRegistry providers;
    for (const auto* id : {"openai-compatible", "openai", "anthropic", "ollama", "openrouter"}) {
        stage = 0;
        ProviderConfig config;
        config.tools = tools;
        config.api_key = "fixture-key";
        config.settings = {
            {"base_url", server.url}, {"max_output_tokens", 256}, {"max_tool_rounds", 6}};
        Conversation chat(providers.create(id, config), "fixture-model");
        chat.connect();
        const auto before = executions;
        check(chat.send("Use command_77").text == "Complete" && executions == before + 1 &&
                  stage == 4,
              "API discovers, describes and invokes a real app command once using compact tools");
    }
    full = true;
    stage = 0;
    ProviderConfig config;
    config.tools = tools;
    config.settings = {{"base_url", server.url}, {"deferred_tools", false}};
    Conversation legacy(providers.create("openai-compatible", config), "fixture-model");
    legacy.connect();
    check(legacy.send("Hello").text == "Complete",
          "Explicit legacy mode sends all original tool definitions");
}
