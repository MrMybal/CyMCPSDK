#include "test.hpp"
#include <cymcp/ai.hpp>
#include <cymcp/performance.hpp>
#include <httplib.h>
using namespace cymcp;
void api_active_turn_budget() {
    httplib::Server server;
    std::thread worker;
    struct Stop {
        httplib::Server& s;
        std::thread& t;
        ~Stop() {
            s.stop();
            if (t.joinable())
                t.join();
        }
    } stop{server, worker};
    std::atomic_int requests{0}, executions{0};
    server.Post("/chat/completions", [&](const auto&, auto& response) {
        const bool call = ++requests == 1;
        Json delta =
            call ? Json{{"tool_calls",
                         Json::array({{{"index", 0},
                                       {"id", "write-1"},
                                       {"function", {{"name", "write"}, {"arguments", "{}"}}}}})}}
                 : Json{{"content", "Recovered"}};
        auto frame = [](const Json& j) { return "data: " + j.dump() + "\n\n"; };
        response.set_content(
            frame({{"choices",
                    Json::array({{{"index", 0}, {"delta", delta}, {"finish_reason", nullptr}}})}}) +
                frame({{"choices",
                        Json::array({{{"index", 0},
                                      {"delta", Json::object()},
                                      {"finish_reason", call ? "tool_calls" : "stop"}}})}}) +
                "data: [DONE]\n\n",
            "text/event-stream");
    });
    int port = server.bind_to_any_port("127.0.0.1");
    if (port < 1)
        throw std::runtime_error("Cannot bind context fixture");
    worker = std::thread([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    auto tools = std::make_shared<Registry>(Policy::allow_all());
    tools->add({"write",
                "Write",
                {{"type", "object"}},
                {{"type", "object"}},
                false,
                [&](const auto&, const auto&) {
                    ++executions;
                    return Json{{"text", std::string(2000, 'x')}};
                }});
    HttpChatOptions options;
    options.base_url = "http://127.0.0.1:" + std::to_string(port);
    options.tools = tools;
    Conversation chat(std::make_shared<HttpChatProvider>(options), "model");
    chat.configure_context({600, 1, 64, {}});
    chat.connect();
    expect_error("ContextTooLarge", [&] { chat.send("Perform the write"); });
    auto history = chat.history();
    check(requests == 1 && executions == 1 && history.size() == 3 &&
              history.back()["role"] == "tool",
          "Context overflow retains executed mutations and their complete result without retry");
    check(chat.send("Continue").text == "Recovered" && executions == 1 &&
              chat.history().size() == 5,
          "A new user turn recovers with compact context and complete stored history");
}
class Fixture final : public Provider {
  public:
    Json last;
    bool uses_host_history() const noexcept override {
        return true;
    }
    ProviderCapabilities capabilities() const override {
        return {};
    }
    void connect(const CallContext&) override {}
    Session start_session(std::string model, std::string, const CallContext&,
                          std::string) override {
        return {"fixture", model};
    }
    ChatResult send(Session&, Json& messages, const CallContext&) override {
        last = messages;
        messages.push_back({{"role", "assistant"}, {"content", "Answer"}});
        return {"Answer", "stop"};
    }
    void disconnect() noexcept override {}
};
int main() {
    try {
        auto provider = std::make_shared<Fixture>();
        Conversation chat(provider, "model", "Pinned instructions");
        ContextOptions o;
        o.max_bytes = 900;
        o.recent_turns = 2;
        o.summary_bytes = 100;
        int summaries = 0;
        o.summarize = [&](const Json& request, const CallContext&) {
            ++summaries;
            check(request.contains("previous_summary") && request["messages"].is_array(),
                  "Summarizer receives omitted turns and previous summary only");
            return "Earlier turns retained in the full application history.";
        };
        chat.configure_context(o);
        chat.connect();
        for (int n = 0; n < 12; ++n) {
            auto answer = chat.send("Turn " + std::to_string(n) + std::string(120, 'x'));
            check(answer.text == "Answer", "Projected API turn completes");
            if (n < 2)
                check(summaries == 0,
                      "No summarizer runs until earlier user turns are actually omitted");
            check(provider->last.front()["role"] == "system" &&
                      serialized_size(provider->last) <= o.max_bytes,
                  "Pinned instructions and bounded context reach the provider");
        }
        check(chat.history().size() == 25 && summaries > 0,
              "Projection keeps the entire original conversation in host history");
        check(provider->last.back()["content"].get<std::string>().starts_with("Turn 11"),
              "Newest user turn remains complete");
        auto full = chat.history();
        ContextWindow disabled;
        check(disabled.project(full).messages == full,
              "Disabled context projection preserves legacy behavior");
        ContextWindow window({650, 1, 100, {}});
        Json history =
            Json::array({{{"role", "system"}, {"content", "Pinned"}},
                         {{"role", "user"}, {"content", "old"}},
                         {{"role", "assistant"},
                          {"content", ""},
                          {"tool_calls",
                           Json::array({{{"id", "call"},
                                         {"type", "function"},
                                         {"function", {{"name", "read"}, {"arguments", "{}"}}}}})}},
                         {{"role", "tool"}, {"tool_call_id", "call"}, {"content", "result"}},
                         {{"role", "assistant"}, {"content", "done"}},
                         {{"role", "user"}, {"content", "new"}}});
        auto selected = window.project(history);
        check(selected.messages.size() == 3 && selected.omitted_messages == 4,
              "Tool call/result groups are evicted together with their user turn");
        expect_error("ContextTooLarge", [&] { chat.send(std::string(2000, 'z')); });
        check(chat.history() == full,
              "Rejected oversized input leaves committed history unchanged");
        chat.edit_message(0, "Edited first turn");
        chat.send("After edit");
        check(summaries > 1, "Editing history invalidates incremental summaries");
        auto checkpoint = chat.checkpoint();
        auto restored_provider = std::make_shared<Fixture>();
        Conversation restored(restored_provider, "model");
        restored.configure_context(o);
        restored.restore(checkpoint);
        check(restored.history() == chat.history(),
              "Checkpoint stores full history rather than the projected window");
        api_active_turn_budget();
        std::cout << checks << " context checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
