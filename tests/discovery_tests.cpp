#include "test.hpp"
#include <cymcp/ai.hpp>
#include <fstream>
#include <httplib.h>
using namespace cymcp;
namespace {
void write(const std::filesystem::path& path, const Json& value) {
    private_directory(path.parent_path());
    std::ofstream(path, std::ios::binary) << value.dump();
}
const DiscoveredConnection& find(const std::vector<DiscoveredConnection>& rows,
                                 const std::string& id) {
    for (const auto& row : rows)
        if (row.provider_id == id)
            return row;
    throw std::runtime_error("Missing discovery profile");
}
struct ScopedEnv {
    std::string name, saved;
    bool had = false;
    static void put(const std::string& name, const std::string& value) {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        if (value.empty())
            unsetenv(name.c_str());
        else
            setenv(name.c_str(), value.c_str(), 1);
#endif
    }
    ScopedEnv(std::string n, const std::string& value) : name(std::move(n)) {
        if (auto previous = std::getenv(name.c_str()))
            saved = previous;
        put(name, value);
    }
    ~ScopedEnv() {
        put(name, saved);
    }
};
// A remote-catalogue provider: never local, catalogue order carries no recommendation.
struct CloudFixture final : Provider {
    bool failing_catalogue = false;
    bool uses_host_history() const noexcept override {
        return true;
    }
    ProviderCapabilities capabilities() const override {
        return {true, false, false, true};
    }
    void connect(const CallContext&) override {}
    Json list_models(const CallContext&) override {
        if (failing_catalogue)
            throw Error("ProviderError", "Catalogue temporarily unavailable");
        return Json::array({{{"id", "arbitrary-first-paid-model"}}});
    }
    Session start_session(std::string model, std::string, const CallContext&,
                          std::string) override {
        if (model.empty())
            throw Error("UnknownModel", "Select an exact model ID");
        return {"session", std::move(model)};
    }
    ChatResult send(Session&, Json&, const CallContext&) override {
        throw Error("Unsupported", "Fixture never generates");
    }
    void disconnect() noexcept override {}
};
struct Server {
    httplib::Server http;
    std::thread thread;
    std::atomic_int generations{0};
    std::string url;
    std::atomic_bool empty{false};
    Server() {
        http.Get("/v1/models", [this](const auto&, auto& response) {
            response.set_content(
                Json(
                    {{"data", empty ? Json::array() : Json::array({{{"id", "real-local-model"}}})}})
                    .dump(),
                "application/json");
        });
        http.Post("/v1/chat/completions", [this](const auto&, auto& response) {
            ++generations;
            response.status = 500;
        });
        auto port = http.bind_to_any_port("127.0.0.1");
        if (port < 1)
            throw std::runtime_error("Bind failed");
        url = "http://127.0.0.1:" + std::to_string(port) + "/v1";
        thread = std::thread([this] { http.listen_after_bind(); });
        http.wait_until_ready();
    }
    ~Server() {
        http.stop();
        if (thread.joinable())
            thread.join();
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            return 2;
        auto folder = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("cymcp-detection-" + random_token(16));
        private_directory(folder);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{folder};
        DiscoveryOptions options;
        options.use_environment = false;
        options.probe_local_services = false;
        options.executable_directories = {folder / "bin"};
        options.npm_roots = {folder / "npm/node_modules"};
        ProviderRegistry providers;
        auto rows = providers.discover(options);
        check(rows.size() == 22, "All built-in profiles have discovery results");
        check(find(rows, "codex").state == "not-installed",
              "Missing CLI distinguished from a connected account");
        check(find(rows, "openai").state == "needs-key",
              "Cloud API requires credentials independently of CLI accounts");
        check(find(rows, "openai-compatible").state == "needs-endpoint",
              "Generic compatible server never guesses an endpoint");
        auto native_name = [](const std::string& name) {
#ifdef _WIN32
            return name + ".exe";
#else
            return name;
#endif
        };
        private_directory(options.executable_directories.front());
        auto native = folder / "bin" / native_name("claude");
        std::filesystem::copy_file(std::filesystem::u8path(argv[1]), native);
        rows = providers.discover(options);
        check(find(rows, "claude-code").available(),
              "Installed CLI detected without configuration");
        check(find(rows, "claude-code").config.process.executable == native,
              "Resolved native executable passed to the connection");
        check(rows.front().provider_id == "claude-code",
              "Available native connections precede missing providers");
        ProviderConfig explicit_config;
        explicit_config.process.executable = folder / "missing.exe";
        options.configured["claude-code"] = explicit_config;
        check(find(providers.discover(options), "claude-code").state == "not-installed",
              "Broken explicit path does not silently use a different installation");
        options.configured.clear();
        auto npm = options.npm_roots.front() / "@anthropic-ai/claude-code";
        write(npm / "package.json", {{"bin", {{"claude", "bin/claude.exe"}}}});
        private_directory(npm / "bin");
        std::filesystem::copy_file(native, npm / "bin/claude.exe");
        std::filesystem::remove(native);
        rows = providers.discover(options);
        check(find(rows, "claude-code").config.process.executable == npm / "bin/claude.exe",
              "Native npm binary resolved from actual package metadata");
        auto codex = options.npm_roots.front() / "@openai/codex";
        write(codex / "package.json", {{"bin", {{"codex", "bin/codex.js"}}}});
        private_directory(codex / "bin");
        std::ofstream(codex / "bin/codex.js") << "// fixture; never executed";
        std::filesystem::copy_file(std::filesystem::u8path(argv[1]),
                                   folder / "bin" / native_name("node"));
        rows = providers.discover(options);
        const auto& resolved = find(rows, "codex").config.process;
        check(resolved.executable.filename() == native_name("node") &&
                  resolved.arguments.size() == 1 &&
                  std::filesystem::u8path(resolved.arguments.front()) == codex / "bin/codex.js",
              "npm script uses node with a literal argument instead of a shell shim");
        {
#if defined(_M_ARM64) || defined(__aarch64__)
            const std::string arch = "aarch64";
#else
            const std::string arch = "x86_64";
#endif
#ifdef _WIN32
            const std::string triple = arch + "-pc-windows-msvc";
#elif defined(__APPLE__)
            const std::string triple = arch + "-apple-darwin";
#else
            const std::string triple = arch + "-unknown-linux-musl";
#endif
            auto vendor = codex / "node_modules/@openai/codex-platform/vendor" / triple / "bin";
            private_directory(vendor);
            std::filesystem::copy_file(std::filesystem::u8path(argv[1]),
                                       vendor / native_name("codex"));
            auto vendored_rows = providers.discover(options);
            const auto& vendored = find(vendored_rows, "codex").config.process;
            check(vendored.executable == vendor / native_name("codex") &&
                      vendored.arguments.empty(),
                  "npm JS launcher is bypassed for its vendored native binary");
            std::filesystem::remove_all(codex / "node_modules");
        }
        write(codex / "package.json", {{"bin", {{"codex", "../../escape.js"}}}});
        check(!find(providers.discover(options), "codex").available(),
              "Package entry cannot escape the package with parent traversal");
        auto extension =
            ProviderDescriptor{"custom", "Host extension", "http", "optional-api-key", {}};
        extension.discovery.default_endpoint = "http://127.0.0.1:1/v1";
        extension.discovery.local_service = true;
        ProviderRegistry custom(false);
        custom.add(extension, [](const ProviderConfig& config) {
            return std::make_shared<HttpChatProvider>(
                HttpChatOptions{config.settings.at("base_url").get<std::string>(),
                                config.api_key,
                                {},
                                8,
                                std::chrono::milliseconds(200)});
        });
        Server server;
        ProviderConfig config;
        config.settings["base_url"] = server.url;
        options.configured["custom"] = config;
        options.probe_local_services = true;
        auto before = Clock::now();
        auto detected = custom.discover(options);
        check(detected.size() == 1 && detected.front().available(),
              "Host extension participates in the same automatic discovery");
        check(default_model(detected.front().models) == "real-local-model",
              "Local detection obtains actual model IDs");
        check(Clock::now() - before < std::chrono::seconds(2),
              "Local discovery completes within a bounded interval");
        check(server.generations == 0, "Discovery never submits a paid generation or tool request");
        Conversation chat(custom.create("custom", config), "");
        Json models;
        chat.connect({}, {}, true, &models);
        check(chat.session().model == "real-local-model" && models.size() == 1,
              "Automatic API connection selects a returned model without manual ID entry");
        check(server.generations == 0, "Opening an automatic connection sends no message");
        chat.disconnect();
        server.empty = true;
        check(find(custom.discover(options), "custom").state == "no-models",
              "Running service without models has an actionable separate state");
        CallContext cancelled;
        cancelled.cancellation.cancel();
        expect_error("Cancelled", [&] { custom.discover(options, cancelled); });
        options.probe_timeout = std::chrono::milliseconds(0);
        expect_error("InvalidOptions", [&] { custom.discover(options); });
        check(default_model(Json::array(
                  {{{"id", "first"}}, {{"id", "default"}, {"isDefault", true}}})) == "default",
              "Provider-declared default takes priority");
        check(default_model(Json::array()).empty(), "Empty catalogue never invents a model");
        check(default_model(Json::array(
                  {{{"id", "text-embedding-3-small"}}, {{"id", "chat-model"}}})) == "chat-model",
              "Automatic text chat skips embedding-only catalogue entries");
        check(providers.describe().dump().find("api_key") == std::string::npos,
              "Provider catalogue contains no credentials");
        {
            Json described = Json::object();
            for (const auto& row : providers.describe())
                described[row.at("id").get<std::string>()] = row.at("defaults");
            check(described.at("ollama").at("base_url") == "http://127.0.0.1:11434" &&
                      described.at("llama.cpp").at("base_url") == "http://127.0.0.1:8080/v1" &&
                      described.at("lmstudio").at("base_url") == "http://127.0.0.1:1234/v1",
                  "Local services publish their conventional endpoints");
            check(described.at("openai").at("credential_env") == "OPENAI_API_KEY" &&
                      described.at("anthropic").at("credential_env") == "ANTHROPIC_API_KEY",
                  "Cloud APIs publish the conventional credential variable name");
            check(described.at("codex").at("settings").at("turn_timeout_ms") == 0 &&
                      described.at("openai").at("settings").at("max_output_tokens") == 0 &&
                      described.at("openai").at("settings").at("max_tool_rounds") == 0 &&
                      described.at("claude-code").at("settings").at("setting_sources") == "user" &&
                      described.at("gemini").at("settings").at("acp_arguments") ==
                          Json::array({"--acp"}),
                  "Every built-in profile publishes its default settings");
            check(described.at("claude-code").at("executables") == Json::array({"claude"}),
                  "CLI profiles publish the native executable names they detect");
            check(described.at("claude-code").at("login_arguments") ==
                          Json::array({"auth", "login"}) &&
                      described.at("codex").at("login_arguments") == Json::array({"login"}) &&
                      described.at("antigravity").at("login_arguments").empty(),
                  "CLI profiles publish their own sign-in command for a host console");
        }
        {
            auto cloud = std::make_shared<CloudFixture>();
            Conversation automatic(cloud, "");
            Json catalogue;
            expect_error("UnknownModel", [&] { automatic.connect({}, {}, true, &catalogue); });
            check(catalogue.size() == 1,
                  "Remote API never opens on an arbitrary model but returns its catalogue");
            cloud->failing_catalogue = true;
            Conversation chosen(cloud, "explicit-model");
            Json none = nullptr;
            chosen.connect({}, {}, true, &none);
            check(chosen.session().model == "explicit-model" && none.empty(),
                  "A failing optional catalogue never fails a valid session");
            chosen.disconnect();
        }
        {
            DiscoveryOptions environment;
            environment.probe_local_services = false;
            ScopedEnv ollama("OLLAMA_HOST", "0.0.0.0");
            ScopedEnv anthropic("ANTHROPIC_BASE_URL", "https://proxy.example.invalid/");
            auto env_rows = providers.discover(environment);
            check(find(env_rows, "ollama").config.settings.at("base_url") ==
                      "http://127.0.0.1:11434",
                  "OLLAMA_HOST bind-all address without port reaches the loopback default port");
            check(find(env_rows, "anthropic").config.settings.at("base_url") ==
                      "https://proxy.example.invalid/v1",
                  "ANTHROPIC_BASE_URL follows the SDK convention without the version segment");
            ScopedEnv remote("OLLAMA_HOST", "https://ollama.example.invalid");
            check(find(providers.discover(environment), "ollama").config.settings.at("base_url") ==
                      "https://ollama.example.invalid:443",
                  "OLLAMA_HOST keeps an explicit scheme and its default port");
        }
        {
            const char* name = "CYMCP_DISCOVERY_TEST_KEY";
            const auto previous = std::getenv(name);
            const std::string saved = previous ? previous : "";
            struct Restore {
                const char* name;
                std::string value;
                ~Restore() {
#ifdef _WIN32
                    _putenv_s(name, value.c_str());
#else
                    if (value.empty())
                        unsetenv(name);
                    else
                        setenv(name, value.c_str(), 1);
#endif
                }
            } restore{name, saved};
#ifdef _WIN32
            _putenv_s(name, "fixture-value-never-exported");
#else
            setenv(name, "fixture-value-never-exported", 1);
#endif
            ProviderRegistry cloud(false);
            ProviderDescriptor recipe{"test-api", "Test API", "http", "api-key", {}};
            recipe.discovery.default_endpoint = "https://api.example.invalid/v1";
            recipe.discovery.api_key_env = name;
            cloud.add(recipe, [](const ProviderConfig&) -> std::shared_ptr<Provider> {
                throw std::runtime_error("Discovery must not instantiate a remote provider");
            });
            auto cloud_rows = cloud.discover();
            check(cloud_rows.front().available(),
                  "API environment key detected without a remote request");
            check(cloud_rows.front().api_key_env == name &&
                      cloud_rows.front().config.api_key.empty(),
                  "Discovery returns the environment variable name rather than copying its "
                  "credential");
        }
        std::cout << checks << " discovery checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
