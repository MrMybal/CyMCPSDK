#include <cstdlib>
#include <cymcp/ai.hpp>
#include <fstream>
#include <iostream>
using namespace cymcp;
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int n = 1; n < argc; ++n)
        args.push_back(path_utf8(std::filesystem::path(argv[n])));
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
#endif
    try {
        ProviderRegistry providers;
        if (args.size() == 1 && args.front() == "providers") {
            std::cout << providers.describe().dump(2) << '\n';
            return 0;
        }
        bool models = args.size() == 2 && args.front() == "models";
        if (args.size() != 1 && !models) {
            std::cerr << "Usage: cymcp_chat providers | models CONFIG.json | CONFIG.json\n"
                         "Then one JSON user message per stdin line: {\"text\":\"Hello\"}\n";
            return 2;
        }
        std::ifstream input(std::filesystem::u8path(args.back()), std::ios::binary);
        if (!input)
            throw Error("InvalidOptions", "Cannot open chat configuration");
        std::string data;
        for (char c; input.get(c);) {
            data += c;
            if (data.size() > 1024 * 1024)
                throw Error("InvalidOptions", "Configuration exceeds limit");
        }
        auto config = parse_json(data);
        if (config.contains("api_key"))
            throw Error("InvalidOptions", "Use api_key_env, not a key stored in JSON");
        ProviderConfig options;
        options.settings = config.value("settings", Json::object());
        auto key_env = config.value("api_key_env", std::string());
        if (!key_env.empty()) {
            if (key_env.find_first_not_of(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
                std::string::npos)
                throw Error("InvalidOptions", "Invalid API key environment variable name");
            if (const char* key = std::getenv(key_env.c_str()))
                options.api_key = key;
        }
        auto process = config.value("process", Json::object());
        options.process.executable =
            std::filesystem::u8path(process.value("executable", std::string()));
        options.process.arguments = process.value("arguments", std::vector<std::string>());
        options.process.cwd = std::filesystem::u8path(process.value("cwd", std::string()));
        auto provider = providers.create(config.at("provider").get<std::string>(), options);
        if (models) {
            provider->connect();
            std::cout << provider->list_models().dump(2) << '\n';
            return 0;
        }
        Conversation chat(provider, config.value("model", std::string()),
                          config.value("system", std::string()));
        CallContext context;
        context.events = [](const Event& event) {
            std::cout << Json({{"type", event.type}, {"data", event.data}}).dump() << '\n'
                      << std::flush;
        };
        chat.connect(config.value("resume_id", std::string()), context);
        std::string line;
        for (char c; std::cin.get(c);) {
            if (c != '\n') {
                line += c;
                if (line.size() > 1024 * 1024)
                    throw Error("InvalidParameters", "Input line exceeds limit");
                continue;
            }
            if (line.empty())
                continue;
            auto message = parse_json(line);
            line.clear();
            chat.send(message.at("text"), context);
            std::cout << Json({{"type", "SessionUpdated"}, {"id", chat.session().id}}).dump()
                      << '\n'
                      << std::flush;
        }
        if (!line.empty())
            throw Error("UnexpectedEOF", "Input message must end with a newline");
        return 0;
    } catch (const Error& error) {
        std::cerr << Json({{"error", error.code}, {"message", error.what()}}).dump() << '\n';
        return 1;
    } catch (const std::exception&) {
        std::cerr << "Invalid chat configuration or provider event\n";
        return 1;
    }
}
