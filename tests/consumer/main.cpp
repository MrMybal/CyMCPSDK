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
    if (providers.describe().size() != 15 || !api->capabilities().streaming)
        return 2;
#endif
    cymcp::Registry operations(cymcp::Policy::allow_all());
    return operations.describe().empty() ? 0 : 1;
}
