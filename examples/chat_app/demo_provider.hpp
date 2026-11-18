#pragma once
#include <cymcp/ai.hpp>

namespace chat_example {
// A deterministic local UI fixture, explicitly not a language model.
class DemoProvider final : public cymcp::Provider {
    std::atomic_bool closed_{true};
    bool french_;

  public:
    explicit DemoProvider(bool french = false) : french_(french) {}
    bool uses_host_history() const noexcept override {
        return true;
    }
    cymcp::ProviderCapabilities capabilities() const override {
        return {true, true, false, true, true, true, true, true, false};
    }
    void connect(const cymcp::CallContext& context) override {
        context.cancellation.check();
        closed_ = false;
    }
    cymcp::Json list_models(const cymcp::CallContext& context) override {
        context.cancellation.check();
        return cymcp::Json::array({{{"id", "demo-markdown"}, {"name", "Markdown UI fixture"}}});
    }
    cymcp::Session start_session(std::string model, std::string resume,
                                 const cymcp::CallContext& context, std::string) override {
        context.cancellation.check();
        return {resume.empty() ? "demo-" + cymcp::random_token(16) : resume,
                model.empty() ? "demo-markdown" : model};
    }
    cymcp::ChatResult send(cymcp::Session&, cymcp::Json& history,
                           const cymcp::CallContext& context) override {
        const std::string text =
            french_
                ? "## Le tchat natif est prêt\n\n"
                  "Cette réponse est une **démonstration locale**, sans appel à une IA. "
                  "Choisissez un fournisseur dans **Paramètres → Fournisseurs** pour utiliser un vrai "
                  "modèle.\n\n"
                  "### Ce que vous pouvez tester\n\n"
                  "- Le texte **en gras**, *en italique* et le `code en ligne`.\n"
                  "- Le défilement pendant le streaming, la sélection et la copie.\n"
                  "- Les pièces jointes, l’arrêt et la reprise d’une conversation.\n\n"
                  "```cpp\n#include <cymcp/chat_ui.hpp>\n\ncymcp::ChatController chat;\n"
                  "cymcp::ChatPanel panel(chat);\n// Dans la boucle de rendu "
                  ":\npanel.draw();\n```\n\n"
                  "| Composant | Fonction |\n| --- | --- |\n| CyUI | Interface native et Markdown "
                  "|\n"
                  "| CyMCPSDK | Connecteurs, sessions et streaming |\n\n"
                  "> La même interface peut être intégrée dans une autre application.\n\n"
                  "[Documentation du projet](https://github.com/MrMybal/CyMCPSDK)"
                : "## Native chat is ready\n\n"
                  "This response is a **local demonstration**, with no AI request. "
                  "Choose a provider in **Settings → Providers** to use a real model.\n\n"
                  "### What you can try\n\n"
                  "- **Bold**, *italic* and `inline code`.\n"
                  "- Scrolling while text streams, selection and copying.\n"
                  "- Attachments, stopping and resuming a conversation.\n\n"
                  "```cpp\n#include <cymcp/chat_ui.hpp>\n\ncymcp::ChatController chat;\n"
                  "cymcp::ChatPanel panel(chat);\n// In the render loop:\npanel.draw();\n```\n\n"
                  "| Component | Purpose |\n| --- | --- |\n| CyUI | Native UI and Markdown |\n"
                  "| CyMCPSDK | Providers, sessions and streaming |\n\n"
                  "> The same panel can be embedded in another application.\n\n"
                  "[Project documentation](https://github.com/MrMybal/CyMCPSDK)";
        context.emit("ThinkingDelta",
                     {{"text", french_ ? "Préparation de la démonstration Markdown native."
                                       : "Preparing the native Markdown demonstration."}});
        for (std::size_t n = 0; n < text.size();) {
            context.cancellation.check();
            if (closed_)
                throw cymcp::Error("Cancelled", "Demo closed");
            // Preserve UTF-8 codepoint boundaries across displayed chunks.
            auto end = std::min(n + 28, text.size());
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
                ++end;
            context.emit("TextDelta", {{"text", text.substr(n, end - n)}});
            n = end;
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }
        history.push_back({{"role", "assistant"}, {"content", text}});
        context.emit("TurnCompleted", {{"text", text}});
        return {text, "stop", {{"demo", true}}};
    }
    void disconnect() noexcept override {
        closed_ = true;
    }
};
inline std::shared_ptr<cymcp::ProviderRegistry> providers() {
    auto registry = std::make_shared<cymcp::ProviderRegistry>();
    registry->add({"demo",
                   "Demo / Markdown (no AI)",
                   "in-process-fixture",
                   "none",
                   {true, true, false, true, true, true, true, true, false}},
                  [](const cymcp::ProviderConfig& config) {
                      return std::make_shared<DemoProvider>(
                          config.settings.value("demo_locale", std::string("en")) == "fr");
                  });
    return registry;
}
} // namespace chat_example
