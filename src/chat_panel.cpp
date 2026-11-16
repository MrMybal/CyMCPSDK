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
    if (phase == "choosing_model")
        return "Choose a model";
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
    if (code == "Busy")
        return "Wait for the response or stop it before sending. Your draft is kept.";
    if (code == "AttachmentUnavailable")
        return "Attachment preview is not available in this host.";
    if (code == "UnknownModel")
        return "Select a model in Providers, then connect.";
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
                {"Choose a model", "Choisir un modèle"},
                {"Select a model in Providers, then connect.",
                 "Sélectionnez un modèle dans Fournisseurs puis connectez."},
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
                {"Send your first message.", "Envoyez votre premier message."},
                {"Connect a provider and send your first message.",
                 "Connectez un fournisseur puis envoyez votre premier message."},
                {"Type a message. Ctrl+Enter sends; Enter inserts a new line.",
                 "Écrivez un message. Ctrl+Entrée envoie ; Entrée ajoute une ligne."},
                {"Ctrl+Enter to send", "Ctrl+Entrée pour envoyer"},
                {"Enter to send · Shift+Enter for a new line",
                 "Entrée pour envoyer · Maj+Entrée pour une nouvelle ligne"},
                {"Wait for the response or stop it before sending. Your draft is kept.",
                 "Attendez la réponse ou arrêtez-la avant d’envoyer. Votre brouillon est "
                 "conservé."},
                {"Cancel edit", "Annuler la modification"},
                {"Attachment preview is not available in this host.",
                 "Cet hôte ne fournit pas de prévisualisation de pièce jointe."},
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
    cyui::TranscriptMessage row;
    cyui::Document thinking;
    cyui::DocumentState thinking_view;
    cyui::MessageComposerState composer, editor;
    std::uint64_t editing = 0;
    std::vector<std::filesystem::path> attachments;
    std::string local_error, link;
    void attach(std::vector<std::filesystem::path> paths) {
        if (paths.size() + attachments.size() > 256)
            throw Error("InvalidContent", "At most 256 attachments");
        attachments.insert(attachments.end(), paths.begin(), paths.end());
    }
    const ChatMessage* message(std::uint64_t id) const {
        const auto& rows = controller.state().messages;
        auto found = std::lower_bound(rows.begin(), rows.end(), id,
                                      [](const auto& row, auto key) { return row.id < key; });
        return found != rows.end() && found->id == id ? &*found : nullptr;
    }
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
    impl_->composer.text = std::move(text);
    impl_->composer.focus = true;
}
const std::string& ChatPanel::draft() const noexcept {
    return impl_->composer.text;
}
void ChatPanel::add_attachments(std::vector<std::filesystem::path> paths) {
    impl_->attach(std::move(paths));
}
void ChatPanel::draw(const char* id, float width, float height) {
    auto& s = *impl_;
    s.controller.poll();
    const auto& state = s.controller.state();
    if (s.local_error == "Busy" && !state.busy)
        s.local_error.clear();
    if (s.editing && !s.message(s.editing))
        s.editing = 0;
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
    const auto measured = ImGui::CalcTextSize(
        s.composer.text.c_str(), nullptr, false,
        std::max(cyui::dp(40), ImGui::GetContentRegionAvail().x - cyui::dp(110)));
    const auto input_height =
        std::clamp(measured.y + ImGui::GetStyle().FramePadding.y * 2 + cyui::dp(8),
                   ImGui::GetFrameHeight() + cyui::dp(8), cyui::dp(180));
    const float footer =
        input_height + ImGui::GetFrameHeightWithSpacing() * (2 + (!s.attachments.empty() ? 1 : 0) +
                                                             (!state.metrics.empty() ? 1 : 0));
    const float transcript_height =
        std::max(cyui::dp(50), ImGui::GetContentRegionAvail().y - footer);
    if (state.messages.empty()) {
        ImGui::BeginChild("##empty", {0, transcript_height});
        cyui::emptyState(cyui::tr("No messages yet").c_str(),
                         cyui::tr(state.connected
                                      ? "Send your first message."
                                      : "Connect a provider and send your first message.")
                             .c_str(),
                         cyui::icon::mail);
        ImGui::EndChild();
    } else {
        cyui::TranscriptOptions options;
        options.document.services = s.options.document_services;
        options.document.tasks = false;
        options.document.linkPreviews = false;
        options.editing = s.editing;
        bool edited = false;
        options.editor = [&](const auto&, float editor_width) {
            cyui::MessageComposerOptions input;
            input.attachments = false;
            input.editing = true;
            auto result =
                cyui::messageComposer("##edit-message", s.editor, {editor_width, 0}, input);
            if (result.submitted && s.options.edit_message)
                edited = true;
            if (result.cancelled || cyui::button(cyui::trLabel("Cancel edit").c_str()))
                s.editing = 0;
        };
        cyui::TranscriptSource source;
        source.id = ImGui::GetID("##chat-history");
        source.count = state.messages.size();
        // Streaming edits do not reset the sparse height index or parsed cache.
        source.revision = state.messages.front().id;
        source.message = [&](std::size_t index) -> const cyui::TranscriptMessage* {
            if (index >= state.messages.size())
                return nullptr;
            const auto& message = state.messages[index];
            s.row = {};
            s.row.id = message.id;
            s.row.role = cyui::tr(message.role == "user" ? "You" : "Assistant");
            s.row.content = message.markdown;
            s.row.time = message.interrupted ? cyui::tr("Interrupted") : "";
            s.row.own = message.role == "user" && !state.busy && s.options.edit_message &&
                        s.options.delete_message;
            for (const auto& attachment : message.attachments)
                s.row.attachments.push_back({attachment.id, attachment.name, attachment.source,
                                             attachment.bytes,
                                             attachment.mime_type.rfind("image/", 0) == 0});
            return &s.row;
        };
        source.indexOf = [&](cyui::ItemId key) {
            const auto* message = s.message(key);
            return message ? static_cast<std::size_t>(message - state.messages.data())
                           : std::string::npos;
        };
        const auto result =
            cyui::transcript("##messages", s.transcript, source, {0, transcript_height}, options);
        if (edited && s.editing)
            s.action([&] {
                s.options.edit_message(s.editing, s.editor.text);
                s.editing = 0;
            });
        if (const auto* message = s.message(result.reply)) {
            auto keep = std::min<std::size_t>(message->markdown.size(), 4096);
            while (keep < message->markdown.size() && keep &&
                   (static_cast<unsigned char>(message->markdown[keep]) & 0xc0) == 0x80)
                --keep;
            auto quote = message->markdown.substr(0, keep);
            std::string quoted = "> ";
            for (char c : quote) {
                quoted += c;
                if (c == '\n')
                    quoted += "> ";
            }
            s.composer.text = quoted + "\n\n" + s.composer.text;
            s.composer.focus = true;
        }
        if (result.edit)
            if (const auto* message = s.message(result.edit)) {
                s.editing = message->id;
                s.editor.text = message->markdown;
                s.editor.focus = true;
            }
        if (result.erase && s.options.delete_message)
            s.action([&] { s.options.delete_message(result.erase); });
        if (result.attachmentMessage)
            if (const auto* message = s.message(result.attachmentMessage);
                message && result.attachmentIndex < message->attachments.size())
                s.action([&] {
                    if (!s.options.open_attachment)
                        throw Error("AttachmentUnavailable", "Host attachment callback required");
                    s.options.open_attachment(message->attachments[result.attachmentIndex],
                                              result.download);
                });
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
    cyui::MessageComposerOptions composer_options;
    composer_options.attachments = static_cast<bool>(s.options.pick_attachments);
    composer_options.canSubmitEmpty = !s.attachments.empty();
    composer_options.paste = [&](std::string& text) {
        if (!s.options.paste)
            return false;
        bool consumed = false;
        s.action([&] {
            std::vector<std::filesystem::path> paths;
            consumed = s.options.paste(text, paths);
            if (consumed)
                s.attach(std::move(paths));
        });
        return consumed;
    };
    const auto composed = cyui::messageComposer("##composer", s.composer, {}, composer_options);
    if (composed.attach && s.options.pick_attachments)
        s.action([&] { s.attach(s.options.pick_attachments()); });
    {
        cyui::ScopedDisabled disabled(!state.busy);
        if (cyui::button(cyui::trLabel("Stop").c_str()))
            s.controller.cancel();
    }
    ImGui::SameLine();
    cyui::textFaint("%s", cyui::tr("Enter to send · Shift+Enter for a new line").c_str());
    if (!state.metrics.empty()) {
        auto first = state.metrics.value("first_token_ms", Json());
        cyui::textFaint("%s: %.0f ms", cyui::tr("Total").c_str(),
                        state.metrics.value("total_ms", 0.0));
        if (first.is_number()) {
            ImGui::SameLine();
            cyui::textFaint("/ %s: %.1f ms", cyui::tr("First token").c_str(), first.get<double>());
        }
    }
    if (composed.submitted && !state.busy && state.connected)
        s.action([&] {
            s.controller.send(s.composer.text, s.attachments);
            s.composer.text.clear();
            s.attachments.clear();
            s.composer.focus = true;
        });
    else if (composed.submitted)
        s.local_error = state.busy ? "Busy" : "InvalidState";
    ImGui::EndChild();
}
} // namespace cymcp
