#pragma once
#include <cymcp/chat_ui.hpp>
#include <fstream>

namespace chat_example {
inline cymcp::Json read_plan(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw cymcp::Error("InvalidOptions", "Cannot read validation plan");
    std::string text;
    for (char c; file.get(c);) {
        text += c;
        if (text.size() > 64 * 1024)
            throw cymcp::Error("InvalidOptions", "Validation plan too large");
    }
    auto value = cymcp::parse_json(text);
    if (!value.is_object() || !value.contains("steps") || !value.at("steps").is_array() ||
        value.at("steps").size() > 128)
        throw cymcp::Error("InvalidOptions", "Validation plan requires up to 128 steps");
    return value;
}
// Driven by the real window's render loop. No native UI calls from worker threads.
struct Exercise {
    cymcp::Json plan, results = cymcp::Json::array();
    std::size_t index = 0;
    bool issued = false, done = false, passed = true, streamed = false;
    cymcp::Clock::time_point started = cymcp::Clock::now(), step_started = started;
    int frames = 0, wait_frames = 0;
    double max_frame_ms = 0;
    std::string initial_session;
    std::string failure;
    std::filesystem::path report;
    Exercise(cymcp::Json input, std::filesystem::path output)
        : plan(std::move(input)), report(std::move(output)) {}

    template <class App> void tick(App& app) {
        ++frames;
        app.chat.poll();
        if (app.chat.state().busy && !app.chat.state().messages.empty() &&
            app.chat.state().messages.back().role == "assistant" &&
            !app.chat.state().messages.back().markdown.empty())
            streamed = true;
        if (done)
            return;
        if (index == plan.at("steps").size()) {
            done = true;
            return;
        }
        const auto& step = plan.at("steps").at(index);
        const auto action = step.at("action").template get<std::string>();
        if (!issued) {
            issued = true;
            wait_frames = 0;
            streamed = false;
            step_started = cymcp::Clock::now();
            try {
                if (action == "connect")
                    app.connect(step.value("restore", false));
                else if (action == "send") {
                    std::vector<std::filesystem::path> paths;
                    for (const auto& path : step.value("attachments", cymcp::Json::array()))
                        paths.push_back(std::filesystem::u8path(path.template get<std::string>()));
                    app.chat.send(step.at("text").template get<std::string>(), std::move(paths));
                } else if (action == "models")
                    app.chat.refresh_models();
                else if (action == "reconnect")
                    app.chat.reconnect();
                else if (action == "disconnect")
                    app.chat.disconnect();
                else if (action == "save")
                    app.chat.save();
                else if (action == "rights")
                    app.commands.allow_changes(step.at("allow").template get<bool>());
                else if (action != "wait")
                    throw cymcp::Error("InvalidOptions", "Unknown validation action");
            } catch (const cymcp::Error& error) {
                failure = error.code;
            } catch (...) {
                failure = "InvalidOptions";
            }
        }
        if (app.chat.state().busy) {
            if (step.contains("cancel_after_ms") &&
                cymcp::Clock::now() - step_started >
                    std::chrono::milliseconds(step.at("cancel_after_ms").template get<int>()))
                app.chat.cancel();
            if (cymcp::Clock::now() - step_started >
                std::chrono::milliseconds(step.value("timeout_ms", 45000))) {
                app.chat.cancel();
                failure = "ValidationTimeout";
            } else
                return;
        }
        if (action == "wait" && cymcp::Clock::now() - step_started <
                                    std::chrono::milliseconds(step.value("wait_ms", 0)))
            return;
        if (++wait_frames < step.value("settle_frames", 4))
            return;
        const auto& state = app.chat.state();
        std::string code = failure.empty() ? state.error_code : failure;
        bool ok = code == step.value("error", std::string());
        std::string reply;
        if (!state.messages.empty() && state.messages.back().role == "assistant")
            reply = state.messages.back().markdown;
        if (step.value("trim_reply", false)) {
            const auto first = reply.find_first_not_of(" \t\r\n"),
                       last = reply.find_last_not_of(" \t\r\n");
            reply =
                first == std::string::npos ? std::string() : reply.substr(first, last - first + 1);
        }
        if (step.contains("reply"))
            ok &= reply == step.at("reply").template get<std::string>();
        if (step.contains("contains"))
            ok &= reply.find(step.at("contains").template get<std::string>()) != std::string::npos;
        if (step.value("streaming", false))
            ok &= streamed;
        if (step.contains("counter"))
            ok &= app.commands.snapshot().at("value") == step.at("counter");
        if (step.contains("messages"))
            ok &= state.messages.size() == step.at("messages").template get<std::size_t>();
        // Several CLIs only reveal the native ID on the first generation.
        if (initial_session.empty() && !state.session_id.empty())
            initial_session = state.session_id;
        if (step.value("same_session", false))
            ok &= !initial_session.empty() && state.session_id == initial_session;
        passed &= ok;
        results.push_back(
            {{"step", index},
             {"action", action},
             {"passed", ok},
             {"error", code},
             {"phase", state.phase},
             {"streamed_before_completion", streamed},
             {"messages", state.messages.size()},
             {"reply_bytes", reply.size()},
             {"elapsed_ms",
              std::chrono::duration<double, std::milli>(cymcp::Clock::now() - step_started)
                  .count()},
             {"session_present", !state.session_id.empty()},
             {"same_session", !initial_session.empty() && state.session_id == initial_session},
             {"metrics", state.metrics},
             {"counter", app.commands.snapshot()}});
        ++index;
        issued = false;
        failure.clear();
        if (!ok && !plan.value("continue_after_failure", false))
            done = true;
    }
    void write(const cymcp::ChatViewState& state) const {
        cymcp::Json value = {{"format", "cymcp-chat-app-validation"},
                             {"version", cymcp::version},
                             {"passed", passed && done && index == plan.at("steps").size()},
                             {"provider", state.provider_id},
                             {"model", state.model},
                             {"frames", frames},
                             {"max_frame_ms", max_frame_ms},
                             {"results", results}};
        std::ofstream file(report, std::ios::binary | std::ios::trunc);
        if (!file)
            throw cymcp::Error("StorageError", "Cannot write validation report");
        file << value.dump(2);
        if (!file)
            throw cymcp::Error("StorageError", "Cannot write validation report");
    }
};
} // namespace chat_example
