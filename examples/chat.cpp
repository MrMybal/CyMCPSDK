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
        bool benchmark = args.size() == 2 && args.front() == "benchmark";
        if (args.size() != 1 && !models && !benchmark) {
            std::cerr << "Usage: cymcp_chat providers | models CONFIG.json | benchmark CONFIG.json "
                         "| CONFIG.json\n"
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
        std::unique_ptr<ConversationStore> store;
        std::string storage_id;
        if (config.contains("history")) {
            const auto& history = config.at("history");
            store = std::make_unique<ConversationStore>(
                std::filesystem::u8path(history.at("directory").get<std::string>()));
            storage_id = history.at("id").get<std::string>();
        }
        CallContext context;
        std::size_t process_starts = 0;
        context.events = [&](const Event& event) {
            if (event.type == "ProcessStarted")
                ++process_starts;
            if (benchmark)
                return;
            std::cout << Json({{"type", event.type}, {"data", event.data}}).dump() << '\n'
                      << std::flush;
        };
        auto begin = Clock::now();
        if (store && config.at("history").value("restore", false))
            chat.restore(store->load(storage_id), context);
        else
            chat.connect(config.value("resume_id", std::string()), context);
        auto connect_ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        if (benchmark) {
            auto turns = config.value("benchmark_turns", 3);
            if (turns < 1 || turns > 20)
                throw Error("InvalidOptions", "Benchmark requires 1..20 turns");
            Json measurements = Json::array();
            bool passed = true;
            for (int n = 0; n < turns; ++n) {
                auto result = chat.send("Reply exactly with CYMCP_OK. Do not use tools.", context);
                auto measurement = result.metrics;
                measurement["turn"] = n + 1;
                measurement["cold"] = n == 0;
                measurement["usage"] = result.usage;
                auto reply = result.text;
                auto first = reply.find_first_not_of(" \r\n\t"),
                     last = reply.find_last_not_of(" \r\n\t");
                measurement["expected_reply"] = first != std::string::npos &&
                                                reply.substr(first, last - first + 1) == "CYMCP_OK";
                passed = passed && measurement.at("expected_reply").get<bool>();
                measurements.push_back(std::move(measurement));
            }
            if (store)
                store->save(storage_id, chat);
            std::cout << Json({{"provider", provider->identity()},
                               {"model", chat.session().model},
                               {"connect_ms", connect_ms},
                               {"process_starts", process_starts},
                               {"passed", passed},
                               {"turns", measurements}})
                             .dump(2)
                      << '\n';
            chat.disconnect();
            return passed ? 0 : 1;
        }
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
            auto command = message.value("command", std::string());
            if (!command.empty()) {
                if (command == "reconnect")
                    chat.reconnect(context);
                else if (command == "disconnect")
                    chat.disconnect();
                else if (command == "save" && store)
                    store->save(storage_id, chat);
                else
                    throw Error("InvalidParameters", "Unknown conversation command");
                continue;
            }
            auto content = message.contains("content") ? message.at("content")
                                                       : message.value("text", Json(""));
            if (message.contains("attachments")) {
                if (content.is_string())
                    content = Json::array({text_block(content.get<std::string>())});
                for (const auto& attachment : message.at("attachments"))
                    content.push_back(attachment_from_file(
                        std::filesystem::u8path(attachment.at("path").get<std::string>()),
                        attachment.value("mime_type", std::string())));
            }
            try {
                chat.send(content, context);
            } catch (const Error& error) {
                if (store)
                    store->save(storage_id, chat);
                std::cout << Json({{"type", "Error"}, {"code", error.code}}).dump() << '\n'
                          << std::flush;
                continue; // the host explicitly chooses reconnect/new messages; no implicit retry
            }
            if (store)
                store->save(storage_id, chat);
            std::cout << Json({{"type", "SessionUpdated"}, {"id", chat.session().id}}).dump()
                      << '\n'
                      << std::flush;
        }
        if (!line.empty())
            throw Error("UnexpectedEOF", "Input message must end with a newline");
        chat.disconnect();
        return 0;
    } catch (const Error& error) {
        std::cerr << Json({{"error", error.code}, {"message", error.what()}}).dump() << '\n';
        return 1;
    } catch (const std::exception&) {
        std::cerr << "Invalid chat configuration or provider event\n";
        return 1;
    }
}
