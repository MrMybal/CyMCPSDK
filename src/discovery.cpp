#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <cymcp/ai.hpp>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cymcp {
namespace {
std::string environment(const std::string& name) {
#ifdef _WIN32
    auto key = std::filesystem::u8path(name).wstring();
    auto size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (!size || size > 32768)
        return {};
    std::wstring value(size, L'\0');
    auto count = GetEnvironmentVariableW(key.c_str(), value.data(), size);
    if (!count || count >= size)
        return {};
    value.resize(count);
    return path_utf8(std::filesystem::path(value));
#else
    auto value = std::getenv(name.c_str());
    return value ? value : "";
#endif
}
bool runnable(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return false;
#ifdef _WIN32
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".exe" || ext == L".com";
#else
    return access(path.c_str(), X_OK) == 0;
#endif
}
void append(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
        paths.push_back(path);
}
std::vector<std::filesystem::path> directories(const DiscoveryOptions& options) {
    auto paths = options.executable_directories;
    if (!options.use_environment)
        return paths;
    auto raw = environment("PATH");
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::size_t at = 0;
    while (at < raw.size()) {
        auto end = raw.find(separator, at);
        auto part = raw.substr(at, end - at);
        if (part.size() >= 2 && part.front() == '"' && part.back() == '"')
            part = part.substr(1, part.size() - 2);
        // An empty/relative PATH entry must never run code from an incidental cwd.
        auto path = std::filesystem::u8path(part);
        if (path.is_absolute())
            append(paths, path);
        if (end == std::string::npos)
            break;
        at = end + 1;
    }
    auto home = std::filesystem::u8path(environment(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
        ));
    if (!home.empty())
        for (const auto& suffix : {".local/bin", ".opencode/bin", ".bun/bin", ".npm-global/bin",
                                   ".cargo/bin", ".local/share/mise/shims"})
            append(paths, home / suffix);
#ifdef _WIN32
    auto local = std::filesystem::u8path(environment("LOCALAPPDATA"));
    if (!local.empty())
        for (const auto& suffix : {"agy/bin", "Programs/Ollama", "Programs/Antigravity/bin"})
            append(paths, local / suffix);
    auto roaming = std::filesystem::u8path(environment("APPDATA"));
    if (!roaming.empty())
        append(paths, roaming / "npm");
    auto programs = std::filesystem::u8path(environment("ProgramFiles"));
    if (!programs.empty())
        append(paths, programs / "nodejs");
#else
    append(paths, "/usr/local/bin");
    append(paths, "/usr/bin");
    append(paths, "/opt/homebrew/bin");
#endif
    return paths;
}
std::filesystem::path find_native(const std::vector<std::filesystem::path>& paths,
                                  const std::string& name) {
    if (name.empty() || name.find_first_of("/\\") != std::string::npos)
        return {};
    for (const auto& directory : paths) {
        auto path = directory / std::filesystem::u8path(name);
#ifdef _WIN32
        if (path.extension().empty())
            path += L".exe";
#endif
        if (runnable(path))
            return std::filesystem::absolute(path);
    }
    return {};
}
std::vector<std::filesystem::path> npm_roots(const DiscoveryOptions& options,
                                             const std::vector<std::filesystem::path>& paths) {
    auto roots = options.npm_roots;
    for (const auto& path : paths) {
        append(roots, path / "node_modules");
        append(roots, path.parent_path() / "lib/node_modules");
    }
    return roots;
}
Json manifest(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return Json::object();
    std::string text(65537, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (text.size() > 65536)
        return Json::object();
    try {
        return parse_json(text);
    } catch (...) {
        return Json::object();
    }
}
std::string model_id(const Json& row) {
    if (row.is_string())
        return row.get<std::string>();
    if (!row.is_object())
        return {};
    // Codex's actual request model may differ from a catalogue record's ID.
    for (auto key : {"model", "id", "name"})
        if (row.contains(key) && row.at(key).is_string())
            return row.at(key).get<std::string>();
    return {};
}
bool text_model(const Json& row) {
    auto id = model_id(row);
    std::transform(id.begin(), id.end(), id.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (id.empty())
        return false;
    for (auto excluded : {"embedding", "rerank", "whisper", "tts-", "transcrib", "dall-e", "image-",
                          "moderation", "realtime"})
        if (id.find(excluded) != std::string::npos)
            return false;
    if (row.is_object() && row.contains("architecture") && row.at("architecture").is_object()) {
        auto modalities = row.at("architecture").value("output_modalities", Json());
        if (modalities.is_array() &&
            std::find(modalities.begin(), modalities.end(), "text") == modalities.end())
            return false;
    }
    return true;
}
} // namespace

ProviderDiscoveryHints builtin_discovery(const std::string& id) {
    ProviderDiscoveryHints hints;
    const std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> cli{
        {"codex", {{"codex"}, {"@openai/codex"}}},
        {"claude-code", {{"claude"}, {"@anthropic-ai/claude-code"}}},
        {"claude", {{"claude"}, {"@anthropic-ai/claude-code"}}},
        {"antigravity", {{"agy"}, {}}},
        {"agy", {{"agy"}, {}}},
        {"opencode", {{"opencode"}, {"opencode-ai"}}},
        {"opencode-cli", {{"opencode"}, {"opencode-ai"}}},
        {"gemini", {{"gemini"}, {"@google/gemini-cli"}}},
        {"qwen-code", {{"qwen"}, {"@qwen-code/qwen-code"}}},
        {"qwen", {{"qwen"}, {"@qwen-code/qwen-code"}}},
        {"kimi", {{"kimi"}, {}}},
        {"goose", {{"goose"}, {}}},
        {"devin", {{"devin"}, {}}},
        {"command-code", {{"command-code", "cmdc"}, {"command-code"}}},
        {"command-code-acp", {{"command-code", "cmdc"}, {"command-code"}}},
        {"codebuddy", {{"codebuddy", "cbc"}, {"@tencent-ai/codebuddy-code"}}},
        {"codebuddy-acp", {{"codebuddy", "cbc"}, {"@tencent-ai/codebuddy-code"}}}};
    auto found = cli.find(id);
    if (found != cli.end()) {
        hints.executables = found->second.first;
        hints.npm_packages = found->second.second;
    }
    if (id == "openai" || id == "openai-compatible") {
        hints.api_key_env = "OPENAI_API_KEY";
        hints.endpoint_env = "OPENAI_BASE_URL";
        hints.default_endpoint = id == "openai" ? "https://api.openai.com/v1" : "";
    } else if (id == "anthropic") {
        hints.api_key_env = "ANTHROPIC_API_KEY";
        hints.endpoint_env = "ANTHROPIC_BASE_URL";
        hints.default_endpoint = "https://api.anthropic.com/v1";
    } else if (id == "openrouter") {
        hints.api_key_env = "OPENROUTER_API_KEY";
        hints.endpoint_env = "OPENROUTER_BASE_URL";
        hints.default_endpoint = "https://openrouter.ai/api/v1";
    } else if (id == "ollama") {
        hints.local_service = true;
        hints.endpoint_env = "OLLAMA_HOST";
        hints.default_endpoint = "http://127.0.0.1:11434";
    } else if (id == "lmstudio" || id == "llama.cpp") {
        hints.local_service = true;
        hints.default_endpoint =
            id == "lmstudio" ? "http://127.0.0.1:1234/v1" : "http://127.0.0.1:9931/v1";
    }
    return hints;
}

ProcessOptions resolve_provider_process(const ProviderDiscoveryHints& hints, ProcessOptions process,
                                        const DiscoveryOptions& options) {
    if (!process.executable.empty()) {
        process.executable = find_executable(path_utf8(process.executable));
        if (!runnable(process.executable))
            throw Error("Unsupported", "Use a native executable or node with a script argument");
        return process;
    }
    auto paths = directories(options);
    for (const auto& name : hints.executables) {
        auto path = find_native(paths, name);
        if (!path.empty()) {
            process.executable = std::move(path);
            return process;
        }
    }
    for (const auto& root : npm_roots(options, paths))
        for (const auto& package : hints.npm_packages) {
            auto folder = root / std::filesystem::u8path(package);
            auto data = manifest(folder / "package.json");
            if (!data.is_object() || !data.contains("bin"))
                continue;
            for (const auto& name : hints.executables) {
                auto bins = data.at("bin");
                auto entry = bins.is_string()   ? bins
                             : bins.is_object() ? bins.value(name, Json())
                                                : Json();
                if (!entry.is_string())
                    continue;
                auto relative =
                    std::filesystem::u8path(entry.get<std::string>()).lexically_normal();
                if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                    continue;
                auto path = folder / relative;
                std::error_code ec;
                if (!std::filesystem::is_regular_file(path, ec))
                    continue;
                auto actual = std::filesystem::canonical(path, ec);
                if (ec)
                    continue;
                auto package_root = std::filesystem::canonical(folder, ec);
                if (ec)
                    continue;
                auto inside = actual.lexically_relative(package_root);
                if (inside.empty() || *inside.begin() == ".." || inside.is_absolute())
                    continue;
                if (runnable(path) && path.extension() != ".js" && path.extension() != ".mjs" &&
                    path.extension() != ".cjs") {
                    process.executable = std::filesystem::absolute(path);
                    return process;
                }
                const auto extension = path.extension();
                if (extension != ".js" && extension != ".mjs" && extension != ".cjs")
                    continue;
                auto node = find_native(paths, "node");
                if (node.empty())
                    continue;
                process.executable = std::move(node);
                process.arguments.insert(process.arguments.begin(),
                                         path_utf8(std::filesystem::absolute(path)));
                return process;
            }
        }
    throw Error("ExecutableMissing", "Native CLI not installed in the detected locations");
}

std::string default_model(const Json& input) {
    const auto& rows = input.is_array()                              ? input
                       : input.is_object() && input.contains("data") ? input.at("data")
                                                                     : Json::array();
    if (!rows.is_array())
        return {};
    for (const auto& row : rows)
        if (text_model(row) && row.is_object() &&
            (row.value("isDefault", Json()) == true || row.value("default", Json()) == true))
            if (auto id = model_id(row); !id.empty())
                return id;
    for (const auto& row : rows)
        if (auto id = model_id(row); text_model(row))
            return id;
    return {};
}

std::vector<DiscoveredConnection> ProviderRegistry::discover(const DiscoveryOptions& options,
                                                             const CallContext& context) const {
    if (options.probe_timeout.count() < 1 || options.probe_timeout > std::chrono::seconds(10))
        throw Error("InvalidOptions", "Discovery timeout must be between 1 ms and 10 seconds");
    std::vector<ProviderDescriptor> descriptors;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, entry] : entries_)
            descriptors.push_back(entry.first);
    }
    std::vector<DiscoveredConnection> result;
    for (const auto& descriptor : descriptors) {
        context.cancellation.check();
        DiscoveredConnection row;
        row.provider_id = descriptor.id;
        row.name = descriptor.name;
        row.state = "manual";
        const auto& hints = descriptor.discovery;
        auto configured = options.configured.find(descriptor.id);
        if (configured != options.configured.end())
            row.config = configured->second;
        try {
            if (!hints.executables.empty() || !row.config.process.executable.empty()) {
                row.config.process = resolve_provider_process(hints, row.config.process, options);
#ifdef _WIN32
                if (descriptor.id == "antigravity" && options.use_environment &&
                    !row.config.process.environment.contains("HOME")) {
                    auto home = environment("USERPROFILE");
                    if (!home.empty())
                        row.config.process.environment["HOME"] = home;
                }
#endif
                row.state = "available";
                row.source = "installed-cli";
            } else if (!hints.default_endpoint.empty() || !hints.endpoint_env.empty() ||
                       row.config.settings.contains("base_url")) {
                auto endpoint = row.config.settings.value("base_url", std::string());
                if (endpoint.empty() && options.use_environment && !hints.endpoint_env.empty())
                    endpoint = environment(hints.endpoint_env);
                if (endpoint.empty())
                    endpoint = hints.default_endpoint;
                if (descriptor.id == "ollama" && !endpoint.empty() &&
                    endpoint.find("://") == std::string::npos)
                    endpoint = "http://" + endpoint;
                if (endpoint.empty()) {
                    row.state = "needs-endpoint";
                } else {
                    row.config.settings["base_url"] = endpoint;
                    bool key_present = !row.config.api_key.empty();
                    if (!hints.api_key_env.empty()) {
                        row.api_key_env = hints.api_key_env;
                        key_present |=
                            options.use_environment && !environment(hints.api_key_env).empty();
                    }
                    const auto url = parse_url(endpoint);
                    const bool requires_key = descriptor.authentication == "api-key";
                    if (requires_key && !key_present) {
                        row.state = "needs-key";
                    } else if (url.loopback && options.probe_local_services) {
                        auto config = row.config;
                        config.settings["timeout_ms"] = options.probe_timeout.count();
                        if (config.api_key.empty() && key_present && options.use_environment)
                            config.api_key = environment(row.api_key_env);
                        auto provider = create(row.provider_id, config);
                        auto probe = context;
                        probe.cancellation = context.cancellation.child();
                        probe.cancellation.deadline = std::min(
                            probe.cancellation.deadline, Clock::now() + options.probe_timeout);
                        row.models = provider->list_models(probe);
                        row.state = default_model(row.models).empty() ? "no-models" : "available";
                        row.source = "local-service";
                    } else {
                        row.state = "available";
                        row.source = url.loopback || hints.local_service ? "configured-endpoint"
                                     : !row.config.api_key.empty()       ? "configured-api"
                                                                         : "environment-api";
                    }
                }
            }
        } catch (const Error& error) {
            context.cancellation.check();
            row.error_code = error.code;
            row.state = error.code == "ExecutableMissing" ? "not-installed" : "unavailable";
        } catch (const std::exception&) {
            row.state = "unavailable";
            row.error_code = "InvalidOptions";
        }
        result.push_back(std::move(row));
    }
    // Present useful choices first, with native accounts before API credentials.
    const auto rank = [](const DiscoveredConnection& row) {
        if (!row.available())
            return 100;
        if (row.provider_id == "codex")
            return 0;
        if (row.provider_id == "claude-code")
            return 1;
        if (row.provider_id == "antigravity")
            return 2;
        return row.source == "installed-cli" ? 3 : row.source == "local-service" ? 4 : 5;
    };
    std::stable_sort(result.begin(), result.end(),
                     [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    return result;
}
} // namespace cymcp
