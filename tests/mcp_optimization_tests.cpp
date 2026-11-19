#include "test.hpp"
#include <cymcp/mcp.hpp>
#include <cymcp/search.hpp>
#include <fstream>
#include <set>
using namespace cymcp;
using namespace std::chrono_literals;
namespace {
auto operations(std::atomic_int& calls) {
    auto target = std::make_shared<Registry>(Policy::allow_all());
    for (int n = 0; n < 80; ++n)
        target->add({"op_" + std::to_string(n),
                     "Operation " + std::to_string(n),
                     {{"type", "object"}},
                     {{"type", "object"}},
                     true,
                     [&calls](const Json& args, const CallContext&) {
                         ++calls;
                         return args;
                     }});
    target->add({"large",
                 "Large result",
                 {{"type", "object"}},
                 {{"type", "object"}},
                 true,
                 [&calls](const auto&, const auto&) {
                     ++calls;
                     return Json{{"text", std::string(200000, 'x') + " é🙂"}};
                 }});
    return target;
}
std::string collect(McpClient& client, const std::string& uri) {
    auto manifest = client.read_resource(uri);
    const auto metadata = parse_json(manifest["contents"][0]["text"].get<std::string>());
    auto next = metadata.at("firstUri").get<std::string>();
    std::string bytes;
    do {
        auto part = client.read_resource(next);
        bytes += part["contents"][0]["text"].get<std::string>();
        auto page = part.at("_meta").at("com.cymcp/chunk");
        next = page.value("nextUri", std::string());
    } while (!next.empty());
    return bytes;
}
int fixture() {
    std::atomic_int calls{0};
    ToolGateway gateway(operations(calls));
    McpServerOptions options;
    options.resources = gateway.resources();
    options.page_size = 2;
    McpServer(*gateway.tools(), "On-demand fixture", options).serve_stdio(std::cin, std::cout);
    return 0;
}
void gateway_transport(McpClient& client) {
    client.connect();
    check(client.list_tools().size() == 5,
          "Bootstrap tools are compatible with paginated MCP discovery");
    auto search = client.call_tool("cy_tools_search", {{"query", "op_17"}});
    check(search["structuredContent"]["tools"].size() == 1,
          "MCP searches without retrieving every operation schema");
    auto schema = client.call_tool("cy_tools_describe", {{"name", "op_17"}});
    check(schema["structuredContent"]["name"] == "op_17",
          "MCP retrieves the selected full schema only");
    check(client.call_tool("cy_tools_call",
                           {{"name", "op_17"},
                            {"arguments", {{"number", 17}}}})["structuredContent"]["number"] == 17,
          "MCP invokes a discovered operation with its actual arguments");
    auto large = client.call_tool("cy_tools_call", {{"name", "large"}});
    check(large.dump().size() < 2048 && large["content"].size() == 2 &&
              large["content"][1]["type"] == "resource_link",
          "Large MCP results become small standard resource links plus a typed handle");
    auto data = parse_json(
        collect(client, large["structuredContent"]["resource"]["uri"].get<std::string>()));
    check(data["text"] == std::string(200000, 'x') + " é🙂",
          "MCP resource reads reconstruct the entire large result");
    check(client.list_resources().empty(),
          "Private result resources are not advertised to other clients");
}
void cache_and_pagination() {
    std::atomic_int calls{0}, list_calls{0}, get_calls{0}, read_calls{0};
    auto target = operations(calls);
    auto resources = std::make_shared<ResourceRegistry>(Policy::allow_all());
    auto text = std::make_shared<const std::string>("cached text");
    Resource resource;
    resource.uri = "app://notes/é";
    resource.name = "Notes";
    resource.size = text->size();
    resource.cache_ttl = 30ms;
    resource.read = [text](auto offset, auto length, const auto& c) {
        c.cancellation.check();
        return text->substr(offset, length);
    };
    resources->add(resource);
    McpServerOptions options;
    options.page_size = 3;
    options.cache_ttl = 30ms;
    options.resources = resources;
    McpServer mcp(*target, "Cache fixture", options);
    const auto token = random_token();
    LocalHttpServer server(token, [&](const Json& request, const CallContext& ctx) {
        const auto method = request.value("method", std::string());
        if (method == "tools/list")
            ++list_calls;
        if (method == "cymcp/tools/get")
            ++get_calls;
        if (method == "resources/read")
            ++read_calls;
        return mcp.handle(request, ctx);
    });
    McpClient client("http://127.0.0.1:" + std::to_string(server.start()) + "/mcp", token);
    client.connect();
    auto first = client.get_tool("op_1");
    client.get_tool("op_1");
    check(get_calls == 1 && list_calls == 0 && client.cache_stats().hits > 0,
          "Tool lookup caches one schema without a full catalogue fetch");
    client.call_tool("op_1", {{"a", 1}});
    client.call_tool("op_1", {{"a", 1}});
    check(calls == 2, "Tool execution results are never cached or replayed");
    auto page = client.list_tools_page();
    check(page["tools"].size() == 3 && page.contains("nextCursor"),
          "MCP clients may load one catalogue page at a time");
    target->add({"added",
                 "Added",
                 {{"type", "object"}},
                 {{"type", "object"}},
                 true,
                 [](const auto&, const auto&) { return Json::object(); }});
    expect_error("RemoteError",
                 [&] { client.list_tools_page(page["nextCursor"].get<std::string>()); });
    client.clear_cache();
    check(client.list_tools().size() == 82,
          "Full legacy-style discovery still visits every paginated tool");
    client.read_resource(resource.uri);
    client.read_resource(resource.uri);
    check(read_calls == 1, "Resource cache uses the complete URI as its key");
    std::this_thread::sleep_for(40ms);
    client.read_resource(resource.uri);
    check(read_calls == 2, "Expired resource TTL triggers a fresh read on demand");
    client.clear_cache();
    client.read_resource(resource.uri);
    check(read_calls == 3, "Explicit cache invalidation refetches resource contents");
    check(client.cache_stats().bytes <= 16 * 1024 * 1024,
          "Client cache respects its serialized-byte budget");
    auto other = std::make_shared<const std::string>("new");
    resource.size = other->size();
    resource.cache_ttl = 0ms;
    resource.read = [other](auto o, auto n, const auto&) { return other->substr(o, n); };
    resources->replace(resource);
    client.clear_cache();
    client.read_resource(resource.uri);
    client.read_resource(resource.uri);
    check(read_calls == 5, "Zero-TTL resources are not cached");
}
void grep_workspace() {
    const auto folder =
        std::filesystem::temp_directory_path() / ("cymcp-search-" + random_token(16));
    private_directory(folder);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{folder};
    std::string text;
    for (int n = 0; n < 120; ++n)
        text += "row " + std::to_string(n) + " needle é🙂\n";
    std::ofstream(folder / "data.txt", std::ios::binary) << text;
    std::ofstream(folder / "other.md") << "NEEDLE uppercase\n";
    std::filesystem::create_directory(folder / ".git");
    std::ofstream(folder / ".git" / "secret.md") << "NEEDLE private metadata\n";
    std::ofstream(folder / ".gitignore") << "ignored.txt\n";
    std::ofstream(folder / "ignored.txt") << "ignored-only\n";
    std::ofstream(folder / "nonutf8.txt", std::ios::binary) << "needle \xff\n";
    FileSearchOptions options;
    options.root = folder;
    options.page_size = 7;
    FileSearch search(options);
    auto registry = std::make_shared<Registry>(Policy::allow_all());
    search.add_to(*registry);
    std::size_t rows = 0;
    std::set<std::size_t> lines;
    Json args = {{"pattern", "needle"}, {"glob", "data.txt"}, {"regex", false}};
    for (int n = 0; n < 100; ++n) {
        auto result = registry->invoke("workspace.grep", args);
        for (const auto& match : result["matches"]) {
            ++rows;
            lines.insert(match["line"].get<std::size_t>());
            check(match["path"] == "data.txt",
                  "Workspace grep returns root-relative matching file paths");
        }
        if (result["complete"].get<bool>())
            break;
        args = {{"cursor", result.at("nextCursor")}};
    }
    check(rows == 120 && lines.size() == 120,
          "Fast grep pages through all matches without omissions or duplicates");
    auto regex = search.grep({{"pattern", "row (0|1) needle"}, {"glob", "*.txt"}});
    check(regex["matches"].size() == 2 && regex["complete"].get<bool>(),
          "Workspace grep supports bounded regular-expression matching");
    auto insensitive =
        search.grep({{"pattern", "needle"}, {"glob", "*.md"}, {"ignore_case", true}});
    check(insensitive["matches"].size() == 1, "Grep supports ignore-case and file globs");
    check(search.grep({{"pattern", "ignored-only"}, {"glob", "ignored.txt"}})["matches"].size() ==
              1,
          "An explicit file glob follows ripgrep's override semantics");
    check(search.grep({{"pattern", "ignored-only"}})["matches"].empty() &&
              search.grep({{"pattern", "ignored-only"}, {"include_ignored", true}})["matches"]
                      .size() == 1,
          "Ignored files are included only on explicit broadening or file selection");
    check(search.grep({{"pattern", "needle"}, {"glob", "nonutf8.txt"}})["skippedNonUtf8"] == 1,
          "Non-UTF-8 matches are counted instead of silently disappearing");
    std::string recovered;
    Json read_args = {{"path", "data.txt"}, {"length", 17}};
    std::string revision;
    while (true) {
        auto part = search.read(read_args);
        recovered += part.at("text").get<std::string>();
        revision = part.at("revision");
        if (part["complete"].get<bool>())
            break;
        read_args = {{"path", "data.txt"},
                     {"offset", part.at("nextOffset")},
                     {"length", 17},
                     {"revision", revision}};
    }
    check(recovered == text, "Workspace range reads preserve complete UTF-8 file contents");
    expect_error("UnsafePath", [&] { search.read({{"path", "../outside.txt"}}); });
    expect_error("UnsafePath", [&] { search.read({{"path", path_utf8(folder / "data.txt")}}); });
    std::ofstream(folder / "data.txt", std::ios::app) << "changed\n";
    expect_error("ResourceChanged",
                 [&] { search.read({{"path", "data.txt"}, {"revision", revision}}); });
    expect_error("SearchError", [&] { search.grep({{"pattern", "["}}); });
    CallContext cancelled;
    cancelled.cancellation.cancel();
    expect_error("Cancelled", [&] { search.grep({{"pattern", "needle"}}, cancelled); });
    CallContext alice;
    alice.principal = "alice";
    CallContext bob;
    bob.principal = "bob";
    const auto private_page = search.grep({{"pattern", "needle"}}, alice);
    expect_error("SearchExpired",
                 [&] { search.grep({{"cursor", private_page.at("nextCursor")}}, bob); });
    search.grep({{"cursor", private_page.at("nextCursor")}}, alice);
    expect_error("SearchExpired",
                 [&] { search.grep({{"cursor", private_page.at("nextCursor")}}, alice); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--serve")
            return fixture();
        std::atomic_int calls{0};
        ToolGateway gateway(operations(calls));
        McpServerOptions options;
        options.resources = gateway.resources();
        options.page_size = 2;
        McpServer mcp(*gateway.tools(), "HTTP gateway", options);
        const auto token = random_token();
        LocalHttpServer server(token,
                               [&](const auto& r, const auto& c) { return mcp.handle(r, c); });
        McpClient http("http://127.0.0.1:" + std::to_string(server.start()) + "/mcp", token);
        gateway_transport(http);
        McpClient stdio(ProcessOptions{std::filesystem::absolute(argv[0]), {"--serve"}});
        gateway_transport(stdio);
        cache_and_pagination();
        try {
            grep_workspace();
        } catch (const Error& error) {
            if (error.code != "ExecutableMissing")
                throw;
            std::cout << "Optional filesystem grep checks skipped: ripgrep is not installed\n";
        }
        std::cout << checks << " MCP optimization and grep checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
