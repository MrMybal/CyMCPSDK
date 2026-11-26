#include <cymcp/changes.hpp>
#include <cymcp/core.hpp>
#include <cymcp/performance.hpp>
#include <cymcp/resources.hpp>
#ifdef CONSUMER_HAS_MCP_HTTP
#include <cymcp/mcp.hpp>
#endif
#ifdef CONSUMER_HAS_AI
#include <cymcp/ai.hpp>
#endif
int main() {
#ifdef CONSUMER_HAS_AI
    auto provider = std::make_shared<cymcp::HttpChatProvider>(cymcp::HttpChatOptions{});
    cymcp::Conversation conversation(provider, "selected-model");
    conversation.configure_context({4096, 4, 256, {}});
    cymcp::ProviderRegistry providers;
    auto api = providers.create("ollama");
    if (providers.describe().size() != 23 || !api->capabilities().streaming)
        return 2;
    auto content = cymcp::Json::array({cymcp::text_block("portable content")});
    cymcp::validate_content(content);
    if (conversation.checkpoint().at("version") != 1 ||
        conversation.status().at("state") != "disconnected")
        return 3;
#endif
    auto operations = std::make_shared<cymcp::Registry>(cymcp::Policy::allow_all());
    auto performance = std::make_shared<cymcp::PerformanceMonitor>();
    operations->set_performance_monitor(performance);
    operations->set_change_feed(std::make_shared<cymcp::ChangeFeed>());
    cymcp::ToolGateway gateway(operations);
#ifdef CONSUMER_HAS_MCP_HTTP
    cymcp::McpServer mcp(*operations, "Installed consumer");
    cymcp::LocalMcpEndpoint identity;
    identity.name = "Installed consumer";
    identity.version = cymcp::version;
    cymcp::LocalHttpServer server(cymcp::LocalHttpServer::UnauthenticatedLoopback{},
        [&](const auto& message, const auto& context) { return mcp.handle(message, context); },
        4, cymcp::provider_message_limit, identity);
    const auto endpoint = "http://127.0.0.1:" + std::to_string(server.start()) + "/mcp";
    const auto found = cymcp::probe_local_mcp(endpoint, {}, std::chrono::seconds(2));
    if (found.status != cymcp::LocalMcpStatus::active || found.name != identity.name)
        return 4;
    bool inventory_available = false;
    (void)cymcp::windows_mcp_listeners(inventory_available);
#endif
    return operations->describe().empty() && gateway.tools()->describe().size() == 5 &&
                   gateway.tools()
                       ->invoke("cy_tools_search", cymcp::Json::object())["tools"]
                       .empty()
               ? 0
               : 1;
}
