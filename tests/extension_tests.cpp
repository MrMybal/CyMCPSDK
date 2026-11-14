#include "test.hpp"
#include <cymcp/ai.hpp>
using namespace cymcp;
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            return 2;
        std::string fixture = argv[1];
        ProviderRegistry registry;
        std::string literal = "spaces \"quotes\" ; $(not-a-shell) & | `literal` é";
        for (bool buddy : {false, true}) {
            auto id = buddy ? "codebuddy" : "command-code";
            auto config_for = [&](std::string flag = "", bool allow = false) {
                ProviderConfig config;
                config.process = {fixture, {"--extension-fixture"}};
                if (buddy)
                    config.process.arguments.push_back("--buddy");
                if (allow)
                    config.process.arguments.push_back("--fixture-allow");
                if (!flag.empty())
                    config.process.arguments.push_back(flag);
                config.settings["allow_all_actions"] = allow;
                return config;
            };
            auto provider = registry.create(id, config_for());
            Conversation chat(provider, "fixture-model");
            chat.connect();
            if (buddy)
                expect_error("Unsupported", [&] { provider->list_models(); });
            else
                check(provider->list_models().size() == 2,
                      "Command Code reads a real JSON catalogue");
            std::string streamed;
            CallContext events;
            events.events = [&](const Event& event) {
                if (event.type == "TextDelta")
                    streamed += event.data.at("text").get<std::string>();
            };
            auto result = chat.send(literal, events);
            check(result.text == literal,
                  "Headless CLI forwards UTF-8 prompt on stdin without shell");
            check(streamed == "Partial",
                  "CLI maps streaming events and deduplicates assistant snapshots");
            check(result.usage.at("output_tokens") == 3, "CLI keeps native usage");
            check(chat.session().id == "extension-fixture-session",
                  "CLI stores its native session ID");
            Conversation resumed(registry.create(id, config_for()), "fixture-model");
            resumed.connect(chat.session().id);
            check(resumed.send("resume exact").text == "resume exact",
                  "CLI resumes the exact session");
            Conversation permissive(registry.create(id, config_for("", true)), "fixture-model");
            permissive.connect();
            check(permissive.send("allow").text == "allow",
                  "CLI allow-all maps to explicit native options");
            for (const auto& [flag, code] : std::vector<std::pair<std::string, std::string>>{
                     {"--fixture-fail", "ProviderError"},
                     {"--fixture-max-turns", "TurnLimitExceeded"},
                     {"--fixture-denied", "ProviderError"},
                     {"--fixture-truncated", "UnexpectedEOF"},
                     {"--fixture-crash", "ProcessCrash"},
                     {"--fixture-invalid-json", "InvalidJson"}}) {
                Conversation broken(registry.create(id, config_for(flag)), "fixture-model");
                broken.connect();
                expect_error(code, [&] { broken.send("partial failure"); });
            }
            Conversation mismatch(registry.create(id, config_for("--fixture-wrong-session")),
                                  "fixture-model");
            mismatch.connect(chat.session().id);
            expect_error("SessionInvalid", [&] { mismatch.send("resume"); });
            auto slow = config_for("--fixture-silent-turn");
            slow.settings["turn_timeout_ms"] = 100;
            Conversation timeout(registry.create(id, slow), "fixture-model");
            timeout.connect();
            expect_error("Timeout", [&] { timeout.send("wait"); });
            slow.settings["turn_timeout_ms"] = 600000;
            auto running = registry.create(id, slow);
            Conversation cancellable(running, "fixture-model");
            cancellable.connect();
            Executor executor(1);
            auto future = cancellable.send_async(executor, "wait");
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            cancellable.cancel();
            expect_error("Cancelled", [&] { future.get(); });
            auto another = cancellable.send_async(executor, "wait again");
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            running->disconnect();
            expect_error("Cancelled", [&] { another.get(); });
            if (!buddy) {
                auto text = registry.create(id, config_for("--fixture-text-models"));
                expect_error("Unsupported", [&] { text->list_models(); });
            }
        }
        auto acp_config = [&](std::string flag = "", bool allow = false, bool devin = false) {
            ProviderConfig config;
            config.process = {fixture, {"--extension-fixture", "--acp-fixture"}};
            if (!flag.empty())
                config.process.arguments.push_back(flag);
            if (allow)
                config.process.arguments.push_back("--fixture-allow");
            if (devin)
                config.process.arguments.push_back("--devin-fixture");
            config.settings["allow_all_actions"] = allow;
            return config;
        };
        auto provider = registry.create("devin", acp_config("", false, true));
        provider->connect();
        check(provider->list_models().size() == 2,
              "Devin exposes native CLI model catalogue without generation");
        Conversation devin(provider, "fixture-other");
        devin.connect();
        check(devin.session().model == "fixture-other",
              "ACP model choice is confirmed by native configuration");
        check(provider->capabilities().resume && provider->capabilities().model_catalogue,
              "ACP capabilities reflect negotiated session state");
        std::vector<std::string> events;
        CallContext context;
        context.events = [&](const Event& event) { events.push_back(event.type); };
        check(devin.send(literal, context).text == literal,
              "ACP streams exact text and ignores other sessions");
        check(devin.send("second turn").text == "second turn",
              "ACP persistent process accepts a second turn");
        check(std::find(events.begin(), events.end(), "ToolStarted") != events.end() &&
                  std::find(events.begin(), events.end(), "ToolCompleted") != events.end(),
              "ACP reports native tool lifecycle");
        check(std::find(events.begin(), events.end(), "UsageUpdated") != events.end(),
              "ACP reports native usage");
        check(provider->list_models().size() == 2,
              "ACP uses advertised model config catalogue after session start");
        auto resumed_provider = registry.create("acp", acp_config());
        Conversation resumed(resumed_provider, "fixture-other");
        resumed.connect(devin.session().id);
        check(resumed.send("after replay").text == "after replay",
              "ACP loadSession replay does not contaminate the next answer");
        for (bool allow : {false, true}) {
            auto permissions =
                registry.create("devin", acp_config("--fixture-permission", allow, true));
            Conversation conversation(permissions, "fixture-model");
            conversation.connect();
            check(conversation.send("permission").text == "permission",
                  "ACP permission response follows host policy and chooses one-shot grant");
        }
        {
            auto config = acp_config("--fixture-permission");
            config.approvals =
                Policy([](const CallContext& active, const std::string&, const Json&) {
                    active.cancellation.cancel();
                    return Decision::allow;
                });
            Conversation pending(registry.create("acp", config), "fixture-model");
            pending.connect();
            expect_error("Cancelled", [&] { pending.send("cancel pending permission"); });
        }
        {
            int callbacks = 0;
            auto config = acp_config("--fixture-foreign-permission");
            config.approvals = Policy([&](const CallContext&, const std::string&, const Json&) {
                ++callbacks;
                return Decision::allow;
            });
            Conversation foreign(registry.create("acp", config), "fixture-model");
            foreign.connect();
            check(foreign.send("foreign request").text == "foreign request" && callbacks == 0,
                  "ACP rejects another session's permission request before host approval");
        }
        auto mode_config = acp_config();
        mode_config.settings["mode"] = "plan";
        Conversation plan(registry.create("acp", mode_config), "fixture-model");
        plan.connect();
        check(plan.send("plan").text == "plan", "ACP selects an exact native session mode");
        Conversation legacy(registry.create("acp", acp_config("--fixture-legacy-models")),
                            "fixture-other");
        legacy.connect();
        check(legacy.session().model == "fixture-other" && legacy.send("legacy").text == "legacy",
              "ACP handles advertised legacy model selection");
        Conversation unadvertised(
            registry.create("acp", acp_config("--fixture-unsupported-request")), "fixture-model");
        unadvertised.connect();
        check(unadvertised.send("no filesystem").text == "no filesystem",
              "ACP rejects unadvertised filesystem requests without executing them");
        for (const auto& [flag, code] : std::vector<std::pair<std::string, std::string>>{
                 {"--fixture-fail", "ProviderError"},
                 {"--fixture-max-turns", "TurnLimitExceeded"},
                 {"--fixture-refusal", "ProviderRefusal"},
                 {"--fixture-truncated", "UnexpectedEOF"}}) {
            Conversation broken(registry.create("acp", acp_config(flag)), "fixture-model");
            broken.connect();
            expect_error(code, [&] { broken.send("partial failure"); });
        }
        expect_error("UnsupportedVersion",
                     [&] { registry.create("acp", acp_config("--fixture-version2"))->connect(); });
        expect_error("ProtocolError",
                     [&] { registry.create("acp", acp_config("--fixture-wrong-rpc"))->connect(); });
        Conversation no_auth(registry.create("acp", acp_config("--fixture-auth")), "fixture-model");
        expect_error("AuthenticationRequired", [&] { no_auth.connect(); });
        Conversation no_resume(registry.create("acp", acp_config("--fixture-no-resume")),
                               "fixture-model");
        expect_error("Unsupported", [&] { no_resume.connect("acp-fixture-session"); });
        Conversation wrong_session(registry.create("acp", acp_config("--fixture-wrong-session")),
                                   "fixture-model");
        expect_error("SessionInvalid", [&] { wrong_session.connect("acp-fixture-session"); });
        auto no_models = registry.create("acp", acp_config("--fixture-no-models"));
        no_models->connect();
        expect_error("Unsupported", [&] { no_models->list_models(); });
        expect_error("Unsupported", [&] { no_models->start_session("not-advertised"); });
        auto silent = acp_config("--fixture-silent-connect");
        silent.settings["connect_timeout_ms"] = 100;
        expect_error("Timeout", [&] { registry.create("acp", silent)->connect(); });
        auto slow = acp_config("--fixture-silent-turn");
        slow.settings["turn_timeout_ms"] = 100;
        Conversation timed(registry.create("acp", slow), "fixture-model");
        timed.connect();
        expect_error("Timeout", [&] { timed.send("wait"); });
        slow.settings["turn_timeout_ms"] = 600000;
        auto cancel_provider = registry.create("acp", slow);
        Conversation cancellable(cancel_provider, "fixture-model");
        cancellable.connect();
        Executor worker(1);
        auto future = cancellable.send_async(worker, "wait");
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        cancellable.cancel();
        expect_error("Cancelled", [&] { future.get(); });
        expect_error("InvalidState", [&] { cancellable.send("must reconnect"); });
        cancellable.connect("acp-fixture-session");
        auto again = cancellable.send_async(worker, "wait again");
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        cancel_provider->disconnect();
        expect_error("Cancelled", [&] { again.get(); });
        std::cout << checks << " extension checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
