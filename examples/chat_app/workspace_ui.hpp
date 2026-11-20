#pragma once
#include "commands.hpp"
#include "journal.hpp"
#include <algorithm>
#include <cyui/Core.h>
#include <cyui/modules/DocumentViews.h>

namespace chat_example {
inline void workspace_translations() {
    cyui::registerTranslations(
        "fr",
        {{"Providers", "Fournisseurs"},
         {"Saved", "Enregistré"},
         {"Could not save attachment", "Impossible d’enregistrer la pièce jointe"},
         {"No preview for this file type. Save the attachment to use it.",
          "Aucun aperçu pour ce type de fichier. Enregistrez la pièce jointe pour l’utiliser."},
         {"Logs", "Journal"},
         {"Settings", "Paramètres"},
         {"Close", "Fermer"},
         {"Help", "Aide"},
         {"Raw JSONL", "JSONL brut"},
         {"Open full manual", "Ouvrir le manuel complet"},
         {"Delete this turn?", "Supprimer ce tour ?"},
         {"The message and its reply will be removed from the conversation history.",
          "Le message et sa réponse seront retirés de l’historique de la conversation."},
         {"Delete turn", "Supprimer le tour"},
         {"Provider settings", "Paramètres des fournisseurs"},
         {"Chat and AI providers", "Tchat et fournisseurs IA"},
         {"Install NVIDIA CUDA 12 runtime", "Installer le runtime NVIDIA CUDA 12"},
         {"Verify NVIDIA CUDA 12 runtime", "Vérifier le runtime NVIDIA CUDA 12"},
         {"Downloading and verifying CUDA runtime…",
          "Téléchargement et vérification du runtime CUDA…"},
         {"NVIDIA license", "Licence NVIDIA"},
         {"Optional private runtime for compatible local engines (2.9 MiB).",
          "Runtime privé facultatif pour les moteurs locaux compatibles (2,9 Mio)."},
         {"Choose a provider", "Choisir un fournisseur"},
         {"Configure providers", "Configurer les fournisseurs"},
         {"View logs", "Voir le journal"},
         {"Detected", "Détecté"},
         {"Connected", "Connecté"},
         {"Setup needed", "À configurer"},
         {"Not detected", "Non détecté"},
         {"Native CLI", "CLI natif"},
         {"Local service", "Service local"},
         {"API", "API"},
         {"Local demo", "Démo locale"},
         {"Connection failed", "Échec de connexion"},
         {"Authentication required", "Authentification requise"},
         {"Choose a model", "Choisir un modèle"},
         {"Select a provider to edit its settings. Double-click to connect.",
          "Sélectionnez un fournisseur pour ses paramètres. Double-cliquez pour connecter."},
         {"Installation and connection", "Installation et connexion"},
         {"Model selection", "Choix du modèle"},
         {"Detected settings", "Réglages détectés"},
         {"Permissions and app tools", "Droits et outils de l’app"},
         {"Conversation and history", "Conversation et historique"},
         {"Advanced launch settings", "Lancement avancé"},
         {"No catalogue loaded", "Aucun catalogue chargé"},
         {"Connect to load the catalogue. Native CLIs can keep their default model.",
          "Connectez pour charger le catalogue. Les CLI peuvent garder leur modèle par défaut."},
         {"Connection settings apply to the next new chat.",
          "Les paramètres s’appliquent au prochain nouveau tchat."},
         {"Diagnostics", "Diagnostics"},
         {"Errors only", "Erreurs uniquement"},
         {"Follow log", "Suivre le journal"},
         {"Copy log", "Copier le journal"},
         {"Open log file", "Ouvrir le fichier journal"},
         {"Clear view", "Vider la vue"},
         {"Time", "Heure"},
         {"Level", "Niveau"},
         {"Operation", "Opération"},
         {"Event", "Événement"},
         {"Details", "Détails"},
         {"Duration", "Durée"},
         {"No matching log entries", "Aucun événement correspondant"},
         {"Connection and request events appear here. Message contents and credentials are "
          "excluded.",
          "Les étapes de connexion et des requêtes apparaissent ici. Le contenu des messages et "
          "les identifiants sont exclus."},
         {"File logging unavailable; the journal remains available here.",
          "Écriture du fichier impossible ; le journal reste disponible ici."},
         {"The session log file reached its size limit; new events remain visible here.",
          "Le fichier a atteint sa limite de taille ; les nouveaux événements restent visibles "
          "ici."},
         {"Operation started", "Opération démarrée"},
         {"Operation completed", "Opération terminée"},
         {"Operation failed", "Échec de l’opération"},
         {"Response stream started", "Début de la réponse"},
         {"Native process started", "Processus natif démarré"},
         {"Provider handshake completed", "Dialogue initial terminé"},
         {"Application tool activity", "Activité d’un outil de l’app"},
         {"Check the provider account or API credentials in Providers, then connect again.",
          "Vérifiez le compte ou la clé API dans Fournisseurs, puis reconnectez."},
         {"Choose a model from the provider catalogue, then connect.",
          "Choisissez un modèle du catalogue, puis connectez."},
         {"The CLI executable is missing. Check its installation in Providers.",
          "Exécutable CLI introuvable. Vérifiez son installation dans Fournisseurs."},
         {"The native process could not run. Check the executable and working directory in "
          "Providers.",
          "Le processus natif n’a pas pu s’exécuter. Vérifiez l’exécutable et le dossier dans "
          "Fournisseurs."},
         {"The service did not respond. Check its address and whether it is running.",
          "Le service ne répond pas. Vérifiez son adresse et son démarrage."},
         {"A connection setting is invalid. Review the selected provider settings.",
          "Un paramètre de connexion est invalide. Vérifiez les réglages du fournisseur."},
         {"The operation was stopped. Reconnect if the provider closed its session.",
          "Opération arrêtée. Reconnectez si le fournisseur a fermé sa session."},
         {"The current permission policy rejected this action.",
          "Les droits actuels ont refusé cette action."},
         {"The provider rejected the request. Check the account, model and native provider status.",
          "Le fournisseur a refusé la requête. Vérifiez le compte, le modèle et l’état de l’outil "
          "natif."},
         {"This provider does not support the requested operation.",
          "Ce fournisseur ne prend pas en charge cette opération."},
         {"Check the operation and error code in Logs and review the provider settings.",
          "Consultez l’opération et le code d’erreur dans Journal et vérifiez les paramètres."},
         {"Model selection required", "Choix du modèle requis"},
         {"Working", "En cours"},
         {"info", "info"},
         {"warning", "alerte"},
         {"error", "erreur"},
         {"success", "succès"},
         {"application", "application"},
         {"discovery", "détection"},
         {"settings", "paramètres"},
         {"connection", "connexion"},
         {"connecting", "connexion"},
         {"generating", "génération"},
         {"loading_models", "catalogue"},
         {"reconnecting", "reconnexion"},
         {"saving", "enregistrement"},
         {"disconnecting", "déconnexion"},
         {"Provider settings selected", "Paramètres du fournisseur sélectionnés"},
         {"Provider discovery completed", "Détection des fournisseurs terminée"},
         {"Searching installed CLIs, API credentials and local services",
          "Recherche des CLI, identifiants API et services locaux"},
         {"Connecting with provider defaults", "Connexion avec les réglages natifs"},
         {"Connecting with the selected model", "Connexion avec le modèle sélectionné"},
         {"No available connection matches the current selection",
          "Aucune connexion disponible pour la sélection actuelle"},
         {"Native sign-in console opened", "Console de connexion native ouverte"},
         {"Local MCP state changed", "État du MCP local modifié"},
         {"Checking provider connection", "Vérification de la connexion du fournisseur"},
         {"Initializing native protocol", "Initialisation du protocole natif"},
         {"Checking native account", "Vérification du compte natif"},
         {"Loading model catalogue", "Chargement du catalogue de modèles"},
         {"Opening conversation session", "Ouverture de la conversation"}});
}
inline const char* provider_kind(const cymcp::Json& item) {
    if (item.value("id", std::string()) == "demo")
        return "Local demo";
    if (item.value("authentication", std::string()).rfind("native-account", 0) == 0)
        return "Native CLI";
    const auto id = item.value("id", std::string());
    return id == "ollama" || id == "lmstudio" || id == "llama.cpp" ? "Local service" : "API";
}
template <class App>
const cymcp::DiscoveredConnection* detected_provider(const App& app, const std::string& id) {
    for (const auto& row : app.detected)
        if (row.provider_id == id)
            return &row;
    return nullptr;
}
struct ProviderStatus {
    const char* text;
    cyui::Tone tone;
};
template <class App> ProviderStatus provider_status(const App& app, const std::string& id) {
    const cymcp::ChatViewState& state = app.chat.state();
    if (state.provider_id == id) {
        if (state.busy)
            return {"Working", cyui::Tone::info};
        if (state.error_code == "AuthenticationRequired")
            return {"Authentication required", cyui::Tone::warning};
        if (state.error_code == "UnknownModel" && !state.models.empty())
            return {"Choose a model", cyui::Tone::warning};
        if (!state.error_code.empty() && state.error_code != "Cancelled")
            return {"Connection failed", cyui::Tone::error};
        if (state.connected)
            return {"Connected", cyui::Tone::success};
    }
    if (id == "demo")
        return {"Local demo", cyui::Tone::neutral};
    auto row = detected_provider(app, id);
    return row && row->available() ? ProviderStatus{"Detected", cyui::Tone::neutral}
                                   : ProviderStatus{"Setup needed", cyui::Tone::warning};
}
template <class App> void draw_error(App& app) {
    const auto& state = app.chat.state();
    const auto code = !app.error.empty() ? app.error : state.error_code;
    if (code.empty())
        return;
    const auto detail = app.error.empty() && !state.error_message.empty()
                            ? state.error_message
                            : cyui::tr(diagnostic_help(code));
    const auto text = std::string("[") + code + "] " + detail;
    cyui::callout(code == "UnknownModel" || code == "Cancelled" ? cyui::Tone::info
                                                                : cyui::Tone::error,
                  text.c_str());
}
template <class App> void draw_models(App& app) {
    const cymcp::ChatViewState& state = app.chat.state();
    cymcp::Json models = state.provider_id == app.provider_id ? state.models : cymcp::Json::array();
    if (models.empty())
        if (auto row = detected_provider(app, app.provider_id))
            models = row->models;
    const cymcp::Json items = models.is_array()    ? models
                              : models.is_object() ? models.value("data", cymcp::Json::array())
                                                   : cymcp::Json::array();
    if (cyui::beginProperties("##model-fields", 160)) {
        cyui::property(cyui::tr("Model ID").c_str());
        if (!items.empty()) {
            if (ImGui::BeginCombo("##models", app.model[0]
                                                  ? app.model.data()
                                                  : cyui::tr("Provider default").c_str())) {
                if (ImGui::Selectable(cyui::tr("Provider default").c_str(), !app.model[0]))
                    app.set_model("");
                for (const cymcp::Json& item : items) {
                    const auto id = item.is_string()
                                        ? item.get<std::string>()
                                        : item.value("model", item.value("id", std::string()));
                    if (!id.empty() && ImGui::Selectable(id.c_str(), id == app.model.data()))
                        app.set_model(id);
                }
                ImGui::EndCombo();
            }
        } else
            ImGui::InputTextWithHint("##model-manual", cyui::tr("Provider default").c_str(),
                                     app.model.data(), app.model.size());
        cyui::endProperties();
    }
    if (items.empty())
        ImGui::TextWrapped(
            "%s",
            cyui::tr("Connect to load the catalogue. Native CLIs can keep their default model.")
                .c_str());
    cyui::ScopedDisabled disabled(state.busy || !state.connected ||
                                  state.provider_id != app.provider_id);
    if (cyui::button(cyui::trLabel("Refresh model catalogue").c_str()))
        app.action([&] { app.chat.refresh_models(); });
}
template <class App> void draw_app_tools(App& app) {
    if (!cyui::beginSection(cyui::trLabel("Permissions and app tools").c_str(), false))
        return;
    {
        cyui::ScopedDisabled disabled(app.chat.state().busy);
        cyui::toggleSwitch(cyui::trLabel("Allow all actions").c_str(), &app.allow_all);
        cyui::toggleSwitch(cyui::trLabel("AI may use app commands").c_str(), &app.app_tools);
    }
    cyui::textMono("%s: %d", cyui::tr("Counter").c_str(),
                   app.commands.snapshot().at("value").template get<int>());
    bool exposed = !app.commands.endpoint().empty();
    const auto access = app.commands.mcp_access();
    const auto current = std::find_if(
        std::begin(mcp_access_options), std::end(mcp_access_options), [access](const auto& option) {
            return cymcp::local_access_settings(option.profile) == access;
        });
    ImGui::TextUnformatted(cyui::tr("MCP access profile").c_str());
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::BeginCombo("##mcp-access", cyui::tr(current->label).c_str())) {
        for (const auto& option : mcp_access_options) {
            auto selected = cymcp::local_access_settings(option.profile);
            cyui::ScopedDisabled disabled(exposed &&
                                          selected.require_token != access.require_token);
            if (ImGui::Selectable(cyui::trLabel(option.label).c_str(), selected == access))
                app.action(
                    [&] { app.commands.configure_mcp_access(selected, app.commands.mcp_port()); });
        }
        if (exposed)
            ImGui::TextDisabled(
                "%s", cyui::tr("Stop local MCP to change the token requirement.").c_str());
        ImGui::EndCombo();
    }
    if (!app.commands.mcp_access().require_token)
        ImGui::TextWrapped(
            "%s", cyui::tr("Any program on this computer can call the app's permitted operations.")
                      .c_str());
    if (app.commands.mcp_access().control_operations)
        ImGui::TextWrapped("%s", cyui::tr("Counter changes are blocked by this profile.").c_str());
    if (cyui::toggleSwitch(cyui::trLabel("Expose local MCP").c_str(), &exposed))
        app.action([&] {
            if (exposed)
                app.commands.start();
            else
                app.commands.stop();
            app.journal.add("info", "", "app-tools", exposed ? "ServerStarted" : "ServerStopped",
                            "Local MCP state changed");
        });
    if (!app.commands.endpoint().empty()) {
        ImGui::TextUnformatted(app.commands.endpoint().c_str());
        if (cyui::button(cyui::trLabel("Copy MCP address").c_str()))
            ImGui::SetClipboardText(app.commands.endpoint().c_str());
        if (app.commands.mcp_access().require_token) {
            ImGui::SameLine();
            if (cyui::button(cyui::trLabel("Copy MCP token").c_str()))
                ImGui::SetClipboardText(app.commands.token().c_str());
        }
    }
    cyui::endSection();
}
template <class App> void draw_appearance(App&) {
    if (!cyui::beginSection(cyui::trLabel("Appearance").c_str(), false))
        return;
    if (cyui::beginProperties("##appearance", 160)) {
        cyui::property(cyui::tr("Language").c_str());
        if (ImGui::BeginCombo("##language", cyui::locale() == "fr" ? "Français" : "English")) {
            if (ImGui::Selectable("English", cyui::locale() == "en"))
                cyui::setLocale("en");
            if (ImGui::Selectable("Français", cyui::locale() == "fr"))
                cyui::setLocale("fr");
            ImGui::EndCombo();
        }
        cyui::property(cyui::tr("Theme").c_str());
        if (ImGui::BeginCombo("##theme", cyui::currentThemeKey().c_str())) {
            for (const auto& theme : cyui::builtInThemes())
                if (ImGui::Selectable(theme.name.c_str(), cyui::currentThemeKey() == theme.key))
                    cyui::setTheme(theme.key.c_str());
            ImGui::EndCombo();
        }
        cyui::endProperties();
    }
    cyui::endSection();
}
template <class App> void draw_provider_settings(App& app) {
    const cymcp::Json entry = app.provider_entry();
    const auto status = provider_status(app, app.provider_id);
    const auto name = entry.value("name", app.provider_id);
    cyui::heading(cyui::tr(name.c_str()).c_str(), 2);
    ImGui::SameLine();
    cyui::badge(cyui::tr(status.text).c_str(), status.tone);
    cyui::textDim("%s", cyui::tr(provider_kind(entry)).c_str());
    if (app.chat.state().provider_id == app.provider_id || !app.error.empty())
        draw_error(app);
    if (auto row = detected_provider(app, app.provider_id);
        row && !row->available() && app.provider_id != "demo")
        cyui::callout(cyui::Tone::warning, cyui::tr(app.discovery_status(*row)).c_str());
    {
        cyui::ScopedDisabled disabled(app.chat.state().busy || app.discovery_task.valid());
        if (cyui::primaryButton(cyui::trLabel("Connect / New chat").c_str(), cyui::dp(230, 0)))
            app.action([&] {
                app.connect();
                app.show_page(0);
            });
        ImGui::SameLine();
        if (entry.value("authentication", std::string()).rfind("native-account", 0) == 0) {
            if (cyui::button(cyui::trLabel("Sign in").c_str()))
                app.action([&] { app.sign_in(); });
            ImGui::SameLine();
        }
        if (cyui::button(cyui::trLabel("Detected settings").c_str()))
            app.action([&] { app.apply_defaults(app.provider_id); });
    }
    cyui::space(10);
    if (cyui::beginSection(cyui::trLabel("Installation and connection").c_str(), true)) {
#ifdef CYMCP_CHAT_HAS_LOCAL
        if (std::string(provider_kind(entry)) == "Local service") {
            ImGui::TextWrapped(
                "%s", cyui::tr("Optional private runtime for compatible local engines (2.9 MiB).")
                          .c_str());
            if (app.cuda_installation.valid()) {
                cyui::textDim("%s", cyui::tr("Downloading and verifying CUDA runtime…").c_str());
            } else {
                if (cyui::button(cyui::trLabel(app.cuda_directory.empty()
                                                   ? "Install NVIDIA CUDA 12 runtime"
                                                   : "Verify NVIDIA CUDA 12 runtime")
                                     .c_str()))
                    app.action([&] { app.install_cuda(); });
                ImGui::SameLine();
                if (cyui::button(cyui::trLabel("NVIDIA license").c_str()))
                    ShellExecuteW(app.window, L"open", L"https://docs.nvidia.com/cuda/eula/",
                                  nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (!app.cuda_directory.empty())
                ImGui::TextWrapped("%s", cymcp::path_utf8(app.cuda_directory).c_str());
        }
#endif
        if (cyui::beginProperties("##connection-fields", 160)) {
            if (std::string(provider_kind(entry)) == "Native CLI") {
                cyui::property(cyui::tr("Executable").c_str());
                ImGui::InputText("##exe", app.executable.data(), app.executable.size());
                cyui::property(cyui::tr("Working directory").c_str());
                ImGui::InputText("##cwd", app.cwd.data(), app.cwd.size());
            } else if (app.provider_id != "demo") {
                cyui::property(cyui::tr("Endpoint").c_str());
                ImGui::InputText("##endpoint", app.endpoint.data(), app.endpoint.size());
                cyui::property(cyui::tr("API key environment").c_str());
                ImGui::InputText("##key-env", app.key_env.data(), app.key_env.size());
                cyui::property(cyui::tr("API key (memory only)").c_str());
                ImGui::InputText("##key", app.key.data(), app.key.size(),
                                 ImGuiInputTextFlags_Password);
            }
            cyui::endProperties();
        }
        if (std::string(provider_kind(entry)) == "Native CLI" &&
            cyui::button(cyui::trLabel("Browse").c_str()))
            app.action([&] { app.browse_executable(); });
        cyui::endSection();
    }
    if (cyui::beginSection(cyui::trLabel("Model selection").c_str(), true)) {
        draw_models(app);
        cyui::endSection();
    }
    if (cyui::beginSection(cyui::trLabel("Advanced launch settings").c_str(), false)) {
        if (cyui::beginProperties("##advanced-fields", 160)) {
            cyui::property(cyui::tr("Arguments (JSON array)").c_str());
            ImGui::InputText("##arguments", app.arguments.data(), app.arguments.size());
            cyui::property(cyui::tr("Resume native ID").c_str());
            ImGui::InputText("##resume", app.resume.data(), app.resume.size());
            cyui::endProperties();
        }
        ImGui::TextUnformatted(cyui::tr("Advanced settings (JSON)").c_str());
        ImGui::InputTextMultiline("##settings", app.settings.data(), app.settings.size(),
                                  cyui::dp(-1, 110));
        cyui::endSection();
    }
    draw_app_tools(app);
    if (cyui::beginSection(cyui::trLabel("Conversation and history").c_str(), false)) {
        ImGui::TextUnformatted(cyui::tr("System prompt").c_str());
        ImGui::InputTextMultiline("##system", app.system.data(), app.system.size(),
                                  cyui::dp(-1, 80));
        cyui::toggleSwitch(cyui::trLabel("Save history").c_str(), &app.save_history);
        if (app.save_history && cyui::beginProperties("##history", 160)) {
            cyui::property(cyui::tr("History folder").c_str());
            ImGui::InputText("##folder", app.history_folder.data(), app.history_folder.size());
            cyui::property(cyui::tr("History ID").c_str());
            ImGui::InputText("##id", app.history_id.data(), app.history_id.size());
            cyui::endProperties();
        }
        {
            cyui::ScopedDisabled disabled(app.chat.state().busy || !app.save_history);
            if (cyui::button(cyui::trLabel("Restore saved chat").c_str()))
                app.action([&] {
                    app.connect(true);
                    app.show_page(0);
                });
        }
        cyui::endSection();
    }
    draw_appearance(app);
    cyui::space(10);
    ImGui::TextWrapped("%s", cyui::tr("Connection settings apply to the next new chat.").c_str());
}
template <class App> void draw_providers(App& app) {
    cyui::heading(cyui::tr("Provider settings").c_str(), 2);
    {
        cyui::ScopedDisabled disabled(app.discovery_task.valid() || app.chat.state().busy);
        if (cyui::button(cyui::trLabel("Detect again").c_str()))
            app.action([&] { app.detect_connections(!app.chat.state().connected); });
        ImGui::SameLine();
        if (cyui::toggleSwitch(cyui::trLabel("Automatic connection").c_str(), &app.automatic) &&
            app.automatic)
            app.action([&] { app.detect_connections(!app.chat.state().connected); });
    }
    if (app.discovery_task.valid()) {
        ImGui::SameLine();
        cyui::textDim("%s", cyui::tr("Detecting installed AI connections...").c_str());
    }
    cyui::space(8);
    if (!ImGui::BeginTable("##provider-layout", 2,
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        return;
    ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, cyui::dp(340));
    ImGui::TableSetupColumn("settings", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::BeginChild("##provider-list");
    cyui::filterBox("##provider-filter", app.provider_filter);
    cyui::space(6);
    std::vector<cymcp::Json> items;
    for (const cymcp::Json& item : app.catalogue)
        items.push_back(item);
    auto rank = [&](const cymcp::Json& item) {
        const auto id = item.at("id").get<std::string>();
        auto row = detected_provider(app, id);
        return row && row->available() ? 0 : id == "demo" ? 1 : 2;
    };
    std::stable_sort(items.begin(), items.end(),
                     [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    for (const cymcp::Json& item : items) {
        const auto id = item.at("id").get<std::string>(), name = item.at("name").get<std::string>();
        if (!app.provider_filter.matches(name.c_str(), id.c_str()))
            continue;
        ImGui::PushID(id.c_str());
        const auto origin = ImGui::GetCursorScreenPos();
        const auto width = ImGui::GetContentRegionAvail().x - cyui::dp(8);
        const auto height = cyui::dp(68);
        if (ImGui::Selectable("##provider-card", app.provider_id == id,
                              ImGuiSelectableFlags_AllowDoubleClick, {width, height})) {
            app.action([&] { app.select_provider(id); });
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !app.chat.state().busy)
                app.action([&] {
                    app.connect();
                    app.show_page(0);
                });
        }
        const auto status = provider_status(app, id);
        auto* draw = ImGui::GetWindowDrawList();
        const auto& palette = cyui::palette();
        draw->AddRect({origin.x, origin.y}, {origin.x + width, origin.y + height},
                      app.provider_id == id ? palette.accent : palette.line, cyui::dp(5));
        draw->PushClipRect(origin, {origin.x + width, origin.y + height}, true);
        draw->AddText({origin.x + cyui::dp(14), origin.y + cyui::dp(11)}, palette.text,
                      cyui::tr(name.c_str()).c_str());
        const auto subtitle = std::string(provider_kind(item)) == status.text
                                  ? cyui::tr(status.text)
                                  : cyui::tr(provider_kind(item)) + "  ·  " + cyui::tr(status.text);
        draw->AddText({origin.x + cyui::dp(14), origin.y + cyui::dp(38)},
                      status.tone == cyui::Tone::error     ? palette.error
                      : status.tone == cyui::Tone::success ? palette.success
                                                           : palette.textDim,
                      subtitle.c_str());
        draw->PopClipRect();
        cyui::space(5);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TableNextColumn();
    ImGui::BeginChild("##provider-editor");
    ImGui::Indent(cyui::dp(16));
    draw_provider_settings(app);
    ImGui::Unindent(cyui::dp(16));
    ImGui::EndChild();
    ImGui::EndTable();
}
template <class App> void draw_logs(App& app) {
    const bool copy = cyui::button(cyui::trLabel("Copy log").c_str());
    ImGui::SameLine();
    if (cyui::button(cyui::trLabel("Open log file").c_str()))
        app.action([&] { app.open_log(); });
    ImGui::SameLine();
    if (cyui::button(cyui::trLabel("Clear view").c_str()))
        app.journal.clear();
    ImGui::SameLine();
    cyui::toggleSwitch(cyui::trLabel("Errors only").c_str(), &app.log_errors_only);
    ImGui::SameLine();
    if (cyui::toggleSwitch(cyui::trLabel("Follow log").c_str(), &app.log_follow))
        app.log_jump = app.log_follow;
    ImGui::SameLine();
    if (cyui::toggleSwitch(cyui::trLabel("Raw JSONL").c_str(), &app.log_raw))
        app.log_jump = app.log_follow;
    cyui::filterBox("##log-filter", app.log_filter);
    cyui::textDim("%s", cymcp::path_utf8(app.log_path.filename()).c_str());
    cyui::tooltip(cymcp::path_utf8(app.log_path).c_str());
    if (app.journal.file_failed())
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("File logging unavailable; the journal remains available here.").c_str());
    if (app.journal.file_full())
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("The session log file reached its size limit; new events remain visible here.")
                .c_str());
    std::vector<const cymcp::ChatDiagnostic*> rows;
    for (const auto& row : app.journal.rows()) {
        if (app.log_errors_only && row.level != "warning" && row.level != "error")
            continue;
        const auto search = row.provider_id + " " + row.operation + " " + row.code + " " +
                            row.provider_code + " " + row.message;
        if (app.log_filter.matches(search.c_str(), row.level.c_str()))
            rows.push_back(&row);
    }
    if (copy)
        ImGui::SetClipboardText(Journal::copy_text(rows, app.log_raw).c_str());
    if (rows.empty()) {
        cyui::emptyState(cyui::tr("No matching log entries").c_str(),
                         cyui::tr("Connection and request events appear here. Message contents and "
                                  "credentials are excluded.")
                             .c_str(),
                         cyui::icon::info);
        return;
    }
    auto follow_new_entries = [&] {
        const auto last = rows.back()->id;
        const bool at_bottom = ImGui::GetScrollMaxY() - ImGui::GetScrollY() <= cyui::dp(3);
        if (app.log_follow && (app.log_jump || (last != app.log_last_id && at_bottom)))
            ImGui::SetScrollY(ImGui::GetCursorPosY());
        app.log_jump = false;
        app.log_last_id = last;
    };
    if (app.log_raw) {
        if (ImGui::BeginChild("##raw-log", {}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    ImGui::TextUnformatted(log_json(*rows[i]).c_str());
            follow_new_entries();
        }
        ImGui::EndChild();
        return;
    }
    if (ImGui::BeginTable("##diagnostics", 7,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        const char* headings[]{"Time",  "Level",   "Provider", "Operation",
                               "Event", "Details", "Duration"};
        const float widths[]{105, 75, 130, 120, 170, 0, 80};
        for (int i = 0; i != 7; ++i)
            ImGui::TableSetupColumn(cyui::tr(headings[i]).c_str(),
                                    i == 5 ? ImGuiTableColumnFlags_WidthStretch
                                           : ImGuiTableColumnFlags_WidthFixed,
                                    cyui::dp(widths[i]));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()), ImGui::GetFrameHeightWithSpacing());
        while (clipper.Step())
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto& row = *rows[index];
                ImGui::TableNextRow(0, ImGui::GetFrameHeightWithSpacing());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(log_time(row.timestamp_ms).c_str());
                ImGui::TableNextColumn();
                cyui::badge(cyui::tr(row.level.c_str()).c_str(),
                            row.level == "error"     ? cyui::Tone::error
                            : row.level == "warning" ? cyui::Tone::warning
                            : row.level == "success" ? cyui::Tone::success
                                                     : cyui::Tone::neutral);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.provider_id.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cyui::tr(row.operation.c_str()).c_str());
                ImGui::TableNextColumn();
                const auto code = row.provider_code.empty()
                                      ? row.code
                                      : row.code + " (" + row.provider_code + ")";
                ImGui::TextUnformatted(code.c_str());
                cyui::tooltip(code.c_str());
                ImGui::TableNextColumn();
                const auto detail = row.level == "error" && row.operation != "settings"
                                        ? row.message
                                        : cyui::tr(row.message.c_str());
                ImGui::TextUnformatted(detail.c_str());
                cyui::tooltip(detail.c_str());
                ImGui::TableNextColumn();
                if (row.elapsed_ms > 0)
                    ImGui::Text("%.0f ms", row.elapsed_ms);
            }
        follow_new_entries();
        ImGui::EndTable();
    }
}
template <class App> void draw_help(App& app) {
    if (cyui::button(cyui::trLabel("Open full manual").c_str()))
        app.action([&] { app.open_help(); });
    const bool fr = cyui::locale() == "fr";
    app.help_document.setMarkdown(fr ? R"(## Envoyer un message

Choisissez une connexion dans **Fournisseurs**, puis revenez au tchat avec **Fermer**.
**Entrée** envoie votre message. **Maj+Entrée** insère une nouvelle ligne. Pendant une réponse, **Arrêter** interrompt la génération.

## Markdown et pièces jointes

Sélectionnez du texte dans le brouillon pour le formater. Collez une image avec **Ctrl+V** ou choisissez un fichier avec **+**.
Survolez un message pour le copier ou y répondre.

## Lire les erreurs

Le tour en échec contient le message du fournisseur. Dans **Journal**, utilisez **Erreurs uniquement** pour le retrouver ou **JSONL brut** pour lire les événements tels qu’ils sont enregistrés.)"
                                     : R"(## Send a message

Choose a connection in **Providers**, then select **Close** to return to the chat.
**Enter** sends your message. **Shift+Enter** starts a new line. Use **Stop** during a response to interrupt generation.

## Markdown and attachments

Select text in the draft to format it. Paste an image with **Ctrl+V** or choose a file with **+**.
Hover over a message to copy or reply to it.

## Read an error

The failed turn contains the provider's explanation. In **Logs**, use **Errors only** to find it or **Raw JSONL** to read events as recorded.)");
    cyui::documentView("##chat-help", app.help_view, app.help_document);
}
} // namespace chat_example
