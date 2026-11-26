#include <csignal>
#include <cymcp/mcp.hpp>
#include <fstream>
#include <iostream>
namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) {
    stopped = 1;
}
} // namespace
int main(int argc, char** argv) {
    using namespace cymcp;
    Registry operations(Policy::allow_all());
    operations.add(
        {"echo",
         "Return text supplied by the caller",
         {{"type", "object"},
          {"properties", {{"text", {{"type", "string"}, {"maxLength", 4096}}}}},
          {"required", {"text"}},
          {"additionalProperties", false}},
         {{"type", "object"},
          {"properties", {{"text", {{"type", "string"}}}}},
          {"required", {"text"}}},
         true,
         [](const Json& args, const CallContext&) { return Json{{"text", args.at("text")}}; }});
    operations.add({"sum",
                    "Add two numbers",
                    {{"type", "object"},
                     {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}},
                     {"required", {"a", "b"}},
                     {"additionalProperties", false}},
                    {{"type", "object"}},
                    true,
                    [](const Json& args, const CallContext&) {
                        return Json{{"value", args["a"].get<double>() + args["b"].get<double>()}};
                    }});
    try {
        std::string command = argc > 1 ? argv[1] : "describe";
        McpServer mcp(operations, "CyMCPSDK example");
        if (command == "describe") {
            std::cout << operations.describe().dump(2) << '\n';
            return 0;
        }
        if (command == "mcp") {
            mcp.serve_stdio(std::cin, std::cout);
            return 0;
        }
        if (command == "invoke" && argc == 4) {
            std::ifstream input(argv[3]);
            if (!input)
                throw Error("FileError", "Cannot read arguments file");
            Json args;
            input >> args;
            std::cout << operations.invoke(argv[2], args).dump() << '\n';
            return 0;
        }
        if (command == "jsonl") {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line.size() > 1024 * 1024)
                    throw Error("InvalidRequest", "Message too large");
                std::cout << dispatch_app(operations, Json::parse(line)).dump() << '\n'
                          << std::flush;
            }
            return 0;
        }
#ifdef CYMCP_HAS_HTTP
        if (command == "serve") {
            int port = 0;
            bool tokenless = argc > 2 && std::string_view(argv[argc - 1]) == "--no-token";
            const int positional = argc - (tokenless ? 1 : 0);
            if (positional > 3)
                throw Error("InvalidArguments", "Usage: cymcp_example serve [PORT] [--no-token]");
            if (positional == 3) {
                std::size_t consumed = 0;
                port = std::stoi(argv[2], &consumed);
                if (consumed != std::string_view(argv[2]).size() || port < 0 || port > 65535)
                    throw Error("InvalidArguments", "Port must be between 0 and 65535");
            }
            auto endpoint = [&](const Json& request, const CallContext& context) {
                if (request.contains("jsonrpc"))
                    return mcp.handle(request, context);
                return dispatch_app(operations, request, context);
            };
            std::unique_ptr<LocalHttpServer> server;
            LocalMcpEndpoint advertisement;
            advertisement.name = "CyMCPSDK example";
            advertisement.version = version;
            if (tokenless) {
                server = std::make_unique<LocalHttpServer>(
                    LocalHttpServer::UnauthenticatedLoopback{}, endpoint, 4,
                    provider_message_limit, advertisement);
            } else {
                const char* key = std::getenv("CYMCP_TOKEN");
                if (!key)
                    throw Error("AuthenticationRequired",
                                "Set CYMCP_TOKEN to a strong random token (32+ characters)");
                server = std::make_unique<LocalHttpServer>(std::string(key), endpoint, 4,
                                                          provider_message_limit, advertisement);
            }
            port = server->start(port);
            std::signal(SIGINT, stop);
            std::signal(SIGTERM, stop);
            std::cerr << "Listening on http://127.0.0.1:" << port << " (/v1, /mcp)\n";
            while (!stopped)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            server->stop();
            return 0;
        }
#endif
        throw Error("InvalidArguments", "Usage: cymcp_example describe | invoke NAME PARAMS.json | "
                                        "jsonl | mcp | serve [PORT] [--no-token]");
    } catch (const Error& e) {
        std::cerr << Json({{"error", {{"code", e.code}, {"message", e.what()}}}}).dump() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Invalid input\n";
        return 1;
    }
}
