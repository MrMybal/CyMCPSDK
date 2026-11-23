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
template <std::size_t N> void assign(std::array<char, N>& field, const std::string& text) {
    if (text.size() >= N)
        throw Error("InvalidOptions", "Configuration field exceeds the example's limit");
    std::copy(text.begin(), text.end(), field.begin());
    field[text.size()] = '\0';
}
void translations() {
    cyui::registerTranslations(
        "fr",
        {
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
            {"App commands", "Commandes de l’application"},
            {"Counter", "Compteur"},
            {"AI may use app commands", "L’IA peut appeler les commandes de l’app"},
            {"Expose local MCP", "Activer le MCP local"},
            {"Copy MCP token", "Copier le jeton MCP"},
            {"Copy MCP address", "Copier l’adresse MCP"},
            {"MCP access profile", "Profil d’accès MCP"},
            {"Local controlled", "Local contrôlé"},
            {"Token only", "Jeton seul"},
            {"Protected", "Protégé"},
            {"Counter changes are blocked by this profile.",
             "Ce profil bloque les modifications du compteur."},
            {"Stop local MCP to change the token requirement.",
             "Arrêtez le MCP local pour changer l’exigence de jeton."},
            {"Any program on this computer can call the app's permitted operations.",
             "Tout programme sur cet ordinateur peut appeler les opérations autorisées de l’app."},
            {"Help (F1)", "Aide (F1)"},
            {"Automatic connection", "Connexion automatique"},
            {"Detect again", "Relancer la détection"},
            {"Detecting installed AI connections...", "Recherche des connexions IA installées…"},
            {"Available connections", "Connexions disponibles"},
            {"Connect", "Connecter"},
            {"Provider default", "Réglage natif du fournisseur"},
            {"Other providers / setup", "Autres fournisseurs / configuration"},
            {"Manual settings", "Paramètres manuels"},
            {"Installed CLI — native account", "CLI installé — compte natif"},
            {"Local service — models found", "Service local — modèles trouvés"},
            {"API key found in environment", "Clé API trouvée dans l’environnement"},
            {"Configured connection", "Connexion configurée"},
            {"Not installed", "Non installé"},
            {"API key required", "Clé API requise"},
            {"Endpoint required", "Adresse du service requise"},
            {"No chat model available", "Aucun modèle de conversation disponible"},
            {"Service unavailable — start it and detect again",
             "Service indisponible — démarrez-le puis relancez la détection"},
            {"Installed but not running — start it and detect again",
             "Installé mais arrêté — démarrez-le puis relancez la détection"},
            {"Manual configuration required", "Configuration manuelle requise"},
            {"No connection detected. Install a CLI, start a local server, or add an API key "
             "below.",
             "Aucune connexion détectée. Installez un CLI, démarrez un serveur local ou ajoutez "
             "une clé API ci-dessous."},
            {"Detected automatically. No message is sent until you press Send.",
             "Détecté automatiquement. Aucun message n’est envoyé avant votre action."},
            {"Sign in with the provider's native CLI, then reconnect.",
             "Connectez-vous avec le CLI natif du fournisseur, puis reconnectez ici."},
            {"Sign in", "Se connecter"},
            {"A console opens: finish signing in there, then press Connect.",
             "Une console s’ouvre : terminez la connexion, puis cliquez sur Connecter."},
            {"Selected model applies to the next connection.",
             "Le modèle choisi s’applique à la prochaine connexion."},
            {"Choose a model from the catalogue, then connect.",
             "Choisissez un modèle dans le catalogue, puis connectez-vous."},
        });
}
struct App {
    static constexpr int local_mcp_page = 4;

    HWND window;
    std::shared_ptr<ProviderRegistry> registry = chat_example::providers();
    chat_example::Commands commands;
    std::shared_ptr<PerformanceMonitor> performance;
    ContextOptions context_options;
    chat_example::ClipboardAttachments clipboard;
    ChatController chat{registry};
    chat_example::Attachments attachment_host;
    cyui::DocumentServices document_services;
    ChatPanel panel;
    Json catalogue = registry->describe();
    std::string provider_id = "demo", error;
    chat_example::Journal journal;
    std::filesystem::path log_path;
    std::uint64_t last_diagnostic = 0;
    int page = 0, last_settings_page = 1; // Chat, Providers, Logs, Help, Local MCP
    bool page_changed = false, log_errors_only = false, log_follow = true, log_raw = false;
    bool log_jump = true;
    std::uint64_t log_last_id = 0;
    cyui::Document help_document;
    cyui::DocumentState help_view;
    std::uint64_t delete_request = 0;
    bool confirm_delete = false;
    cyui::TextFilter log_filter;
    struct ProviderFields {
        std::vector<std::string> text;
        std::string secret;
        std::map<std::string, std::string> environment;
    };
    std::map<std::string, ProviderFields> provider_fields;
    std::array<char, 256> model{}, key_env{}, resume{}, history_id{};
    std::array<char, 1024> endpoint{}, key{};
    std::array<char, 4096> executable{}, cwd{}, history_folder{};
    std::array<char, 8192> arguments{}, settings{}, system{};
    cyui::TextFilter provider_filter;
    std::map<std::string, std::string> process_environment;
    bool allow_all = false, save_history = false, app_tools = false, testing = false;
    bool automatic = true, connect_after_detection = false;
    std::vector<DiscoveredConnection> detected;
    DiscoveryOptions discovery_options;
    Cancellation discovery_cancel;
    Clock::time_point discovery_started{};
#ifdef CYMCP_CHAT_HAS_LOCAL
    std::filesystem::path dependency_cache, cuda_directory;
    std::future<std::filesystem::path> cuda_installation;
    Cancellation cuda_cancel;
    void install_cuda() {
        if (cuda_installation.valid())
            return;
        cuda_cancel = Cancellation();
        CallContext context;
        context.cancellation = cuda_cancel;
        cuda_installation = std::async(std::launch::async, [cache = dependency_cache, context] {
            DependencyManager manager({cache, Policy::allow_all()});
            return manager.install(cuda_runtime_package(), context) / "runtime";
        });
    }
    void poll_cuda() {
        if (cuda_installation.valid() &&
            cuda_installation.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            action([&] {
                cuda_directory = cuda_installation.get();
                journal.add("info", "", "dependencies", "Installed", "CUDA runtime 12.8.90");
            });
    }
#endif
    std::future<std::vector<DiscoveredConnection>> discovery_task;
    ~App() {
        DragAcceptFiles(window, FALSE);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        discovery_cancel.cancel();
#ifdef CYMCP_CHAT_HAS_LOCAL
        cuda_cancel.cancel();
#endif
    }
    void open_link(const std::string& target) {
        if (auto* file = chat_example::Attachments::find(chat.state(), target)) {
            attachment_host.open(*file, false);
            return;
        }
        if (target.size() > 8192 || std::any_of(target.begin(), target.end(), [](unsigned char c) {
                return c <= 32 || c == 127 || c == '\\';
            }))
            throw Error("InvalidUrl", "Invalid link");
        if (!target.starts_with("mailto:"))
            parse_url(target.substr(0, target.find('#')));
        const auto url = std::filesystem::u8path(target).wstring();
        if (reinterpret_cast<INT_PTR>(
                ShellExecuteW(window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            throw Error("FileError", "Cannot open link");
    }
    ChatPanelOptions panel_options() {
        ChatPanelOptions options;
        options.pick_attachments = [this] { return choose_files(window); };
        options.open_settings = [this] { show_page(last_settings_page); };
        options.require_editable_history = true;
        options.edit_message = [this](auto id, const auto& text) { chat.edit_message(id, text); };
        options.delete_message = [this](auto id) {
            delete_request = id;
            confirm_delete = true;
        };
        options.open_link = [this](const std::string& target) { open_link(target); };
        options.document_services = &document_services;
        document_services.image = [this](const std::string& source) {
            try {
                const auto* attachment = chat_example::Attachments::find(chat.state(), source);
                return attachment ? attachment_host.image(*attachment) : cyui::ImageData{};
            } catch (...) {
                return cyui::ImageData{};
            }
        };
        options.open_attachment = [this](const auto& attachment, bool save) {
            attachment_host.open(attachment, save);
        };
        options.paste = [this](std::string& text, std::vector<std::filesystem::path>& paths) {
            return clipboard.paste(window, text, paths);
        };
        options.preview_attachment = [this](const auto& path) {
            return attachment_host.preview(path);
        };
        return options;
    }
    explicit App(HWND w)
        : window(w), attachment_host(w, device.Get()), panel(chat, panel_options()) {
        assign(model, "demo-markdown");
        assign(arguments, "[]");
        assign(settings, "{}");
        assign(history_id, "test-chat");
        wchar_t local[32768]{};
        const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        const auto folder =
            count && count < 32768 ? std::filesystem::path(local) : std::filesystem::current_path();
        assign(history_folder, path_utf8(folder / "CyMCPSDK" / "ChatLab" / "conversations"));
#ifdef CYMCP_CHAT_HAS_LOCAL
        dependency_cache = folder / "CyMCPSDK" / "ChatLab" / "dependencies";
#endif
    }
    template <class F> void action(F&& fn) {
        try {
            fn();
            error.clear();
        } catch (const Error& failure) {
            error = failure.code;
            journal.add("error", provider_id, "settings", error,
                        chat_example::diagnostic_help(error));
        } catch (...) {
            error = "InvalidOptions";
            journal.add("error", provider_id, "settings", error,
                        chat_example::diagnostic_help(error));
        }
    }
    void start_diagnostics(std::filesystem::path path) {
        log_path = std::move(path);
        journal.open_file(log_path);
        journal.add("info", "", "application", "Started", std::string("CyMCPSDK ") + version);
    }
    void sync_diagnostics() {
        if (performance)
            for (const auto& row : performance->drain())
                journal.add("info", "", row.value("stage", std::string()), "Performance",
                            row.dump(), row.value("elapsed_ms", 0.0));
        for (const auto& row : chat.state().diagnostics) {
            if (row.id <= last_diagnostic)
                continue;
            journal.add(row.level, row.provider_id, row.operation, row.code,
                        row.level == "error" && row.message.empty()
                            ? chat_example::diagnostic_help(row.code)
                            : row.message,
                        row.elapsed_ms, row.provider_code);
            last_diagnostic = row.id;
            if (row.code == "UnknownModel" && chat.state().phase == "choosing_model")
                show_page(1);
        }
    }
    void show_page(int value) {
        if (value < 0 || value > local_mcp_page)
            throw Error("InvalidOptions", "Unknown settings page");
        if (value)
            last_settings_page = value;
        page = value;
        page_changed = true;
    }
    void select_provider(const std::string& id) {
        if (id == provider_id)
            return;
        provider_fields[provider_id] = {{model.data(), key_env.data(), resume.data(),
                                         endpoint.data(), executable.data(), cwd.data(),
                                         arguments.data(), settings.data(), system.data()},
                                        key.data(),
                                        process_environment};
        auto saved = provider_fields.find(id);
        if (saved == provider_fields.end())
            apply_defaults(id);
        else {
            provider_id = id;
            const auto& text = saved->second.text;
            assign(model, text[0]);
            assign(key_env, text[1]);
            assign(resume, text[2]);
            assign(endpoint, text[3]);
            assign(executable, text[4]);
            assign(cwd, text[5]);
            assign(arguments, text[6]);
            assign(settings, text[7]);
            assign(system, text[8]);
            assign(key, saved->second.secret);
            process_environment = saved->second.environment;
        }
        journal.add("info", id, "settings", "ProviderSelected", "Provider settings selected");
        error.clear();
    }
    void browse_executable() {
        auto selected = choose_files(window, true);
        if (!selected.empty())
            assign(executable, path_utf8(selected.front()));
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
        automatic = config.value("automatic", false);
        discovery_options.use_environment = config.value("discovery_environment", true);
        assign(model, config.value("model", std::string()));
        assign(system, config.value("system", std::string()));
        assign(key_env, config.value("api_key_env", std::string()));
        assign(resume, config.value("resume_id", std::string()));
        auto process = config.value("process", Json::object());
        assign(executable, process.value("executable", std::string()));
        assign(cwd, process.value("cwd", std::string()));
        assign(arguments, process.value("arguments", Json::array()).dump());
        process_environment = process.value("environment", std::map<std::string, std::string>());
        const Json discovery_profiles = config.value("discovery", Json::object());
        for (const auto& [id, entry] : discovery_profiles.items()) {
            if (entry.contains("api_key"))
                throw Error("InvalidOptions", "Use environment credentials");
            ProviderConfig candidate;
            const auto native = entry.value("process", Json::object());
            candidate.process.executable =
                std::filesystem::u8path(native.value("executable", std::string()));
            candidate.process.arguments = native.value("arguments", std::vector<std::string>());
            candidate.settings = entry.value("settings", Json::object());
            discovery_options.configured[id] = std::move(candidate);
        }
        auto options = config.value("settings", Json::object());
        allow_all = options.value("allow_all_actions", false);
        options.erase("allow_all_actions");
        assign(settings, options.dump(2));
        auto command_config = config.value("app_commands", Json::object());
        const auto perf_config = config.value("performance", Json::object());
        if (perf_config.value("enabled", false)) {
            PerformanceOptions o;
            o.records = perf_config.value("records", std::size_t(1024));
            o.jsonl_file = std::filesystem::u8path(perf_config.value("file", std::string()));
            performance = std::make_shared<PerformanceMonitor>(std::move(o));
        }
        const auto context_config = config.value("context", Json::object());
        context_options.max_bytes = context_config.value("max_bytes", std::size_t(0));
        context_options.recent_turns = context_config.value("recent_turns", std::size_t(8));
        const auto storage = command_config.value("storage", Json::object());
        ResourceOptions storage_options;
        storage_options.disk_directory =
            std::filesystem::u8path(storage.value("directory", std::string()));
        storage_options.max_stored_bytes =
            storage.value("memory_bytes", storage_options.max_stored_bytes);
        storage_options.max_disk_bytes =
            storage.value("disk_bytes", storage_options.max_disk_bytes);
        storage_options.spool_threshold =
            storage.value("spool_threshold", storage_options.spool_threshold);
        if (performance || command_config.value("notifications", false) || !storage.empty())
            commands.configure_services(std::move(storage_options),
                                        command_config.value("notifications", false), performance);
        app_tools = command_config.value("attach_to_ai", false);
        commands.deferred_tools(command_config.value("deferred_tools", false));
        if (config.contains("app_commands"))
            commands.configure_mcp_access(chat_example::mcp_access_from_config(command_config),
                                          command_config.value("mcp_port", 0));
        if (command_config.contains("workspace")) {
            const auto& workspace = command_config.at("workspace");
            commands.configure_workspace(
                std::filesystem::u8path(workspace.at("root").get<std::string>()),
                std::filesystem::u8path(workspace.value("ripgrep", std::string())));
        }
        if (command_config.value("mcp", false))
            commands.start();
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
        connection.automatic_model = automatic;
        connection.context = context_options;
        connection.performance = performance;
        connection.provider.settings = parse_json(settings.data());
        if (endpoint[0])
            connection.provider.settings["base_url"] = endpoint.data();
        connection.provider.settings["allow_all_actions"] = allow_all;
        if (provider_id == "demo")
            connection.provider.settings["demo_locale"] = cyui::locale();
        connection.provider.approvals = allow_all ? Policy::allow_all() : Policy::deny_all();
        connection.provider.process.executable = std::filesystem::u8path(executable.data());
        connection.provider.process.cwd = std::filesystem::u8path(cwd.data());
        connection.provider.process.arguments =
            parse_json(arguments.data()).get<std::vector<std::string>>();
        connection.provider.process.environment = process_environment;
        if (app_tools) {
            std::string transport;
            for (const auto& entry : catalogue)
                if (entry.at("id") == provider_id)
                    transport = entry.at("transport").get<std::string>();
            if (transport == "app-server-stdio" || transport == "responses-sse" ||
                transport == "messages-sse" || transport == "chat-completions-sse" ||
                transport == "chat-ndjson")
                connection.provider.tools = commands.ai_registry();
            else
                throw Error("Unsupported",
                            "This CLI uses native MCP tools; attach its MCP configuration instead");
        }
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
        journal.add("info", provider_id, "connection", "ConfigurationReady",
                    model[0] ? "Connecting with the selected model"
                             : "Connecting with provider defaults");
    }
    ProviderConfig current_config() const {
        ProviderConfig config;
        config.process.executable = std::filesystem::u8path(executable.data());
        config.process.cwd = std::filesystem::u8path(cwd.data());
        config.process.arguments = parse_json(arguments.data()).get<std::vector<std::string>>();
        config.process.environment = process_environment;
        config.settings = parse_json(settings.data());
        if (endpoint[0])
            config.settings["base_url"] = endpoint.data();
        config.api_key = key.data();
        if (config.api_key.empty() && key_env[0])
            if (auto value = std::getenv(key_env.data()))
                config.api_key = value;
        return config;
    }
    void detect_connections(bool connect_first = false) {
        if (discovery_task.valid())
            return;
        discovery_cancel = Cancellation{};
        auto options = discovery_options;
        if (provider_id != "demo" && (executable[0] || endpoint[0] || key[0] || key_env[0]))
            options.configured[provider_id] = current_config();
        connect_after_detection = connect_first;
        discovery_started = Clock::now();
        journal.add("info", "", "discovery", "Started",
                    "Searching installed CLIs, API credentials and local services");
        CallContext context;
        context.cancellation = discovery_cancel;
        discovery_task =
            std::async(std::launch::async, [r = registry, options = std::move(options), context] {
                return r->discover(options, context);
            });
    }
    // Fill every field with the provider's published conventions, then with the native
    // installation or local service found by the last automatic detection.
    void apply_defaults(const std::string& id) {
        provider_id = id;
        assign(model, id == "demo" ? "demo-markdown" : "");
        assign(system, "");
        assign(resume, "");
        assign(key, "");
        assign(executable, "");
        assign(cwd, "");
        assign(arguments, "[]");
        process_environment.clear();
        Json defaults = Json::object();
        for (const auto& item : catalogue)
            if (item.at("id") == id)
                defaults = item.value("defaults", Json::object());
        auto options = defaults.value("settings", Json::object());
        options.erase("allow_all_actions"); // owned by the toggle
        assign(settings, options.empty() ? std::string("{}") : options.dump(2));
        assign(endpoint, defaults.value("base_url", std::string()));
        assign(key_env, defaults.value("credential_env", std::string()));
        for (const auto& row : detected)
            if (row.provider_id == id && row.available())
                apply_detected(row);
        error.clear();
    }
    void apply_detected(const DiscoveredConnection& row) {
        if (!row.config.process.executable.empty()) {
            assign(executable, path_utf8(row.config.process.executable));
            assign(arguments, Json(row.config.process.arguments).dump());
            process_environment = row.config.process.environment;
        }
        if (!row.config.process.cwd.empty())
            assign(cwd, path_utf8(row.config.process.cwd));
        auto options = parse_json(settings.data());
        for (const auto& [name, value] : row.config.settings.items())
            if (name == "base_url")
                assign(endpoint, value.get<std::string>());
            else if (name != "allow_all_actions")
                options[name] = value;
        assign(settings, options.empty() ? std::string("{}") : options.dump(2));
        if (!row.api_key_env.empty())
            assign(key_env, row.api_key_env);
        if (!row.config.api_key.empty())
            assign(key, row.config.api_key);
    }
    void use_detected(const DiscoveredConnection& row, bool preserve_options = false) {
        select_provider(row.provider_id);
        const std::string kept_model = model.data(), kept_resume = resume.data(),
                          kept_system = system.data();
        apply_defaults(row.provider_id);
        apply_detected(row);
        assign(system, kept_system);
        if (preserve_options) {
            assign(model, kept_model);
            assign(resume, kept_resume);
        }
        error.clear();
    }
    void poll_discovery() {
        if (!discovery_task.valid() ||
            discovery_task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        action([&] {
            detected = discovery_task.get();
            const auto elapsed =
                std::chrono::duration<double, std::milli>(Clock::now() - discovery_started).count();
            for (const auto& row : detected)
                journal.add("info", row.provider_id, "discovery", row.state, discovery_status(row));
            journal.add("success", "", "discovery", "Completed", "Provider discovery completed",
                        elapsed);
            if (!automatic || !connect_after_detection || chat.state().busy ||
                chat.state().connected)
                return;
            auto selected = std::find_if(detected.begin(), detected.end(), [&](const auto& row) {
                // Demo is the initial offline placeholder, not an explicit AI preference.
                return provider_id != "demo" && row.provider_id == provider_id;
            });
            if (selected == detected.end() && (provider_id.empty() || provider_id == "demo"))
                selected = std::find_if(detected.begin(), detected.end(),
                                        [](const auto& row) { return row.available(); });
            if (selected != detected.end() && selected->available()) {
                use_detected(*selected, selected->provider_id == provider_id);
                connect();
            } else {
                journal.add("warning", provider_id == "demo" ? "" : provider_id, "connection",
                            "NotAvailable",
                            "No available connection matches the current selection");
            }
        });
        connect_after_detection = false;
    }
    const char* discovery_status(const DiscoveredConnection& row) const {
        if (row.available()) {
            if (row.source == "installed-cli")
                return "Installed CLI — native account";
            if (row.source == "local-service")
                return "Local service — models found";
            if (row.source == "configured-endpoint" || row.source == "configured-api")
                return "Configured connection";
            return "API key found in environment";
        }
        if (row.state == "not-installed")
            return "Not installed";
        if (row.state == "needs-key")
            return "API key required";
        if (row.state == "needs-endpoint")
            return "Endpoint required";
        if (row.state == "no-models")
            return "No chat model available";
        if (row.state == "not-running")
            return "Installed but not running — start it and detect again";
        if (row.state == "unavailable")
            return "Service unavailable — start it and detect again";
        return "Manual configuration required";
    }
    Json provider_entry() const {
        for (const auto& item : catalogue)
            if (item.at("id") == provider_id)
                return item;
        return Json::object();
    }
    // The provider's own CLI (manual path first, else the detected installation) followed by
    // its documented sign-in subcommand, or nothing for CLIs that sign in on first launch.
    ProcessOptions sign_in_process() const {
        ProcessOptions process;
        for (const auto& row : detected)
            if (row.provider_id == provider_id && !row.config.process.executable.empty())
                process = row.config.process;
        if (executable[0]) {
            process = current_config().process;
            process.executable = find_executable(executable.data());
        }
        if (process.executable.empty())
            throw Error("ExecutableMissing", "Native CLI not detected; set its executable path");
        const auto defaults = provider_entry().value("defaults", Json::object());
        for (const auto& argument : defaults.value("login_arguments", Json::array()))
            process.arguments.push_back(argument.get<std::string>());
        return process;
    }
    void set_model(const std::string& id) {
        assign(model, id);
    }
    void sign_in() {
        open_console(sign_in_process());
        journal.add("info", provider_id, "authentication", "SignInOpened",
                    "Native sign-in console opened");
    }
    void open_log() {
        if (log_path.empty() || !std::filesystem::is_regular_file(log_path))
            throw Error("FileError", "Log file is not available");
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", log_path.c_str(), nullptr,
                                                    nullptr, SW_SHOWNORMAL)) <= 32)
            throw Error("FileError", "Cannot open log file");
    }
    void open_help() {
        std::wstring path(32768, L'\0');
        auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size >= path.size())
            throw Error("FileError", "Cannot locate application");
        path.resize(size);
        auto url = chat_example::help_url(path, cyui::locale() == "fr");
        if (reinterpret_cast<INT_PTR>(
                ShellExecuteW(window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            throw Error("FileError", "Cannot open documentation");
    }
    void draw() {
        chat.poll();
        poll_discovery();
#ifdef CYMCP_CHAT_HAS_LOCAL
        poll_cuda();
#endif
        sync_diagnostics();
        if (ImGui::IsKeyPressed(ImGuiKey_F1))
            show_page(3);
        else if (page && ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive() &&
                 !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            show_page(0);
        const auto viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("CyMCPSDK Chat Lab", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        if (confirm_delete) {
            ImGui::OpenPopup("###delete-turn");
            confirm_delete = false;
        }
        const auto delete_title = cyui::tr("Delete this turn?") + "###delete-turn";
        if (ImGui::BeginPopupModal(delete_title.c_str(), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                cyui::tr("The message and its reply will be removed from the conversation history.")
                    .c_str());
            if (cyui::button(cyui::trLabel("Cancel").c_str()) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (cyui::button(cyui::trLabel("Delete turn").c_str())) {
                action([&] { chat.delete_turn(delete_request); });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        attachment_host.draw(chat.state(),
                             [this](const std::string& link) { action([&] { open_link(link); }); });
        if (!chat.state().busy)
            clipboard.discard_unused(panel.pending_attachments());
        if (page == 0) {
            if (!error.empty())
                chat_example::draw_error(*this);
            panel.draw();
        } else {
            const int requested = page_changed ? page : 0;
            page_changed = false;
            const auto close_label = cyui::trLabel("Close");
            const float close_width = ImGui::CalcTextSize(close_label.c_str(), nullptr, true).x +
                                      2 * ImGui::GetStyle().FramePadding.x;
            const float tabs_width = std::max(1.f, ImGui::GetContentRegionAvail().x - close_width -
                                                       ImGui::GetStyle().ItemSpacing.x);
            const auto fixed = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::BeginChild("##settings-tabs", {tabs_width, ImGui::GetFrameHeight()},
                              ImGuiChildFlags_None, fixed);
            if (ImGui::BeginTabBar("##settings")) {
                constexpr std::pair<int, const char*> tabs[]{
                    {1, "Providers"}, {local_mcp_page, "Local MCP"}, {2, "Logs"}, {3, "Help"}};
                for (const auto& [index, label] : tabs) {
                    if (ImGui::BeginTabItem(cyui::trLabel(label).c_str(), nullptr,
                                            requested == index ? ImGuiTabItemFlags_SetSelected
                                                               : 0)) {
                        if (!requested || requested == index)
                            last_settings_page = page = index;
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (cyui::button(close_label.c_str(), cyui::Variant::secondary,
                             {close_width, ImGui::GetFrameHeight()}))
                show_page(0);
            if (page) {
                ImGui::BeginChild("##settings-content", {}, ImGuiChildFlags_None, fixed);
                if (page == 1)
                    chat_example::draw_providers(*this);
                else if (page == local_mcp_page)
                    chat_example::draw_local_mcp(*this);
                else if (page == 2)
                    chat_example::draw_logs(*this);
                else
                    chat_example::draw_help(*this);
                ImGui::EndChild();
            }
        }
        ImGui::End();
        cyui::renderNotifications();
    }
};
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
        App app(window);
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
