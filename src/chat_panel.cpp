#include <algorithm>
#include <cstring>
#include <cymcp/chat_ui.hpp>
#include <cyui/Core.h>
#include <cyui/modules/DocumentViews.h>

namespace cymcp {
namespace {
const char* phase_label(const std::string& phase) {
    if (phase == "ready")
        return "Ready";
    if (phase == "connecting")
        return "Connecting";
    if (phase == "generating")
        return "Generating";
    if (phase == "loading_models")
        return "Loading models";
    if (phase == "reconnecting")
        return "Reconnecting";
    if (phase == "saving")
        return "Saving";
    if (phase == "disconnecting")
        return "Disconnecting";
    if (phase == "interrupted")
        return "Interrupted";
    if (phase == "failed")
        return "Failed";
    return "Disconnected";
}
const char* error_label(const std::string& code) {
    if (code == "AuthenticationRequired")
        return "The provider requires authentication in its native environment.";
    if (code == "ExecutableMissing")
        return "CLI not found. Choose its native executable in Connection settings.";
    if (code == "Unsupported")
        return "This provider does not support this operation. You can enter a model ID manually.";
    if (code == "Cancelled")
        return "Operation interrupted. Reconnect explicitly if the provider closed its session.";
    if (code == "ContextTooLarge" || code == "Backpressure")
        return "The conversation reached a memory limit. Save it and start a new conversation.";
    if (code == "UnsupportedContent")
        return "This provider or model does not support this attachment type.";
    if (code == "SessionInvalid")
        return "The exact saved session could not be resumed. Its history has been preserved.";
    if (code == "InvalidState")
        return "Connect a session before sending a message.";
    if (code == "InvalidContent")
        return "Enter a message or select an attachment before sending.";
    return "The operation failed. Check the provider, model and connection settings.";
}
void register_chat_translations() {
    static std::once_flag once;
    std::call_once(once, [] {
        cyui::registerTranslations(
            "fr",
            {
                {"Ready", "Prêt"},
                {"Connecting", "Connexion"},
                {"Generating", "Génération"},
                {"Loading models", "Chargement des modèles"},
                {"Reconnecting", "Reconnexion"},
                {"Saving", "Enregistrement"},
                {"Disconnecting", "Déconnexion"},
                {"Interrupted", "Interrompu"},
                {"Failed", "Échec"},
                {"Disconnected", "Déconnecté"},
                {"You", "Vous"},
                {"Assistant", "Assistant"},
                {"Chat", "Tchat"},
                {"Reconnect", "Reconnecter"},
                {"Disconnect", "Déconnecter"},
                {"Save conversation", "Enregistrer la conversation"},
                {"Follow responses", "Suivre les réponses"},
                {"Attach files", "Joindre des fichiers"},
                {"Remove attachment", "Retirer la pièce jointe"},
                {"Send", "Envoyer"},
                {"Stop", "Arrêter"},
                {"Activity", "Activité"},
                {"Thinking", "Réflexion"},
                {"First token", "Premier token"},
                {"Total", "Total"},
                {"No messages yet", "Aucun message pour le moment"},
                {"Connect a provider and send your first message.",
                 "Connectez un fournisseur puis envoyez votre premier message."},
                {"Type a message. Ctrl+Enter sends; Enter inserts a new line.",
                 "Écrivez un message. Ctrl+Entrée envoie ; Entrée ajoute une ligne."},
                {"Ctrl+Enter to send", "Ctrl+Entrée pour envoyer"},
                {"Link", "Lien"},
                {"Copy link", "Copier le lien"},
                {"Close", "Fermer"},
                {"The provider requires authentication in its native environment.",
                 "Le fournisseur demande une authentification dans son environnement natif."},
                {"CLI not found. Choose its native executable in Connection settings.",
                 "CLI introuvable. Choisissez son exécutable natif dans les paramètres de "
                 "connexion."},
                {"This provider does not support this operation. You can enter a model ID "
                 "manually.",
                 "Ce fournisseur ne propose pas cette opération. Vous pouvez saisir un identifiant "
                 "de modèle."},
                {"Operation interrupted. Reconnect explicitly if the provider closed its session.",
                 "Opération interrompue. Reconnectez explicitement si le fournisseur a fermé sa "
                 "session."},
                {"The conversation reached a memory limit. Save it and start a new conversation.",
                 "La conversation a atteint une limite mémoire. Enregistrez-la puis ouvrez une "
                 "nouvelle conversation."},
                {"This provider or model does not support this attachment type.",
                 "Ce fournisseur ou modèle ne prend pas en charge ce type de pièce jointe."},
                {"The exact saved session could not be resumed. Its history has been preserved.",
                 "La session enregistrée n’a pas pu être reprise. Son historique est conservé."},
                {"Connect a session before sending a message.",
                 "Connectez une session avant d’envoyer un message."},
                {"Enter a message or select an attachment before sending.",
                 "Écrivez un message ou choisissez une pièce jointe."},
                {"The operation failed. Check the provider, model and connection settings.",
                 "L’opération a échoué. Vérifiez le fournisseur, le modèle et les paramètres de "
                 "connexion."},
                {"Conversation could not be saved", "La conversation n’a pas pu être enregistrée"},
            });
    });
}
} // namespace
struct ChatPanel::Impl {
    ChatController& controller;
    ChatPanelOptions options;
    cyui::TranscriptState transcript;
    std::vector<cyui::TranscriptMessage> rows;
    cyui::Document thinking;
    cyui::DocumentState thinking_view;
    std::string draft; // grows with the text: pasted logs and files are not truncated
    std::vector<std::filesystem::path> attachments;
    std::uint64_t revision = ~std::uint64_t(0);
    std::string language, local_error, link;
    bool focus_draft = false;
    explicit Impl(ChatController& c, ChatPanelOptions o) : controller(c), options(std::move(o)) {
        register_chat_translations();
    }
    template <class F> void action(F&& fn) {
        try {
            fn();
            local_error.clear();
        } catch (const Error& error) {
            local_error = error.code;
        } catch (...) {
            local_error = "HostError";
        }
    }
};
ChatPanel::ChatPanel(ChatController& controller, ChatPanelOptions options)
    : impl_(std::make_unique<Impl>(controller, std::move(options))) {}
ChatPanel::~ChatPanel() = default;
void ChatPanel::set_draft(std::string text) {
    impl_->draft = std::move(text);
}
namespace {
int resize_draft(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}
} // namespace
void ChatPanel::draw(const char* id, float width, float height) {
    auto& s = *impl_;
    s.controller.poll();
    const auto& state = s.controller.state();
    cyui::ScopedID scope(id);
    ImGui::BeginChild("##chat-panel", {width, height});
    cyui::heading(cyui::tr("Chat").c_str(), 2);
    ImGui::SameLine();
    const auto tone = !state.error_code.empty() ? cyui::Tone::warning
                      : state.connected         ? cyui::Tone::success
                                                : cyui::Tone::neutral;
    cyui::badge(cyui::tr(phase_label(state.phase)).c_str(), tone);
    if (state.busy) {
        ImGui::SameLine();
        cyui::spinner("##busy");
    }
    cyui::textDim("%s%s%s", state.provider_id.c_str(), state.model.empty() ? "" : " / ",
                  state.model.c_str());
    {
        cyui::ScopedDisabled disabled(state.busy);
        if (cyui::ghostButton(cyui::trLabel("Reconnect").c_str()))
            s.action([&] { s.controller.reconnect(); });
        ImGui::SameLine();
        if (cyui::ghostButton(cyui::trLabel("Disconnect").c_str()))
            s.action([&] { s.controller.disconnect(); });
        ImGui::SameLine();
        if (cyui::ghostButton(cyui::trLabel("Save conversation").c_str()))
            s.action([&] { s.controller.save(); });
    }
    ImGui::SameLine();
    ImGui::Checkbox(cyui::trLabel("Follow responses").c_str(), &s.transcript.follow);
    const auto error = s.local_error.empty() ? state.error_code : s.local_error;
    if (!error.empty()) {
        cyui::callout(error == "Cancelled" ? cyui::Tone::neutral : cyui::Tone::warning,
                      cyui::tr(error_label(error)).c_str());
        cyui::textFaint("%s", error.c_str());
    }
    if (!state.storage_error.empty()) {
        cyui::callout(cyui::Tone::warning, cyui::tr("Conversation could not be saved").c_str());
        cyui::textFaint("%s", state.storage_error.c_str());
    }
    if (state.revision != s.revision || s.language != cyui::locale()) {
        s.rows.clear();
        for (const auto& message : state.messages)
            s.rows.push_back({message.id, cyui::tr(message.role == "user" ? "You" : "Assistant"),
                              message.markdown,
                              message.interrupted ? cyui::tr("Interrupted") : ""});
        s.revision = state.revision;
        s.language = cyui::locale();
    }
    bool details = false;
    if (!state.thinking.empty() || !state.activity.empty()) {
        details = ImGui::CollapsingHeader(cyui::trLabel("Activity").c_str());
        if (details) {
            ImGui::BeginChild("##activity", cyui::dp(0, 140), ImGuiChildFlags_Borders);
            if (!state.thinking.empty()) {
                s.thinking.setMarkdown(state.thinking);
                cyui::documentView("##thinking", s.thinking_view, s.thinking, cyui::dp(0, 95));
            }
            for (const auto& activity : state.activity)
                cyui::textDim("%s: %s", activity.type.c_str(), activity.name.c_str());
            ImGui::EndChild();
        }
    }
    const float footer =
        cyui::dp(158) + (!s.attachments.empty() ? ImGui::GetFrameHeightWithSpacing() : 0);
    const float transcript_height =
        std::max(cyui::dp(50), ImGui::GetContentRegionAvail().y - footer);
    if (s.rows.empty()) {
        ImGui::BeginChild("##empty", {0, transcript_height});
        cyui::emptyState(cyui::tr("No messages yet").c_str(),
                         cyui::tr("Connect a provider and send your first message.").c_str(),
                         cyui::icon::mail);
        ImGui::EndChild();
    } else {
        cyui::DocumentOptions options;
        options.services = s.options.document_services;
        options.tasks = false;
        options.linkPreviews = false;
        auto result =
            cyui::transcript("##messages", s.transcript, s.rows, {0, transcript_height}, options);
        if (!result.link.empty()) {
            if (s.options.open_link)
                s.action([&] { s.options.open_link(result.link); });
            else {
                s.link = result.link;
                ImGui::OpenPopup("##chat-link");
            }
        }
    }
    if (ImGui::BeginPopup("##chat-link")) {
        cyui::heading(cyui::tr("Link").c_str(), 3);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cyui::dp(420));
        ImGui::TextUnformatted(s.link.c_str());
        ImGui::PopTextWrapPos();
        if (cyui::button(cyui::trLabel("Copy link").c_str()))
            ImGui::SetClipboardText(s.link.c_str());
        ImGui::EndPopup();
    }
    if (!s.attachments.empty()) {
        ImGui::BeginChild("##attachments", {0, ImGui::GetFrameHeightWithSpacing()}, 0,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (std::size_t n = 0; n < s.attachments.size();) {
            cyui::ScopedID attachment_id(static_cast<int>(n));
            auto label = path_utf8(s.attachments[n].filename());
            if (cyui::ghostButton((label + "  x").c_str()))
                s.attachments.erase(s.attachments.begin() + n);
            else
                ++n;
            cyui::tooltip(cyui::tr("Remove attachment").c_str());
            ImGui::SameLine();
        }
        ImGui::EndChild();
    }
    ImGui::SetNextItemWidth(-1);
    if (s.focus_draft) {
        ImGui::SetKeyboardFocusHere();
        s.focus_draft = false;
    }
    const bool submit =
        ImGui::InputTextMultiline("##draft", s.draft.data(), s.draft.capacity() + 1,
                                  cyui::dp(-1, 90),
                                  ImGuiInputTextFlags_EnterReturnsTrue |
                                      ImGuiInputTextFlags_CallbackResize,
                                  resize_draft, &s.draft);
    cyui::tooltip(cyui::tr("Type a message. Ctrl+Enter sends; Enter inserts a new line.").c_str());
    bool send = submit;
    {
        cyui::ScopedDisabled disabled(state.busy || !state.connected);
        send = cyui::primaryButton(cyui::trLabel("Send").c_str(), cyui::dp(98, 0)) || send;
    }
    ImGui::SameLine();
    {
        cyui::ScopedDisabled disabled(!state.busy);
        if (cyui::button(cyui::trLabel("Stop").c_str()))
            s.controller.cancel();
    }
    ImGui::SameLine();
    if (s.options.pick_attachments) {
        cyui::ScopedDisabled disabled(state.busy);
        if (cyui::button(cyui::trLabel("Attach files").c_str()))
            s.action([&] {
                auto paths = s.options.pick_attachments();
                if (paths.size() + s.attachments.size() > 256)
                    throw Error("InvalidContent", "At most 256 attachments");
                s.attachments.insert(s.attachments.end(), paths.begin(), paths.end());
            });
        ImGui::SameLine();
    }
    cyui::textFaint("%s", cyui::tr("Ctrl+Enter to send").c_str());
    if (!state.metrics.empty()) {
        auto first = state.metrics.value("first_token_ms", Json());
        cyui::textFaint("%s: %.0f ms", cyui::tr("Total").c_str(),
                        state.metrics.value("total_ms", 0.0));
        if (first.is_number()) {
            ImGui::SameLine();
            cyui::textFaint("/ %s: %.1f ms", cyui::tr("First token").c_str(), first.get<double>());
        }
    }
    if (send && !state.busy && state.connected)
        s.action([&] {
            s.controller.send(s.draft, s.attachments);
            s.draft.clear();
            s.attachments.clear();
            s.focus_draft = true;
        });
    ImGui::EndChild();
}
} // namespace cymcp
