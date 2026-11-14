#include <cymcp/core.hpp>
#ifdef CONSUMER_HAS_AI
#include <cymcp/ai.hpp>
#endif
int main() {
#ifdef CONSUMER_HAS_AI
    auto provider = std::make_shared<cymcp::HttpChatProvider>(cymcp::HttpChatOptions{});
    cymcp::Conversation conversation(provider, "selected-model");
    cymcp::ProviderRegistry providers;
    auto api = providers.create("ollama");
    if (providers.describe().size() != 22 || !api->capabilities().streaming)
        return 2;
    auto content = cymcp::Json::array({cymcp::text_block("portable content")});
    cymcp::validate_content(content);
    if (conversation.checkpoint().at("version") != 1 ||
        conversation.status().at("state") != "disconnected")
        return 3;
#endif
    cymcp::Registry operations(cymcp::Policy::allow_all());
    return operations.describe().empty() ? 0 : 1;
}
