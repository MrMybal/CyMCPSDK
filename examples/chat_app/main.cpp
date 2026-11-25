#include "commands.hpp"
#include "demo_provider.hpp"
#include "exercise.hpp"
#include "help.hpp"
#include "journal.hpp"
#include <cymcp/performance.hpp>

// Include Windows definitions before the dependent SDK headers.
#include <shellapi.h>
#include <windows.h>

#include "attachments.hpp"
#include "clipboard.hpp"
#include "workspace_ui.hpp"
#include <array>
#include <commdlg.h>
#include <cstdlib>
#include <cymcp/chat_ui.hpp>
#ifdef CYMCP_CHAT_HAS_LOCAL
#include <cymcp/local.hpp>
#endif
#include <cyui/Core.h>
#include <d3d11.h>
#include <fstream>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <wincodec.h>
#include <wrl/client.h>

using namespace cymcp;
using Microsoft::WRL::ComPtr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace {
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> renderer;
ComPtr<IDXGISwapChain> swap_chain;
ComPtr<ID3D11RenderTargetView> target;
UINT resize_width = 0, resize_height = 0;
bool create_target() {
    ComPtr<ID3D11Texture2D> back;
    return SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back))) &&
           SUCCEEDED(device->CreateRenderTargetView(back.Get(), nullptr, &target));
}
bool create_device(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};
    for (auto driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP})
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 2,
                                                    D3D11_SDK_VERSION, &desc, &swap_chain, &device,
                                                    &level, &renderer)))
            return create_target();
    return false;
}
bool capture_png(const std::filesystem::path& path) {
    ComPtr<ID3D11Texture2D> back, staging;
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back))))
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
        return false;
    renderer->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(renderer->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    struct Unmap {
        ID3D11Texture2D* texture;
        ~Unmap() {
            renderer->Unmap(texture, 0);
        }
    } unmap{staging.Get()};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(desc.Width, desc.Height)))
        return false;
    auto format = GUID_WICPixelFormat32bppBGRA;
    return SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA &&
           SUCCEEDED(frame->WritePixels(desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height,
                                        static_cast<BYTE*>(mapped.pData))) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
void receive_files(HWND window, HDROP drop);
LRESULT WINAPI window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui::GetCurrentContext() &&
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return 1;
    switch (message) {
    case WM_DROPFILES:
        receive_files(window, reinterpret_cast<HDROP>(wparam));
        DragFinish(reinterpret_cast<HDROP>(wparam));
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            resize_width = LOWORD(lparam);
            resize_height = HIWORD(lparam);
        }
        return 0;
    case WM_DPICHANGED: {
        if (ImGui::GetCurrentContext())
            cyui::applyDpi(static_cast<float>(HIWORD(wparam)) / 96.f);
        auto rect = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_GETMINMAXINFO: {
        auto limits = reinterpret_cast<MINMAXINFO*>(lparam);
        const float scale = static_cast<float>(GetDpiForWindow(window)) / 96.f;
        limits->ptMinTrackSize = {static_cast<LONG>(980 * scale), static_cast<LONG>(700 * scale)};
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
std::vector<std::filesystem::path> choose_files(HWND owner, bool executable = false) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter = executable ? L"Executable\0*.exe\0All files\0*.*\0" : L"All files\0*.*\0";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR |
                   (executable ? 0 : OFN_ALLOWMULTISELECT);
    if (!GetOpenFileNameW(&dialog))
        return {};
    std::vector<std::filesystem::path> paths;
    std::filesystem::path first(buffer.data());
    const wchar_t* next = buffer.data() + first.native().size() + 1;
    if (*next == L'\0')
        return {first};
    while (*next) {
        paths.push_back(first / next);
        next += std::wcslen(next) + 1;
    }
    return paths;
}
// Windows argv quoting (CommandLineToArgvW rules); no shell is involved.
std::wstring quote_argument(const std::wstring& value) {
    if (!value.empty() && value.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return value;
    std::wstring result = L"\"";
    for (std::size_t n = 0;; ++n) {
        std::size_t slashes = 0;
        while (n < value.size() && value[n] == L'\\') {
            ++n;
            ++slashes;
        }
        if (n == value.size()) {
            result.append(slashes * 2, L'\\');
            break;
        }
        if (value[n] == L'"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        result += value[n];
    }
    return result + L"\"";
}
// Opens the provider's own sign-in in a visible console, on the user's explicit request.
// The user completes it there; credentials never pass through this application.
void open_console(const ProcessOptions& process) {
    std::wstring command = quote_argument(process.executable.wstring());
    for (const auto& argument : process.arguments)
        command += L" " + quote_argument(std::filesystem::u8path(argument).wstring());
    std::wstring environment;
    if (!process.environment.empty()) {
        std::map<std::wstring, std::wstring> merged;
        if (auto native = GetEnvironmentStringsW()) {
            for (auto at = native; *at; at += std::wcslen(at) + 1) {
                std::wstring entry = at;
                if (auto eq = entry.find(L'=', 1); eq != std::wstring::npos)
                    merged[entry.substr(0, eq)] = entry.substr(eq + 1);
            }
            FreeEnvironmentStringsW(native);
        }
        for (const auto& [key, value] : process.environment)
            merged[std::filesystem::u8path(key).wstring()] =
                std::filesystem::u8path(value).wstring();
        for (const auto& [key, value] : merged)
            environment += key + L"=" + value + L'\0';
        environment += L'\0';
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    auto cwd = process.cwd.wstring();
    if (!CreateProcessW(process.executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
                        environment.empty() ? nullptr : environment.data(),
                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &info))
        throw Error("ProcessError", "Cannot open the provider sign-in console");
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
}

using NativeWindow = HWND;
using NativeRenderer = ID3D11Device*;
template <class Callback>
void request_files(HWND window, Callback&& callback, bool executable = false) {
    callback(choose_files(window, executable));
}
std::filesystem::path application_data_folder() {
    wchar_t local[32768]{};
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    return count && count < 32768 ? std::filesystem::path(local) : std::filesystem::current_path();
}
std::filesystem::path current_executable() {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size())
        throw Error("FileError", "Cannot locate application");
    path.resize(size);
    return path;
}
void open_native(const std::wstring& target) {
    if (reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        throw Error("FileError", "Cannot open file or link");
}
void open_native(const std::string& target) {
    open_native(std::filesystem::u8path(target).wstring());
}
void open_native(const std::filesystem::path& target) {
    open_native(target.wstring());
}
} // namespace

#include "application.hpp"

namespace {
void receive_files(HWND window, HDROP drop) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app)
        app->action([&] {
            auto paths = chat_example::ClipboardAttachments::dropped(drop);
            if (!paths.empty()) {
                app->panel.add_attachments(std::move(paths));
                app->show_page(0);
            }
        });
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    bool smoke = false, hidden = false, test_ready = false;
    std::filesystem::path capture, config, exercise_path, report_path, log_file;
    std::string theme = "mid-grey", language = "en";
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args)
        return 2;
    for (int n = 1; n < count; ++n) {
        std::wstring argument = args[n];
        if (argument == L"--smoke")
            smoke = true;
        else if (argument == L"--hidden")
            hidden = true;
        else if (argument == L"--test-ready-stdout")
            test_ready = true;
        else if (n + 1 < count && argument == L"--exercise")
            exercise_path = args[++n];
        else if (n + 1 < count && argument == L"--report")
            report_path = args[++n];
        else if (n + 1 < count && argument == L"--capture")
            capture = args[++n];
        else if (n + 1 < count && argument == L"--config")
            config = args[++n];
        else if (n + 1 < count && argument == L"--log-file")
            log_file = args[++n];
        else if (n + 1 < count && argument == L"--theme")
            theme = path_utf8(args[++n]);
        else if (n + 1 < count && argument == L"--language")
            language = path_utf8(args[++n]);
        else {
            LocalFree(args);
            return 2;
        }
    }
    LocalFree(args);
    if ((smoke || (!capture.empty() && exercise_path.empty())) && !config.empty())
        return 2; // Demo captures never submit real generations.
    if ((!exercise_path.empty() && (report_path.empty() || smoke)) ||
        (test_ready && exercise_path.empty()))
        return 2;
    std::unique_ptr<chat_example::Exercise> exercise;
    try {
        if (!exercise_path.empty())
            exercise = std::make_unique<chat_example::Exercise>(
                chat_example::read_plan(exercise_path), report_path);
    } catch (...) {
        return 2;
    }
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW window_class{sizeof(WNDCLASSEXW),
                             CS_CLASSDC,
                             window_proc,
                             0,
                             0,
                             instance,
                             nullptr,
                             LoadCursorW(nullptr, IDC_ARROW),
                             nullptr,
                             nullptr,
                             L"CyMCPSDKChatLab",
                             nullptr};
    RegisterClassExW(&window_class);
    const auto dpi =
        ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND window =
        CreateWindowW(window_class.lpszClassName, L"CyMCPSDK Chat Lab", WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(1320 * dpi),
                      static_cast<int>(900 * dpi), nullptr, nullptr, instance, nullptr);
    if (!window || !create_device(window)) {
        if (window)
            DestroyWindow(window);
        if (SUCCEEDED(com))
            CoUninitialize();
        return 3;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    cyui::Config ui;
    ui.themeKey = theme.c_str();
    ui.density = cyui::Density::comfortable;
    ui.dpiScale = dpi;
    cyui::apply(ui);
    cyui::loadFonts();
    translations();
    chat_example::workspace_translations();
    cyui::setLocale(language.c_str());
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device.Get(), renderer.Get());
    if (!smoke && capture.empty() && !hidden) {
        ShowWindow(window, show);
        UpdateWindow(window);
    }
    int result = 0;
    {
        App app(window, device.Get());
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
        DragAcceptFiles(window, TRUE);
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
            const auto ready =
                Json({{"endpoint", app.commands.endpoint()}, {"token", app.commands.token()}})
                    .dump() +
                "\n";
            DWORD written = 0;
            if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ready.data(),
                           static_cast<DWORD>(ready.size()), &written, nullptr) ||
                written != ready.size())
                result = 8;
        }
        bool done = false, sent = false;
        int settled_frames = 0;
        auto started = Clock::now();
        while (!done) {
            const auto frame_started = Clock::now();
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
                if (message.message == WM_QUIT)
                    done = true;
            }
            if (done)
                break;
            if (!smoke && capture.empty() && IsIconic(window)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (resize_width && resize_height) {
                renderer->OMSetRenderTargets(0, nullptr, nullptr);
                target.Reset();
                if (FAILED(swap_chain->ResizeBuffers(0, resize_width, resize_height,
                                                     DXGI_FORMAT_UNKNOWN, 0)) ||
                    !create_target()) {
                    result = 3;
                    break;
                }
                resize_width = resize_height = 0;
            }
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app.draw();
            if (!exercise && (smoke || !capture.empty()) && !sent && app.chat.state().connected &&
                !app.chat.state().busy) {
                app.action([&] {
                    app.chat.send(language == "fr" ? "Montre-moi le rendu Markdown natif de CyUI."
                                                   : "Show me CyUI's native Markdown rendering.");
                });
                sent = true;
            }
            ImGui::Render();
            auto colour = cyui::vec(cyui::palette().bg0);
            const float clear[]{colour.x, colour.y, colour.z, colour.w};
            auto render_target = target.Get();
            renderer->OMSetRenderTargets(1, &render_target, nullptr);
            renderer->ClearRenderTargetView(render_target, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (exercise) {
                if (exercise->needs_frame_time(app)) {
                    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
                    ComPtr<ID3D11Query> fence;
                    if (FAILED(device->CreateQuery(&desc, &fence))) {
                        result = 3;
                        break;
                    }
                    renderer->End(fence.Get());
                    renderer->Flush();
                    const auto deadline = Clock::now() + std::chrono::seconds(5);
                    HRESULT status;
                    while ((status = renderer->GetData(fence.Get(), nullptr, 0, 0)) == S_FALSE &&
                           Clock::now() < deadline)
                        std::this_thread::yield();
                    if (status != S_OK) {
                        result = 3;
                        break;
                    }
                    exercise->frame_completed(!app.chat.state().busy);
                }
                exercise->tick(app);
                if (exercise->done) {
                    if (result == 0)
                        result = exercise->passed ? 0 : 7;
                    done = true;
                }
            }
            if (exercise && done && !capture.empty() && !capture_png(capture))
                result = 5;
            if (!exercise && (smoke || !capture.empty()) && sent && !app.chat.state().busy) {
                if (++settled_frames >= 12) {
                    const auto& state = app.chat.state();
                    if (!app.error.empty() || !state.error_code.empty() ||
                        state.messages.size() != 2 ||
                        state.messages.back().markdown.find(
                            language == "fr" ? "## Le tchat natif" : "## Native chat") != 0)
                        result = 4;
                    if (!capture.empty() && !capture_png(capture))
                        result = 5;
                    done = true;
                }
            }
            if (exercise)
                exercise->max_frame_ms =
                    std::max(exercise->max_frame_ms,
                             std::chrono::duration<double, std::milli>(Clock::now() - frame_started)
                                 .count());
            const auto presented =
                swap_chain->Present(smoke || !capture.empty() || hidden ? 0 : 1, 0);
            if (FAILED(presented)) {
                result = 3;
                break;
            }
            if (presented == DXGI_STATUS_OCCLUDED && !smoke && capture.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!exercise && (smoke || !capture.empty()) &&
                Clock::now() - started > std::chrono::seconds(25)) {
                result = 6;
                break;
            }
            if (smoke || !capture.empty() || hidden)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    target.Reset();
    swap_chain.Reset();
    renderer.Reset();
    device.Reset();
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, instance);
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
