#include "demo_provider.hpp"

// Include Windows definitions before the dependent SDK headers.
#include <windows.h>

#include <array>
#include <commdlg.h>
#include <cstdlib>
#include <cymcp/chat_ui.hpp>
#include <cyui/Core.h>
#include <d3d11.h>
#include <fstream>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <shellapi.h>
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
LRESULT WINAPI window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui::GetCurrentContext() &&
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return 1;
    switch (message) {
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
template <std::size_t N> void assign(std::array<char, N>& field, const std::string& text) {
    if (text.size() >= N)
        throw Error("InvalidOptions", "Configuration field exceeds the example's limit");
    std::copy(text.begin(), text.end(), field.begin());
    field[text.size()] = '\0';
}
void translations() {
    cyui::registerTranslations(
        "fr", {
                  {"Connection", "Connexion"},
                  {"Provider", "Fournisseur"},
                  {"Model ID", "Identifiant du modèle"},
                  {"Endpoint", "Adresse du service"},
                  {"API key environment", "Variable de clé API"},
                  {"API key (memory only)", "Clé API (en mémoire)"},
                  {"Executable", "Exécutable"},
                  {"Arguments (JSON array)", "Arguments (tableau JSON)"},
                  {"Working directory", "Dossier de travail"},
                  {"Advanced settings (JSON)", "Paramètres avancés (JSON)"},
                  {"System prompt", "Consigne système"},
                  {"Allow all actions", "Autoriser toutes les actions"},
                  {"Save history", "Enregistrer l’historique"},
                  {"History folder", "Dossier d’historique"},
                  {"History ID", "Identifiant d’historique"},
                  {"Resume native ID", "Identifiant natif à reprendre"},
                  {"Connect / New chat", "Connecter / Nouveau tchat"},
                  {"Restore saved chat", "Reprendre le tchat enregistré"},
                  {"Refresh model catalogue", "Actualiser le catalogue"},
                  {"Use this model for the next connection",
                   "Utiliser ce modèle à la prochaine connexion"},
                  {"Browse", "Parcourir"},
                  {"Appearance", "Apparence"},
                  {"Language", "Langue"},
                  {"Theme", "Thème"},
                  {"Search providers", "Rechercher un fournisseur"},
                  {"Demo / Markdown (no AI)", "Démo / Markdown (sans IA)"},
                  {"Demo mode: deterministic local text, no AI request.",
                   "Mode démo : texte local déterministe, aucun appel IA."},
                  {"Connection settings apply to the next connection.",
                   "Les paramètres s’appliquent à la prochaine connexion."},
                  {"Native CLI accounts are reused. API keys remain in memory.",
                   "Les comptes CLI natifs sont réutilisés. Les clés API restent en mémoire."},
                  {"Windows test application", "Application de test Windows"},
              });
}
struct App {
    HWND window;
    std::shared_ptr<ProviderRegistry> registry = chat_example::providers();
    ChatController chat{registry};
    ChatPanel panel;
    Json catalogue = registry->describe();
    std::string provider_id = "demo", error;
    std::array<char, 256> model{}, key_env{}, resume{}, history_id{};
    std::array<char, 1024> endpoint{}, key{};
    std::array<char, 4096> executable{}, cwd{}, history_folder{};
    std::array<char, 8192> arguments{}, settings{}, system{};
    cyui::TextFilter provider_filter;
    bool allow_all = false, save_history = false;
    explicit App(HWND w) : window(w), panel(chat, {{}, [w] { return choose_files(w); }}) {
        assign(model, "demo-markdown");
        assign(arguments, "[]");
        assign(settings, "{}");
        assign(history_id, "test-chat");
        wchar_t local[32768]{};
        const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        const auto folder =
            count && count < 32768 ? std::filesystem::path(local) : std::filesystem::current_path();
        assign(history_folder, path_utf8(folder / "CyMCPSDK" / "ChatLab" / "conversations"));
    }
    template <class F> void action(F&& fn) {
        try {
            fn();
            error.clear();
        } catch (const Error& failure) {
            error = failure.code;
        } catch (...) {
            error = "InvalidOptions";
        }
    }
    void load(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw Error("InvalidOptions", "Cannot open configuration");
        std::string text;
        for (char c; file.get(c);) {
            text += c;
            if (text.size() > 64 * 1024)
                throw Error("InvalidOptions", "Configuration too large");
        }
        auto config = parse_json(text);
        if (config.contains("api_key"))
            throw Error("InvalidOptions", "Use api_key_env");
        provider_id = config.at("provider").get<std::string>();
        assign(model, config.value("model", std::string()));
        assign(system, config.value("system", std::string()));
        assign(key_env, config.value("api_key_env", std::string()));
        assign(resume, config.value("resume_id", std::string()));
        auto process = config.value("process", Json::object());
        assign(executable, process.value("executable", std::string()));
        assign(cwd, process.value("cwd", std::string()));
        assign(arguments, process.value("arguments", Json::array()).dump());
        auto options = config.value("settings", Json::object());
        allow_all = options.value("allow_all_actions", false);
        options.erase("allow_all_actions");
        assign(settings, options.dump(2));
        if (config.contains("history")) {
            save_history = true;
            assign(history_folder, config.at("history").at("directory").get<std::string>());
            assign(history_id, config.at("history").at("id").get<std::string>());
        }
    }
    void connect(bool restore = false) {
        ChatConnection connection;
        connection.provider_id = provider_id;
        connection.model = model.data();
        connection.system_prompt = system.data();
        connection.resume_id = resume.data();
        connection.provider.settings = parse_json(settings.data());
        if (endpoint[0])
            connection.provider.settings["base_url"] = endpoint.data();
        connection.provider.settings["allow_all_actions"] = allow_all;
        connection.provider.approvals = allow_all ? Policy::allow_all() : Policy::deny_all();
        connection.provider.process.executable = std::filesystem::u8path(executable.data());
        connection.provider.process.cwd = std::filesystem::u8path(cwd.data());
        connection.provider.process.arguments =
            parse_json(arguments.data()).get<std::vector<std::string>>();
        connection.provider.api_key = key.data();
        if (connection.provider.api_key.empty() && key_env[0]) {
            std::string name = key_env.data();
            if (name.find_first_not_of(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
                std::string::npos)
                throw Error("InvalidOptions", "Invalid environment variable name");
            if (auto value = std::getenv(name.c_str()))
                connection.provider.api_key = value;
        }
        if (save_history) {
            connection.history_directory = std::filesystem::u8path(history_folder.data());
            connection.history_id = history_id.data();
            connection.restore = restore;
        } else if (restore)
            throw Error("InvalidOptions", "Enable history first");
        chat.open(std::move(connection));
    }
    void connection_panel() {
        cyui::heading(cyui::tr("Connection").c_str(), 2);
        {
            cyui::ScopedDisabled disabled(chat.state().busy);
            if (cyui::beginProperties("##provider-properties", 110)) {
                cyui::property(cyui::tr("Provider").c_str());
                auto name = provider_id;
                for (const auto& item : catalogue)
                    if (item.at("id") == provider_id)
                        name = item.at("name").get<std::string>();
                if (ImGui::BeginCombo("##provider", cyui::tr(name.c_str()).c_str())) {
                    cyui::filterBox("##provider-filter", provider_filter);
                    for (const auto& item : catalogue) {
                        auto id = item.at("id").get<std::string>();
                        auto label = item.at("name").get<std::string>();
                        if (!provider_filter.matches(label.c_str(), id.c_str()))
                            continue;
                        if (ImGui::Selectable((cyui::tr(label.c_str()) + "##" + id).c_str(),
                                              id == provider_id)) {
                            provider_id = id;
                            assign(model, id == "demo" ? "demo-markdown" : "");
                            assign(endpoint, "");
                            assign(executable, "");
                            assign(arguments, "[]");
                            assign(settings, "{}");
                            assign(key, "");
                            assign(resume, "");
                            assign(key_env, id == "anthropic"    ? "ANTHROPIC_API_KEY"
                                            : id == "openrouter" ? "OPENROUTER_API_KEY"
                                            : id == "openai" || id == "openai-compatible"
                                                ? "OPENAI_API_KEY"
                                                : "");
                        }
                    }
                    ImGui::EndCombo();
                }
                cyui::property(cyui::tr("Model ID").c_str());
                ImGui::InputText("##model", model.data(), model.size());
                cyui::property(cyui::tr("Endpoint").c_str());
                ImGui::InputText("##endpoint", endpoint.data(), endpoint.size());
                cyui::property(cyui::tr("API key environment").c_str());
                ImGui::InputText("##key-env", key_env.data(), key_env.size());
                cyui::property(cyui::tr("API key (memory only)").c_str());
                ImGui::InputText("##key", key.data(), key.size(), ImGuiInputTextFlags_Password);
                cyui::endProperties();
            }
            cyui::toggleSwitch(cyui::trLabel("Allow all actions").c_str(), &allow_all);
            if (cyui::beginSection(cyui::trLabel("Executable").c_str(), false)) {
                if (cyui::beginProperties("##process")) {
                    cyui::property(cyui::tr("Executable").c_str());
                    ImGui::InputText("##exe", executable.data(), executable.size());
                    cyui::property(cyui::tr("Working directory").c_str());
                    ImGui::InputText("##cwd", cwd.data(), cwd.size());
                    cyui::property(cyui::tr("Arguments (JSON array)").c_str());
                    ImGui::InputText("##argv", arguments.data(), arguments.size());
                    cyui::endProperties();
                }
                if (cyui::button(cyui::trLabel("Browse").c_str()))
                    action([&] {
                        auto selected = choose_files(window, true);
                        if (!selected.empty())
                            assign(executable, path_utf8(selected.front()));
                    });
                cyui::endSection();
            }
            if (cyui::beginSection(cyui::trLabel("Advanced settings (JSON)").c_str(), false)) {
                ImGui::InputTextMultiline("##settings", settings.data(), settings.size(),
                                          cyui::dp(-1, 100));
                cyui::endSection();
            }
            if (cyui::beginSection(cyui::trLabel("System prompt").c_str(), false)) {
                ImGui::InputTextMultiline("##system", system.data(), system.size(),
                                          cyui::dp(-1, 80));
                cyui::endSection();
            }
            cyui::toggleSwitch(cyui::trLabel("Save history").c_str(), &save_history);
            if (save_history && cyui::beginProperties("##history")) {
                cyui::property(cyui::tr("History folder").c_str());
                ImGui::InputText("##folder", history_folder.data(), history_folder.size());
                cyui::property(cyui::tr("History ID").c_str());
                ImGui::InputText("##history-id", history_id.data(), history_id.size());
                cyui::endProperties();
            }
            if (cyui::beginSection(cyui::trLabel("Resume native ID").c_str(), false)) {
                ImGui::InputText("##resume", resume.data(), resume.size());
                cyui::endSection();
            }
            if (cyui::button(cyui::trLabel("Connect / New chat").c_str(), cyui::Variant::secondary,
                             cyui::dp(-1, 0)))
                action([&] { connect(); });
            if (cyui::button(cyui::trLabel("Restore saved chat").c_str(), cyui::Variant::secondary,
                             cyui::dp(-1, 0)))
                action([&] { connect(true); });
        }
        {
            cyui::ScopedDisabled disabled(chat.state().busy || !chat.state().connected);
            if (cyui::button(cyui::trLabel("Refresh model catalogue").c_str(),
                             cyui::Variant::secondary, cyui::dp(-1, 0)))
                action([&] { chat.refresh_models(); });
        }
        const auto& models = chat.state().models;
        const auto& items = models.is_array()         ? models
                            : models.contains("data") ? models.at("data")
                                                      : Json::array();
        if (items.is_array() && !items.empty() &&
            ImGui::BeginCombo("##model-catalogue", cyui::tr("Model ID").c_str())) {
            for (const auto& item : items) {
                auto id =
                    item.is_string() ? item.get<std::string>() : item.value("id", std::string());
                if (!id.empty() && ImGui::Selectable(id.c_str(), id == model.data()))
                    assign(model, id);
            }
            ImGui::EndCombo();
        }
        if (provider_id == "demo")
            cyui::callout(cyui::Tone::info,
                          cyui::tr("Demo mode: deterministic local text, no AI request.").c_str());
        ImGui::PushTextWrapPos();
        cyui::textFaint("%s",
                        cyui::tr("Connection settings apply to the next connection.").c_str());
        cyui::textFaint(
            "%s", cyui::tr("Native CLI accounts are reused. API keys remain in memory.").c_str());
        if (!error.empty())
            cyui::textColoured(cyui::palette().error, "%s", error.c_str());
        ImGui::PopTextWrapPos();
        if (cyui::beginSection(cyui::trLabel("Appearance").c_str(), true)) {
            if (cyui::beginProperties("##appearance")) {
                cyui::property(cyui::tr("Language").c_str());
                if (ImGui::BeginCombo("##language",
                                      cyui::locale() == "fr" ? "Français" : "English")) {
                    if (ImGui::Selectable("English", cyui::locale() == "en"))
                        cyui::setLocale("en");
                    if (ImGui::Selectable("Français", cyui::locale() == "fr"))
                        cyui::setLocale("fr");
                    ImGui::EndCombo();
                }
                cyui::property(cyui::tr("Theme").c_str());
                if (ImGui::BeginCombo("##theme", cyui::currentThemeKey().c_str())) {
                    for (const auto& theme : cyui::builtInThemes())
                        if (ImGui::Selectable(theme.name.c_str(),
                                              cyui::currentThemeKey() == theme.key))
                            cyui::setTheme(theme.key.c_str());
                    ImGui::EndCombo();
                }
                cyui::endProperties();
            }
            cyui::endSection();
        }
    }
    void draw() {
        chat.poll();
        auto viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("CyMCPSDK Chat Lab", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        cyui::panelHeader("CyMCPSDK Chat Lab", cyui::tr("Windows test application").c_str());
        cyui::divider();
        if (ImGui::BeginTable("##layout", 2,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("##connection", ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(330));
            ImGui::TableSetupColumn("##chat", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginChild("##connection-scroll");
            connection_panel();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            panel.draw();
            ImGui::EndTable();
        }
        ImGui::End();
        cyui::renderNotifications();
    }
};
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    bool smoke = false;
    std::filesystem::path capture, config;
    std::string theme = "mid-grey", language = "en";
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args)
        return 2;
    for (int n = 1; n < count; ++n) {
        std::wstring argument = args[n];
        if (argument == L"--smoke")
            smoke = true;
        else if (n + 1 < count && argument == L"--capture")
            capture = args[++n];
        else if (n + 1 < count && argument == L"--config")
            config = args[++n];
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
    if ((smoke || !capture.empty()) && !config.empty())
        return 2; // captures never submit real generations
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
    cyui::setLocale(language.c_str());
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device.Get(), renderer.Get());
    if (!smoke && capture.empty()) {
        ShowWindow(window, show);
        UpdateWindow(window);
    }
    int result = 0;
    {
        App app(window);
        if (!config.empty())
            app.action([&] { app.load(config); });
        if (smoke || !capture.empty())
            app.action([&] { app.connect(); });
        bool done = false, sent = false;
        int settled_frames = 0;
        auto started = Clock::now();
        while (!done) {
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
            if ((smoke || !capture.empty()) && !sent && app.chat.state().connected &&
                !app.chat.state().busy) {
                app.action([&] { app.chat.send("Montre-moi le rendu Markdown natif de CyUI."); });
                sent = true;
            }
            ImGui::Render();
            auto colour = cyui::vec(cyui::palette().bg0);
            const float clear[]{colour.x, colour.y, colour.z, colour.w};
            auto render_target = target.Get();
            renderer->OMSetRenderTargets(1, &render_target, nullptr);
            renderer->ClearRenderTargetView(render_target, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if ((smoke || !capture.empty()) && sent && !app.chat.state().busy) {
                if (++settled_frames >= 12) {
                    const auto& state = app.chat.state();
                    if (!app.error.empty() || !state.error_code.empty() ||
                        state.messages.size() != 2 ||
                        state.messages.back().markdown.find("## Le tchat natif") != 0)
                        result = 4;
                    if (!capture.empty() && !capture_png(capture))
                        result = 5;
                    done = true;
                }
            }
            const auto presented = swap_chain->Present(smoke || !capture.empty() ? 0 : 1, 0);
            if (FAILED(presented)) {
                result = 3;
                break;
            }
            if (presented == DXGI_STATUS_OCCLUDED && !smoke && capture.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if ((smoke || !capture.empty()) && Clock::now() - started > std::chrono::seconds(25)) {
                result = 6;
                break;
            }
            if (smoke || !capture.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
