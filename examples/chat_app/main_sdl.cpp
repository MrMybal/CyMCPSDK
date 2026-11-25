#include "Platform.h"
#include "Raster.h"
#include "dialogs_sdl.hpp"
#include "help.hpp"
#include <cymcp/chat_ui.hpp>
#include <cymcp/performance.hpp>
#include <cymcp/process.hpp>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>

extern char** environ;
namespace {
using namespace cymcp;
using NativeWindow = SDL_Window*;
using NativeRenderer = SDL_Renderer*;
chat_example::FileDialogs dialogs;

template <class Callback>
void request_files(SDL_Window* window, Callback&& callback, bool executable = false) {
    dialogs.choose(window, false, !executable, {}, std::forward<Callback>(callback));
}
template <class Callback>
void request_save_file(SDL_Window* window, const std::string& name, Callback&& callback) {
    dialogs.choose(window, true, false, name, std::forward<Callback>(callback));
}
std::filesystem::path application_data_folder() {
    if (const auto* data = std::getenv("XDG_DATA_HOME"); data && *data)
        return std::filesystem::u8path(data);
    if (const auto* home = std::getenv("HOME"); home && *home)
        return std::filesystem::u8path(home) / ".local" / "share";
    return std::filesystem::current_path();
}
std::filesystem::path current_executable() {
    return std::filesystem::read_symlink("/proc/self/exe");
}
void open_native(const std::string& target) {
    if (!SDL_OpenURL(target.c_str()))
        throw Error("FileError", "Cannot open file or link");
}
void open_native(const std::filesystem::path& target) {
    open_native(chat_example::file_url(target));
}
void open_console(const ProcessOptions& process) {
    auto terminal = find_executable("x-terminal-emulator");
    if (terminal.empty())
        terminal = find_executable("xterm");
    if (terminal.empty())
        throw Error("ProcessError", "Install a terminal emulator to open provider sign-in");
    std::vector<std::string> arguments{path_utf8(terminal), "-e", path_utf8(process.executable)};
    arguments.insert(arguments.end(), process.arguments.begin(), process.arguments.end());
    std::map<std::string, std::string> environment;
    for (auto at = environ; at && *at; ++at) {
        const std::string entry(*at);
        const auto equal = entry.find('=');
        if (equal != entry.npos)
            environment[entry.substr(0, equal)] = entry.substr(equal + 1);
    }
    for (const auto& [key, value] : process.environment)
        environment[key] = value;
    std::vector<std::string> entries;
    for (const auto& [key, value] : environment)
        entries.push_back(key + "=" + value);
    std::vector<char*> argv, env;
    for (auto& argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    for (auto& entry : entries)
        env.push_back(entry.data());
    env.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        throw Error("ProcessError", "Cannot prepare provider sign-in");
    struct Cleanup {
        posix_spawn_file_actions_t* actions;
        ~Cleanup() { posix_spawn_file_actions_destroy(actions); }
    } cleanup{&actions};
    if (!process.cwd.empty() &&
        posix_spawn_file_actions_addchdir_np(&actions, process.cwd.c_str()) != 0)
        throw Error("ProcessError", "Cannot select provider working directory");
    pid_t child = -1;
    if (posix_spawn(&child, terminal.c_str(), &actions, nullptr, argv.data(), env.data()) != 0)
        throw Error("ProcessError", "Cannot open the provider sign-in console");
    std::thread([child] {
        int status;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }).detach();
}
bool capture_png(SDL_Renderer* renderer, const std::filesystem::path& path) {
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    Surface captured(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
    if (!captured)
        return false;
    Surface rgba(SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!rgba)
        return false;
    std::vector<unsigned char> pixels(std::size_t(rgba->w) * rgba->h * 4);
    for (int y = 0; y < rgba->h; ++y)
        std::copy_n(static_cast<unsigned char*>(rgba->pixels) + y * rgba->pitch, rgba->w * 4,
                    pixels.data() + std::size_t(y) * rgba->w * 4);
    const auto png = raster::png(pixels.data(), rgba->w, rgba->h);
    return !png.empty() && platform::writeFile(path, png.data(), png.size());
}
} // namespace

#include "attachments.hpp"
#include "clipboard.hpp"
#include "application.hpp"

int main(int argc, char** argv) {
    bool smoke = false, hidden = false, test_ready = false;
    std::filesystem::path capture, config, exercise_path, report_path, log_file;
    std::string theme = "mid-grey", language = "en";
    for (int n = 1; n < argc; ++n) {
        const std::string argument = argv[n];
        if (argument == "--smoke")
            smoke = true;
        else if (argument == "--hidden")
            hidden = true;
        else if (argument == "--test-ready-stdout")
            test_ready = true;
        else if (n + 1 < argc && argument == "--exercise")
            exercise_path = std::filesystem::u8path(argv[++n]);
        else if (n + 1 < argc && argument == "--report")
            report_path = std::filesystem::u8path(argv[++n]);
        else if (n + 1 < argc && argument == "--capture")
            capture = std::filesystem::u8path(argv[++n]);
        else if (n + 1 < argc && argument == "--config")
            config = std::filesystem::u8path(argv[++n]);
        else if (n + 1 < argc && argument == "--log-file")
            log_file = std::filesystem::u8path(argv[++n]);
        else if (n + 1 < argc && argument == "--theme")
            theme = argv[++n];
        else if (n + 1 < argc && argument == "--language")
            language = argv[++n];
        else
            return 2;
    }
    if ((smoke || (!capture.empty() && exercise_path.empty())) && !config.empty())
        return 2;
    if ((!exercise_path.empty() && (report_path.empty() || smoke)) ||
        (test_ready && exercise_path.empty()))
        return 2;
    std::unique_ptr<chat_example::Exercise> exercise;
    try {
        if (!exercise_path.empty())
            exercise = std::make_unique<chat_example::Exercise>(
                chat_example::read_plan(exercise_path), report_path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    platform::Host host;
    if (!host.init("CyMCPSDK Chat Lab", hidden || smoke ||
                       (!capture.empty() && !exercise))) {
        std::cerr << SDL_GetError() << '\n';
        return 3;
    }
    SDL_SetWindowMinimumSize(host.window, 980, 700);
    SDL_Rect usable{};
    int width = 1320, height = 900;
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(host.window), &usable)) {
        width = std::clamp(usable.w - 32, 980, width);
        height = std::clamp(usable.h - 64, 700, height);
    }
    SDL_SetWindowSize(host.window, width, height);
    SDL_SetWindowPosition(host.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    cyui::Config ui;
    ui.themeKey = theme.c_str();
    ui.density = cyui::Density::comfortable;
    ui.dpiScale = host.scale();
    cyui::apply(ui);
    cyui::loadFonts();
    translations();
    chat_example::workspace_translations();
    cyui::setLocale(language.c_str());
    if (!host.initImGui())
        return 3;
    int result = 0;
    {
        App app(host.window, host.renderer);
        app.testing = static_cast<bool>(exercise);
        if (log_file.empty())
            log_file = exercise ? report_path.parent_path() / "application.log"
                       : !capture.empty()
                           ? capture.parent_path() / "logs" / (path_utf8(capture.stem()) + ".log")
                           : std::filesystem::u8path(app.history_folder.data()).parent_path() /
                                 "logs" / ("chat-" + random_token(16) + ".log");
        app.start_diagnostics(log_file);
        if (!config.empty())
            app.action([&] { app.load(config); });
        if (smoke || !capture.empty() || exercise)
            app.automatic = !config.empty() && app.automatic;
        if (app.automatic && !exercise && app.error.empty())
            app.action([&] { app.detect_connections(true); });
        if (exercise && !app.error.empty()) {
            exercise->passed = false;
            exercise->done = true;
        }
        if (!exercise && (smoke || !capture.empty()))
            app.action([&] { app.connect(); });
        if (test_ready) {
            app.commands.start();
            const auto ready = Json({{"endpoint", app.commands.endpoint()},
                                     {"token", app.commands.token()}}).dump() + "\n";
            if (std::fwrite(ready.data(), 1, ready.size(), stdout) != ready.size() ||
                std::fflush(stdout) != 0)
                result = 8;
        }
        bool done = false, sent = false;
        int settled_frames = 0;
        const auto started = Clock::now();
        const bool interactive = !smoke && capture.empty() && !hidden && !exercise;
        auto last_rendered = started;
        auto active_until = started + std::chrono::milliseconds(500);
        while (!done) {
            const auto frame_started = Clock::now();
            if (interactive) {
                SDL_PumpEvents();
                if (SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST))
                    active_until = frame_started + std::chrono::milliseconds(500);
            }
            host.events();
            if (host.closeRequested)
                break;
            try {
                dialogs.poll();
            } catch (...) {
                app.action([] { throw; });
            }
            if (!host.dropped.empty()) {
                app.action([&] {
                    app.panel.add_attachments(std::move(host.dropped));
                    host.dropped.clear();
                    app.show_page(0);
                });
            }
            if (!smoke && capture.empty() && !exercise &&
                (SDL_GetWindowFlags(host.window) & SDL_WINDOW_MINIMIZED)) {
                SDL_Delay(20);
                continue;
            }
            if (interactive && !app.chat.state().busy && frame_started >= active_until &&
                frame_started - last_rendered < std::chrono::milliseconds(100)) {
                // Input wakes the next frame immediately; idle polling still picks up
                // background connection/thumbnail updates without a continuous redraw.
                SDL_Delay(5);
                continue;
            }
            last_rendered = frame_started;
            host.newFrame();
            ImGui::NewFrame();
            app.draw();
            if (!exercise && (smoke || !capture.empty()) && !sent &&
                app.chat.state().connected && !app.chat.state().busy) {
                app.action([&] {
                    app.chat.send(language == "fr" ? "Montre-moi le rendu Markdown natif de CyUI."
                                                  : "Show me CyUI's native Markdown rendering.");
                });
                sent = true;
            }
            ImGui::Render();
            if (!host.render()) {
                result = 3;
                break;
            }
            if (exercise) {
                if (exercise->needs_frame_time(app)) {
                    SDL_Rect pixel{0, 0, 1, 1};
                    auto* completed = SDL_RenderReadPixels(host.renderer, &pixel);
                    if (!completed) {
                        result = 3;
                        break;
                    }
                    SDL_DestroySurface(completed);
                    exercise->frame_completed(!app.chat.state().busy);
                }
                exercise->tick(app);
                if (exercise->done) {
                    if (result == 0)
                        result = exercise->passed ? 0 : 7;
                    done = true;
                }
            }
            if (exercise && done && !capture.empty() && !capture_png(host.renderer, capture))
                result = 5;
            if (!exercise && (smoke || !capture.empty()) && sent && !app.chat.state().busy &&
                ++settled_frames >= 12) {
                const auto& state = app.chat.state();
                if (!app.error.empty() || !state.error_code.empty() || state.messages.size() != 2 ||
                    state.messages.back().markdown.find(
                        language == "fr" ? "## Le tchat natif" : "## Native chat") != 0)
                    result = 4;
                if (!capture.empty() && !capture_png(host.renderer, capture))
                    result = 5;
                done = true;
            }
            if (exercise)
                exercise->max_frame_ms = std::max(exercise->max_frame_ms,
                    std::chrono::duration<double, std::milli>(Clock::now() - frame_started).count());
            if (!SDL_RenderPresent(host.renderer)) {
                result = 3;
                break;
            }
            if (!exercise && (smoke || !capture.empty()) &&
                Clock::now() - started > std::chrono::seconds(25)) {
                result = 6;
                break;
            }
            if (smoke || !capture.empty() || hidden)
                SDL_Delay(10);
            else if (!exercise) {
                // Software renderers may have no presentation clock. Keep an idle
                // chat from spinning while still polling input and streaming at 60 Hz.
                const auto remaining = std::chrono::nanoseconds(16'666'667) -
                                       (Clock::now() - frame_started);
                if (remaining > std::chrono::nanoseconds::zero())
                    std::this_thread::sleep_for(remaining);
            }
        }
        if (exercise) {
            try {
                exercise->write(app.chat.state());
            } catch (...) {
                result = 9;
            }
        }
        app.chat.cancel();
    }
    return result;
}
