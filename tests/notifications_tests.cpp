#include "test.hpp"
#include <cymcp/changes.hpp>
#include <cymcp/mcp.hpp>
#include <cymcp/performance.hpp>
using namespace cymcp;
using namespace std::chrono_literals;
struct Host {
    std::shared_ptr<ChangeFeed> feed = std::make_shared<ChangeFeed>();
    std::shared_ptr<Registry> tools = std::make_shared<Registry>(Policy::allow_all());
    std::shared_ptr<ResourceRegistry> resources =
        std::make_shared<ResourceRegistry>(Policy::allow_all());
    std::shared_ptr<PerformanceMonitor> monitor = std::make_shared<PerformanceMonitor>();
    std::unique_ptr<McpServer> server;
    int generation = 0;
    Resource note(std::string text) {
        auto bytes = std::make_shared<const std::string>(std::move(text));
        Resource r;
        r.uri = "app://state";
        r.name = "State";
        r.size = bytes->size();
        r.cache_ttl = 30s;
        r.read = [bytes](auto o, auto n, const auto&) { return bytes->substr(o, n); };
        return r;
    }
    Host() {
        tools->set_change_feed(feed);
        resources->set_change_feed(feed);
        resources->add(note("before"));
        tools->add({"mutate",
                    "Mutate",
                    {{"type", "object"}},
                    {{"type", "object"}},
                    false,
                    [this](const auto& arguments, const auto&) {
                        ++generation;
                        if (arguments.value("delete", false))
                            resources->remove("app://state");
                        else
                            resources->replace(note("after"));
                        if (generation == 1)
                            tools->add({"new_tool",
                                        "New tool",
                                        {{"type", "object"}},
                                        Json::object(),
                                        true,
                                        [](const auto&, const auto&) { return Json::object(); }});
                        return Json{{"generation", generation}};
                    }});
        McpServerOptions options;
        options.resources = resources;
        options.changes = feed;
        options.performance = monitor;
        server = std::make_unique<McpServer>(*tools, "Change fixture", options);
    }
};
void exercise(McpClient& client) {
    client.connect();
    client.start_notifications({{"toolsListChanged", true},
                                {"resourcesListChanged", true},
                                {"resourceSubscriptions", Json::array({"app://state"})}});
    check(client.notification_status()["running"] == true,
          "Subscription acknowledgment opens an owned notification reader");
    check(client.read_resource("app://state")["contents"][0]["text"] == "before",
          "Initial resource is cached with positive TTL");
    client.read_resource("app://state");
    check(client.list_tools().size() == 1, "Initial tool catalogue is cached");
    client.call_tool("mutate", Json::object());
    const auto deadline = Clock::now() + 3s;
    while (client.notification_status()["revision"].get<unsigned>() < 2 && Clock::now() < deadline)
        std::this_thread::sleep_for(5ms);
    check(client.read_resource("app://state")["contents"][0]["text"] == "after" &&
              client.list_tools().size() == 2,
          "Incoming changes invalidate resource and catalogue caches before TTL expires");
    auto notifications = client.drain_notifications();
    check(!notifications.empty() &&
              notifications.front()["method"] == "notifications/subscriptions/acknowledged",
          "Acknowledgment is the first message on each subscription");
    bool ack = false, tools = false, updated = false;
    for (const auto& n : notifications) {
        check(n["params"]["_meta"].contains("io.modelcontextprotocol/subscriptionId"),
              "Every streamed notification carries its subscription ID");
        auto method = n["method"].get<std::string>();
        ack |= method == "notifications/subscriptions/acknowledged";
        tools |= method == "notifications/tools/list_changed";
        updated |= method == "notifications/resources/updated";
    }
    check(ack && tools && updated,
          "Acknowledgment and both change notifications arrive on the stream");
    client.stop_notifications();
    check(!client.notification_status()["running"].get<bool>(),
          "Notification reader stops explicitly");
    check(!client.call_tool("mutate", Json::object()).value("isError", true),
          "Ordinary requests still work after stopping notifications");
    client.start_notifications({{"resourceSubscriptions", Json::array({"app://state"})}});
    client.call_tool("mutate", Json::object());
    const auto until = Clock::now() + 3s;
    while (client.notification_status()["revision"].get<unsigned>() < 1 && Clock::now() < until)
        std::this_thread::sleep_for(5ms);
    for (const auto& n : client.drain_notifications())
        check(n["method"] != "notifications/tools/list_changed" &&
                  n["method"] != "notifications/resources/list_changed",
              "Only explicitly requested notification types are emitted");
    client.read_resource("app://state");
    const auto before_delete = client.notification_status()["revision"].get<unsigned>();
    client.call_tool("mutate", {{"delete", true}});
    const auto delete_deadline = Clock::now() + 3s;
    while (client.notification_status()["revision"].get<unsigned>() == before_delete &&
           Clock::now() < delete_deadline)
        std::this_thread::sleep_for(5ms);
    expect_error("RemoteError", [&] { client.read_resource("app://state"); });
    client.stop_notifications();
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--server") {
            Host host;
            host.server->serve_stdio(std::cin, std::cout, 1);
            return 0;
        }
        auto monitor = std::make_shared<PerformanceMonitor>();
        McpClientOptions options;
        options.performance = monitor;
        {
            Host host;
            const auto token = random_token();
            LocalHttpServer http(
                token, [&](const auto& r, const auto& c) { return host.server->handle(r, c); });
            McpClient client("http://127.0.0.1:" + std::to_string(http.start()) + "/mcp", token,
                             options);
            exercise(client);
            check(!host.monitor->snapshot()["operations"].empty(),
                  "MCP server records optional performance metadata");
        }
        {
            McpClient client(ProcessOptions{std::filesystem::absolute(argv[0]), {"--server"}},
                             options);
            exercise(client);
        }
        check(!monitor->snapshot()["operations"].empty(),
              "MCP client records optional cache and transport performance metadata");
        std::cout << checks << " notification checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
