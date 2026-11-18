#include "../examples/chat_app/workspace_ui.hpp"
#include "test.hpp"
#include <cyui/modules/DocumentViews.h>
#include <imgui_internal.h>

namespace {
struct LogView {
    chat_example::Journal journal;
    cyui::TextFilter log_filter;
    std::filesystem::path log_path;
    bool log_errors_only = false, log_follow = true, log_raw = false, log_jump = true;
    std::uint64_t log_last_id = 0;

    template <class F> void action(F fn) {
        fn();
    }
    void open_log() {
        throw std::runtime_error("Unexpected open-file action");
    }
};

ImVec2 frame(LogView& app, const char* locate = nullptr) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Journal test", nullptr, ImGuiWindowFlags_NoSavedSettings);
    auto* window = ImGui::GetCurrentWindow();
    if (locate)
        ImGui::SetFocusID(ImGui::GetID(cyui::trLabel(locate).c_str()), window);
    chat_example::draw_logs(app);
    // Navigation records the actual widget bounds, including theme metrics and translation.
    const auto center =
        ImGui::WindowRectRelToAbs(window, window->NavRectRel[ImGuiNavLayer_Main]).GetCenter();
    ImGui::End();
    ImGui::Render();
    return center;
}

void click(LogView& app, const char* label) {
    const auto pos = frame(app, label);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(pos.x, pos.y);
    frame(app);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame(app);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame(app);
    frame(app);
}

ImGuiWindow& scroll_window(const LogView& app) {
    const auto name = app.log_raw ? "##raw-log" : "##diagnostics";
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (window->Active && (window->Flags & ImGuiWindowFlags_ChildWindow) &&
            std::string_view(window->Name).find(name) != std::string_view::npos)
            return *window;
    throw std::runtime_error(std::string("Missing journal scroll view ") + name);
}

void exercise_scrolling(LogView& app) {
    for (int i = 0; i < 3; ++i)
        frame(app);
    auto& child = scroll_window(app);
    check(child.ScrollMax.y > child.Size.y && child.ScrollMax.y - child.Scroll.y <= 3.f,
          "Following a populated journal initially shows its newest entry");
    const auto inside = child.InnerRect.GetCenter();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(inside.x, inside.y);
    frame(app);
    io.AddMouseWheelEvent(0.f, 5.f);
    frame(app);
    frame(app);
    const float reading = child.Scroll.y;
    check(child.ScrollMax.y - reading > ImGui::GetTextLineHeight() * 3.f,
          "A real mouse wheel scrolls back through the journal while follow is enabled");
    app.journal.add("error", "codex", "send", "ProviderError", "A new rejection", 0, "badRequest");
    frame(app);
    frame(app);
    check(std::abs(child.Scroll.y - reading) <= 1.f,
          "A new diagnostic preserves the position of someone reading earlier entries");
    click(app, "Follow log");
    check(!app.log_follow, "The follow switch can be disabled from the journal");
    click(app, "Follow log");
    check(app.log_follow && child.ScrollMax.y - child.Scroll.y <= 3.f,
          "Re-enabling follow jumps back to the newest journal entry");
    for (int i = 0; i < 6; ++i)
        app.journal.add("info", "fixture", "send", "Completed", "Completed entry");
    frame(app);
    frame(app);
    check(child.ScrollMax.y - child.Scroll.y <= 3.f,
          "New entries stay visible for readers following the bottom of the journal");
}
} // namespace

int main() {
    try {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1120, 520};
        io.DeltaTime = 1.f / 60.f;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        cyui::apply();
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(ImTextureID(1));
        std::string clipboard;
        auto& platform = ImGui::GetPlatformIO();
        platform.Platform_ClipboardUserData = &clipboard;
        platform.Platform_SetClipboardTextFn = [](ImGuiContext* context, const char* text) {
            *static_cast<std::string*>(context->PlatformIO.Platform_ClipboardUserData) = text;
        };
        LogView app;
        for (int i = 0; i < 180; ++i)
            app.journal.add("info", "fixture", "send", "Started", "Entry " + std::to_string(i));
        exercise_scrolling(app);
        click(app, "Raw JSONL");
        check(app.log_raw, "The raw JSONL switch opens the raw journal view");
        exercise_scrolling(app);
        click(app, "Errors only");
        click(app, "Copy log");
        std::istringstream lines(clipboard);
        std::size_t errors = 0;
        for (std::string line; std::getline(lines, line); ++errors) {
            const auto row = cymcp::Json::parse(line);
            check(row.at("code") == "ProviderError" && row.at("provider_code") == "badRequest" &&
                      row.at("message") == "A new rejection",
                  "Copying the filtered raw view preserves the diagnostic as JSONL");
        }
        check(errors == 2, "Copy includes each matching row and excludes filtered rows");
        ImGui::DestroyContext();
        std::cout << checks << " workspace checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
