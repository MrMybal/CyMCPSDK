#pragma once
#include <cymcp/chat_ui.hpp>
#include <fstream>
#include <imgui_internal.h>

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
    bool measure_frames = false;
    double first_rendered_ms = -1, completed_frame_ms = -1;
    Exercise(cymcp::Json input, std::filesystem::path output)
        : plan(std::move(input)), report(std::move(output)) {}

    // Called after drawing and before another poll can change the transcript. The benchmark
    // uses a fresh, short conversation with the Chat tab visible and follow enabled.
    template <class App> bool needs_frame_time(const App& app) const {
        if (!measure_frames || app.page != 0)
            return false;
        const auto& state = app.chat.state();
        if (state.messages.empty() || state.messages.back().role != "assistant" ||
            state.messages.back().markdown.find_first_not_of(" \r\n\t") == std::string::npos)
            return false;
        return first_rendered_ms < 0 || (!state.busy && completed_frame_ms < 0);
    }
    void frame_completed(bool response_finished) {
        const auto elapsed =
            std::chrono::duration<double, std::milli>(cymcp::Clock::now() - step_started).count();
        if (first_rendered_ms < 0)
            first_rendered_ms = elapsed;
        if (response_finished)
            completed_frame_ms = elapsed;
    }

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
                if (action == "automatic-connect") {
                    app.automatic = true;
                    app.detect_connections(true);
                } else if (action == "connect")
                    app.connect(step.value("restore", false));
                else if (action == "page")
                    app.show_page(step.at("index").template get<int>());
                else if (action == "draft")
                    app.panel.set_draft(step.at("text").template get<std::string>());
                else if (action == "raw-log")
                    app.log_raw = step.at("enabled").template get<bool>();
                else if (action == "click-settings" || action == "click-close") {
                    const auto* viewport = ImGui::GetMainViewport();
                    const auto padding = ImGui::GetStyle().WindowPadding;
                    const float half = ImGui::GetFrameHeight() * .5f;
                    auto& io = ImGui::GetIO();
                    const float y = action == "click-close"
                        ? viewport->WorkPos.y + padding.y + half
                        : viewport->WorkPos.y + viewport->WorkSize.y - padding.y -
                            ImGui::GetFrameHeightWithSpacing();
                    io.AddMousePosEvent(viewport->WorkPos.x + viewport->WorkSize.x - padding.x - half,
                                        y);
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                } else if (action == "help-key" || action == "close-key") {
                    const auto key = action == "help-key" ? ImGuiKey_F1 : ImGuiKey_Escape;
                    ImGui::GetIO().AddKeyEvent(key, true);
                    ImGui::GetIO().AddKeyEvent(key, false);
                } else if (action == "select-provider")
                    app.select_provider(step.at("id").template get<std::string>());
                else if (action == "select-model")
                    app.chat.select_model(step.at("id").template get<std::string>());
                else if (action == "draft-attachment")
                    app.panel.add_attachments(
                        {std::filesystem::u8path(step.at("file").template get<std::string>())});
                else if (action == "preview-attachment" || action == "save-attachment-twice") {
                    const auto& messages = app.chat.state().messages;
                    auto row =
                        std::find_if(messages.rbegin(), messages.rend(), [](const auto& message) {
                            return !message.attachments.empty();
                        });
                    if (row == messages.rend())
                        throw cymcp::Error("InvalidState", "No attachment");
                    const auto& attachment =
                        row->attachments.at(step.value("index", std::size_t(0)));
                    if (action == "preview-attachment")
                        app.attachment_host.open(attachment, false);
                    else {
                        // Choose a deterministic destination in the test output directory.
                        app.attachment_host.save_to(attachment,
                                                    report.parent_path() / "saved-attachment.bin");
                        app.attachment_host.open(attachment, true);
                    }
                } else if (action == "click-send") {
                    // Use the actual multiline input's bounds to hit CyUI's adjacent Send icon.
                    const auto& windows = ImGui::GetCurrentContext()->Windows;
                    const auto input = std::find_if(windows.begin(), windows.end(), [](const auto* w) {
                        const std::string_view name(w->Name);
                        return w->Active && name.find("/##chat-panel_") != name.npos &&
                               name.find("/##input_") != name.npos;
                    });
                    if (input == windows.end())
                        throw cymcp::Error("ComposerMissing", "Could not locate the Chat Lab composer");
                    const auto& field = **input;
                    auto& io = ImGui::GetIO();
                    const float half = ImGui::GetFrameHeight() * .5f;
                    io.AddMousePosEvent(field.Pos.x + field.Size.x + ImGui::GetStyle().ItemSpacing.x + half,
                                        field.Pos.y + half);
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                } else if (action == "send") {
                    std::vector<std::filesystem::path> paths;
                    for (const auto& path : step.value("attachments", cymcp::Json::array()))
                        paths.push_back(std::filesystem::u8path(path.template get<std::string>()));
                    auto prompt = step.at("text").template get<std::string>();
                    measure_frames = step.value("measure_frames", false);
                    first_rendered_ms = completed_frame_ms = -1;
                    step_started = cymcp::Clock::now();
                    app.chat.send(std::move(prompt), std::move(paths));
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
        if (app.chat.state().busy || app.discovery_task.valid()) {
            if (step.contains("cancel_after_ms") &&
                cymcp::Clock::now() - step_started >
                    std::chrono::milliseconds(step.at("cancel_after_ms").template get<int>()))
                app.chat.cancel();
            if (cymcp::Clock::now() - step_started >
                std::chrono::milliseconds(step.value("timeout_ms", 45000))) {
                app.chat.cancel();
                app.discovery_cancel.cancel();
                failure = "ValidationTimeout";
            } else
                return;
        }
        if (action == "wait" && cymcp::Clock::now() - step_started <
                                    std::chrono::milliseconds(step.value("wait_ms", 0)))
            return;
        if (measure_frames && completed_frame_ms < 0 && failure.empty() &&
            app.chat.state().error_code.empty()) {
            if (cymcp::Clock::now() - step_started <
                std::chrono::milliseconds(step.value("timeout_ms", 45000)))
                return;
            failure = "FrameTimingUnavailable";
        }
        if (++wait_frames < step.value("settle_frames", 4))
            return;
        if (action == "draft-attachment") {
            const auto& pending = app.panel.pending_attachments();
            if (pending.empty() || !app.attachment_host.ready(cymcp::path_utf8(pending.back()))) {
                if (cymcp::Clock::now() - step_started < std::chrono::seconds(5))
                    return;
                failure = "DraftPreviewFailed";
            }
        }
        if (step.contains("image_ready")) {
            bool ready = false;
            for (const auto& message : app.chat.state().messages)
                for (const auto& attachment : message.attachments)
                    ready |= app.attachment_host.ready(attachment.source);
            const bool expected = step.at("image_ready").template get<bool>();
            if (expected && !ready && cymcp::Clock::now() - step_started < std::chrono::seconds(5))
                return;
            if (ready != expected)
                failure = "ImagePreviewFailed";
        }
        const auto& state = app.chat.state();
        std::string code = failure.empty() ? state.error_code : failure;
        bool ok = code == step.value("error", std::string());
        if (step.contains("provider"))
            ok &= state.provider_id == step.at("provider").template get<std::string>();
        if (step.contains("model"))
            ok &= state.model == step.at("model").template get<std::string>();
        if (step.contains("selected_provider"))
            ok &= app.provider_id == step.at("selected_provider").template get<std::string>();
        if (step.contains("configured_model"))
            ok &= std::string(app.model.data()) ==
                  step.at("configured_model").template get<std::string>();
        if (step.contains("configured_system"))
            ok &= std::string(app.system.data()) ==
                  step.at("configured_system").template get<std::string>();
        if (step.contains("page"))
            ok &= app.page == step.at("page").template get<int>();
        if (step.contains("draft"))
            ok &= app.panel.draft() == step.at("draft").template get<std::string>();
        if (step.contains("log_code")) {
            const auto expected = step.at("log_code").template get<std::string>();
            bool found = false;
            for (const auto& row : app.journal.rows())
                found |= row.code == expected;
            ok &= found;
        }
        if (action == "automatic-connect" && code.empty())
            ok &= state.connected;
        std::string reply;
        if (!state.messages.empty() && state.messages.back().role == "assistant") {
            reply = state.messages.back().markdown;
            if (step.contains("turn_error_contains")) {
                const auto& row = state.messages.back();
                const auto expected = step.at("turn_error_contains").template get<std::string>();
                ok &= !row.interrupted && !row.error_code.empty() && row.error_code == code &&
                      row.error_message.find(expected) != std::string::npos;
                if (step.contains("provider_code"))
                    ok &= row.provider_code == step.at("provider_code").template get<std::string>();
                ok &= std::any_of(
                    app.journal.rows().begin(), app.journal.rows().end(), [&](const auto& entry) {
                        return entry.level == "error" && entry.operation == "generating" &&
                               entry.code == row.error_code &&
                               entry.provider_code == row.provider_code &&
                               entry.message == row.error_message;
                    });
            }
        } else if (step.contains("turn_error_contains"))
            ok = false;
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
             {"page", app.page},
             {"log_entries", app.journal.rows().size()},
             {"reply_bytes", reply.size()},
             {"elapsed_ms",
              std::chrono::duration<double, std::milli>(cymcp::Clock::now() - step_started)
                  .count()},
             {"session_present", !state.session_id.empty()},
             {"same_session", !initial_session.empty() && state.session_id == initial_session},
             {"metrics", state.metrics},
             {"counter", app.commands.snapshot()}});
        if (measure_frames) {
            results.back()["first_rendered_ms"] = first_rendered_ms;
            results.back()["completed_frame_ms"] = completed_frame_ms;
            measure_frames = false;
        }
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
