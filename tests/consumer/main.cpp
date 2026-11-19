#include <cymcp/changes.hpp>
#include <cymcp/core.hpp>
#include <cymcp/performance.hpp>
#include <cymcp/resources.hpp>
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
    return operations->describe().empty() && gateway.tools()->describe().size() == 5 &&
                   gateway.tools()
                       ->invoke("cy_tools_search", cymcp::Json::object())["tools"]
                       .empty()
               ? 0
               : 1;
}
