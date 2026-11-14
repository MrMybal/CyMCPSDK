# Connecteurs IA — 0.4.0

Le module `CyMCP::AI` fournit un contrat `Provider`, un registre de factories et 22 profils intégrés.
Les transports sont natifs C++ : aucune dépendance aux compagnons Python, aux SDK Python des fournisseurs,
à CyUI ou à un moteur GPU. Ajouter un fournisseur ne nécessite pas de modifier le service `Conversation`.

## Couverture intégrée

| ID du registre | Transport implémenté | Catalogue | Reprise |
|---|---|---|---|
| `codex` | App Server persistant, JSON-RPC stdio | Natif, paginé | Thread natif exact |
| `claude-code` | CLI `stream-json` persistant par défaut | `Unsupported` ; ID manuel | Session native exacte |
| `opencode` | ACP v1 persistant, OpenCode actuel | Options de session annoncées | `session/load` si annoncé |
| `opencode-cli` | CLI historique **1.x** `run --format json` | `opencode models` | `--session ID` |
| `antigravity` | CLI `agy`, stdin/stdout `stream-json` persistant par défaut | `agy models`, JSON ou tabulé | `--conversation ID` |
| `command-code` | CLI NDJSON `AgentEvent` + résultat final | JSON de `--list-models`, sinon `Unsupported` | `--resume ID` |
| `codebuddy` | CLI Tencent `stream-json`, processus par tour | `Unsupported` ; ID manuel | `--resume ID` |
| `devin` | CLI Cognition `acp`, JSON-RPC persistant | Commande JSON native puis options de session ACP | `session/load` si annoncé |
| `acp` | Agent Client Protocol **v1** sur stdio | Options de session annoncées ou commande JSON configurée | Selon capacités négociées |
| `gemini`, `qwen-code`, `kimi`, `goose` | Profils ACP v1 persistants | Options de session annoncées | Selon capacités négociées |
| `command-code-acp`, `codebuddy-acp` | Variantes ACP v1 persistantes des CLI | Options de session annoncées | Selon capacités négociées |
| `openai` | API **Responses**, HTTPS/SSE | `/v1/models` | Historique de l'hôte |
| `anthropic` | Claude API **Messages**, HTTPS/SSE | `/v1/models`, pagination | Historique de l'hôte |
| `openrouter` | API Chat Completions, HTTPS/SSE | `/models`, authentification `/key` | Historique de l'hôte |
| `ollama` | API native `/api/chat`, HTTP loopback/NDJSON | `/api/tags` | Historique de l'hôte |
| `openai-compatible` | Chat Completions, SSE | `/models` | Historique de l'hôte |
| `llama.cpp` | Profil Chat Completions d'un serveur existant | `/models` | Historique de l'hôte |
| `lmstudio` | Profil Chat Completions, port par défaut 1234 | `/models` | Historique de l'hôte |

Les trois derniers profils réutilisent le même adaptateur compatible. Leurs capacités effectives dépendent du
serveur et du modèle. Le profil `llama.cpp` ne lance pas le moteur : utiliser `LocalEngine`, puis fournir
`engine.base_url()` et `engine.token()`. Ollama est un service distinct, installé et lancé séparément.

Les entrées multimodales sont traduites selon chaque transport et ses capacités ; consulter le
[guide des conversations](Conversations.md) pour la matrice et le format des pièces jointes. Les appels d'outils fonctionnent dans
les adaptateurs API, via `Registry` et ses politiques ; Codex dispose aussi des outils dynamiques.
Claude Code et CodeBuddy utilisent une configuration MCP native. OpenCode CLI 1.x/Antigravity/Command Code utilisent leurs outils/configurations
natives ; leur envoyer `ProviderConfig.tools` retourne `Unsupported`, pour éviter une connexion d'outils fictive.
Les agents ACP reçoivent `settings.mcp_servers`, un tableau au format ACP. Ils ne reçoivent pas directement
`ProviderConfig.tools` : exposer le registre via un serveur MCP, puis fournir ses paramètres de connexion.
Les blocs multimodaux restent possibles avec `HttpChatProvider` si l'endpoint les accepte.

Le profil `opencode` utilise la commande ACP documentée par OpenCode actuel, dont 2.x. Le profil
`opencode-cli` conserve le parseur JSONL 1.x et refuse les autres versions avec `UnsupportedVersion`.
Les nouveaux profils partagent un transport de protocole testé ; leurs versions fournisseurs restent à valider réellement.
Command Code, CodeBuddy et Devin ont été portés depuis les intégrations de CyAIOrchestrator, au-delà
des fiches encore non implémentées de Connector Lab. Leur état de validation C++ est documenté séparément.

## Choisir un fournisseur depuis l'app

```cpp
#include <cymcp/ai.hpp>
using namespace cymcp;

ProviderRegistry providers; // catalogue intégré
Json catalogue = providers.describe();

ProviderConfig config;
config.api_key = key_from_host_vault; // mémoire, hors configuration sérialisable
config.tools = app_operations;       // shared_ptr<const Registry>
config.settings = {
    {"timeout_ms", 120000},
    {"max_output_tokens", 4096},
    {"max_tool_rounds", 8}
};
auto provider = providers.create("anthropic", config);
Conversation chat(provider, exact_model_id, "Assist the user of this application.");
chat.connect();
auto models = provider->list_models();
auto result = chat.send_async(app_executor, "Export this document", event_context);
```

L'application choisit le modèle à partir du catalogue réel ou d'un ID explicite. Le SDK n'impose aucun modèle
commercial par défaut. Un catalogue ne prouve pas le droit d'utiliser tous ses modèles.

`openai`, `anthropic` et `openrouter` demandent une clé en mémoire. `ollama` et les profils compatibles
acceptent une clé optionnelle. `base_url` remplace l'endpoint par défaut ; HTTPS est obligatoire hors loopback.
Les URL avec identifiants, fragments ou query dans l'URL de base sont refusées. Les redirections ne sont pas suivies.
Le SDK ne persiste ni clés ni profils. L'hôte charge les secrets depuis son coffre ou son environnement.
`settings.api_key` est refusé : utiliser le champ séparé `ProviderConfig.api_key`.

`connect()` des API natives effectue une requête de catalogue authentifiée, plus `/key` pour OpenRouter ;
aucune génération n'est envoyée. La connexion compatible générique valide uniquement la configuration :
appeler `list_models()` pour vérifier le service. OpenCode/Antigravity/Command Code/CodeBuddy vérifient l'exécutable avec `--version` ;
l'état du compte reste **non vérifié** avant une génération réussie. Codex et Claude vérifient leur compte natif.
ACP négocie le protocole dans `initialize` ; une session peut ensuite signaler `AuthenticationRequired`.
Le SDK n'ouvre pas de navigateur d'authentification automatiquement.

## CLI et permissions

```cpp
ProviderConfig config;
config.process.executable = absolute_native_cli_path;
config.process.cwd = project_directory;
config.settings = {
    {"connect_timeout_ms", 15000},
    {"turn_timeout_ms", 600000},
    {"allow_all_actions", false}
};
auto provider = providers.create("antigravity", config);
```

Avec un exécutable non configuré, la factory cherche le nom natif dans `PATH`. Command Code accepte aussi
l'alias `cmdc`, CodeBuddy l'alias `cbc`. Ne pas sélectionner le shell Windows `cmd` comme exécutable Command Code.
Fournir un chemin natif explicite pour le profil ACP générique et les installations privées/npm.
Sous Windows, aucun shim `.cmd` ou `.ps1` n'est exécuté par
un shell ; fournir le `.exe` natif ou `node.exe` avec son point d'entrée dans `process.arguments`.
Le préfixe d'arguments reste utilisable pour les wrappers natifs. Aucun prompt n'est interpolé dans une commande shell.

Valeurs par défaut et réglage permissif explicite :

| Adaptateur | Par défaut | `allow_all_actions=true` |
|---|---|---|
| Codex via registre | `read-only`, requêtes d'approbation refusées | `danger-full-access`, approbations natives autorisées |
| Claude Code | `dontAsk`, outils intégrés sélectionnés explicitement | `--dangerously-skip-permissions` |
| OpenCode CLI 1.x (`opencode-cli`) | `permission.*=deny` dans la configuration du processus | `permission.*=allow` |
| Antigravity | `--mode plan --sandbox` | `--dangerously-skip-permissions` |
| Command Code | `--plan` | `--yolo` |
| CodeBuddy | `dontAsk`, liste d'outils explicite | `bypassPermissions` et `--dangerously-skip-permissions` |
| Devin ACP | mode natif `normal`, confiance workspace respectée, demandes ACP refusées | mode natif `dangerous`, confiance workspace ignorée, demandes ACP autorisées |
| ACP générique | politique native de l'agent, demandes ACP refusées | demandes ACP autorisées ; le mode natif se configure séparément |

Les politiques d'outils de l'app restent indépendantes. Fournir `Policy::allow_all()` au `Registry` si l'hôte
veut autoriser tous ses handlers. Le mode plan d'Antigravity permet des lectures ; ce n'est pas une sandbox
créée par le SDK. Les restrictions natives, de compte et d'organisation peuvent toujours s'appliquer.

Pour Claude : `settings.mcp_servers` et `settings.allowed_tools`. Pour OpenCode CLI 1.x : `settings.native_settings`
est sa configuration JSON 1.x, dont `mcp` si nécessaire. Pour Antigravity : cet objet passe à `--settings`.
Les réglages de permissions générés par l'adaptateur prennent priorité sur les réglages contradictoires de ce même objet.
Les fichiers de configuration/plugins natifs du fournisseur restent sous la responsabilité de l'hôte.

Pour CodeBuddy, `settings.mcp_config_file` désigne un fichier MCP préparé par l'hôte, compatible avec les
règles d'admission du workspace du CLI. `settings.allowed_tools` sélectionne les outils intégrés ; une liste
vide les désactive dans le mode par défaut. `settings.native_settings` passe à `--settings`. Le SDK impose
`CODEBUDDY_CODE_DISABLE_BACKGROUND_TASKS=1` uniquement à son processus : ce transport attend un résultat
terminal par tour et ne gère pas les tâches natives qui survivent à ce tour. Les snapshots assistant répétés
ne sont pas ajoutés deux fois au texte progressif. Les restrictions de sécurité natives peuvent encore s'appliquer
au mode permissif, notamment pour les commandes dangereuses ; le SDK n'annonce pas un contournement des règles fournisseur.

Command Code accepte les réglages supplémentaires dans le préfixe `process.arguments`. Un résultat natif
`max_turns` devient `TurnLimitExceeded` : le SDK ne relance pas automatiquement la session. Les refus natifs
et les résultats partiels restent visibles en erreur. Un catalogue uniquement textuel donne `Unsupported`
plutôt qu'une liste de modèles inventée.

Les prompts Claude/Antigravity/Command Code/CodeBuddy passent sur stdin. OpenCode attend son message dans un argument individuel après `--`.
Les instructions système OpenCode/Antigravity/Command Code/CodeBuddy sont ajoutées comme contexte textuel au premier tour ;
elles n'ont pas le statut d'un rôle système natif. Les IDs repris sont exacts, sans « dernière session » implicite.

## Connecter un autre agent ACP sans nouveau parseur

```cpp
ProviderConfig config;
config.process.executable = absolute_native_agent_path;
config.process.cwd = project_directory;
config.settings = {
    {"acp_arguments", {"acp"}}, // adapter au CLI : acp, --acp, autre option documentée
    {"mcp_servers", Json::array()},
    {"allow_all_actions", false}
};
auto provider = providers.create("acp", config);
Conversation chat(provider, ""); // modèle natif courant si aucun ID explicite
chat.connect();
```

Le profil `devin` ajoute déjà le sous-programme `acp` et les options natives de permissions : le préfixe
`process.arguments` peut contenir un wrapper/script, sans répéter `acp`. Pour un autre agent, le profil
générique reçoit sa commande et ses arguments réels. Cela ne valide pas automatiquement tous les produits
qui annoncent ACP ; leur version doit encore être testée. ACP v2, encore distinct du transport v1, est refusé.

Le transport négocie les capacités, crée/charge la session exacte, ignore le replay dans le nouveau résultat,
sélectionne le modèle/mode via `configOptions` ou les champs natifs hérités, et attend la réponse corrélée
`session/prompt` avec `stopReason=end_turn`. Un refus et une limite native deviennent des erreurs explicites.
Le catalogue dépend des données réellement annoncées par l'agent ; avant création de session, il peut être
indisponible. `settings.models_arguments` configure facultativement une commande JSON de catalogue utilisant
le même exécutable/préfixe, sans le sous-programme ACP. Devin fournit déjà `models list --format json`.

`settings.mode` sélectionne un mode natif exact lorsqu'il est annoncé. Une décision `ProviderConfig.approvals`
contrôle les requêtes `session/request_permission` ; un accord ponctuel est préféré à un accord permanent.
Par défaut le SDK n'annonce ni système de fichiers client ni terminal client. Un hôte peut fournir
`settings.client_capabilities` et `request_handler` ; ces requêtes sont aussi soumises à sa politique
et à la vérification de session. Voir [Conversations.md](Conversations.md). Les outils exécutés par l'agent lui-même restent régis par sa politique native.
Le mode `normal` de Devin n'est pas un mode lecture seule. Les instructions système ACP sont fournies comme
contexte textuel au premier prompt, car ACP v1 ne définit pas un rôle système portable.

Le processus reste actif entre les tours. Une annulation transmet `session/cancel`, répond aux permissions
encore en attente avec l'issue annulée, puis ferme l'arbre de processus possédé. Après interruption, reconnecter
la conversation avec son ID natif exact si l'agent sait charger cette session ; aucune reprise implicite n'est lancée.

## Ajouter un fournisseur

Un endpoint compatible peut être ajouté comme profil sans écrire de nouveau parseur :

```cpp
ProviderRegistry providers;
providers.add(
    {"my-provider", "My provider", "chat-completions-sse", "api-key",
     {true, false, true, true, true}},
    [](const ProviderConfig& config) -> std::shared_ptr<Provider> {
        HttpChatOptions options;
        options.base_url = config.settings.at("base_url").get<std::string>();
        options.api_key = config.api_key;
        options.tools = config.tools;
        return std::make_shared<HttpChatProvider>(std::move(options));
    });
```

Pour un autre protocole CLI, API ou SDK natif, implémenter `Provider`, puis enregistrer sa factory avec
`add()`. La conversation, les événements et l'interface éventuelle restent identiques. Les IDs en double et
les factories nulles sont refusés. Les factories s'exécutent hors du verrou du registre. Le registre n'importe
pas de code exécutable à partir d'une simple fiche JSON ; le code d'extension est fourni explicitement par l'hôte.

`ProviderRegistry(false)` crée un registre vide pour n'enregistrer que les fournisseurs nécessaires.
Les classes typées restent utilisables directement, sans registre. Un lien statique peut alors éliminer les
connecteurs non référencés. Les bibliothèques tierces lourdes ne sont toujours pas embarquées.

## Flux, outils et erreurs

Les événements sont ceux du service de conversation : texte progressif, résultat final, outils et usage.
Les résultats finaux remplacent les deltas dans l'interface ; ne pas les concaténer une deuxième fois.
Les flux nécessitent une terminaison native : `response.completed`, `message_stop`, `done=true`,
fin de choix suivie de `[DONE]`, résultat CLI explicite, `step_finish.reason=stop` ou réponse ACP corrélée.
Un code de sortie non nul, un échec après du texte partiel ou un flux tronqué reste une erreur.
Les codes HTTP 401/403 et 429 deviennent `AuthenticationRequired` et `RateLimited`, sans recopier le corps privé.

Les API exécutent les appels de fonctions au travers de `Registry::invoke`. Les noms non compatibles sont
traduits de façon stable. Les IDs d'appels doivent être distincts ; schémas et droits sont appliqués avant le handler.
L'historique conserve les résultats d'outils déjà exécutés, même si une étape suivante échoue. Il n'y a aucune
relance automatique de requête de génération ou d'action.

Les sorties natives nécessaires aux tours suivants sont conservées dans `_cymcp_native`, notamment les
éléments de raisonnement chiffrés Responses et les blocs/signatures Anthropic. Ce sont des données privées de
conversation : leur stockage/chiffrement éventuel appartient à l'hôte. Les APIs n'annoncent pas de reprise
par ID natif ; `Conversation::checkpoint/restore` préserve leur historique applicatif, et `reconnect`
recrée la connexion sans relancer une génération. Un stockage privé facultatif est fourni par `ConversationStore`.

## Exemple exécutable

`cymcp_chat providers` imprime le catalogue machine-readable. `cymcp_chat models CONFIG.json` teste
la connexion et imprime les modèles. `cymcp_chat CONFIG.json` lit une ligne JSON `{"text":"Bonjour"}`
par tour et émet les événements JSONL. Terminer chaque message d'entrée par une nouvelle ligne.
Les exemples `examples/chat-ollama.json` et `examples/chat-openai.json` demandent de choisir un ID de modèle.
Le champ `api_key_env` contient le **nom** d'une variable, jamais la clé. Les exemples Devin et ACP générique
sont aussi fournis. Aucun moteur ni compte n'est installé par cet exemple.

## Sources et preuves

Audit des sources locales le 14 novembre 2026 : [CyToolsCore](https://github.com/MrMybal/CyToolsCore),
notamment `Docs/UnrealIntegration.md`, puis les transports de [CyUEMCPSDK](https://github.com/MrMybal/CyUEMCPSDK)
et les recettes de CyAIConnectorLab, ainsi que `cyai/extra_connectors.py` dans CyAIOrchestrator.
Le noyau CyToolsCore expose les outils ; les connecteurs de conversation
se trouvent dans ces intégrations associées. Les fiches seules n'ont pas été comptées comme implémentations.

Documentation officielle consultée : [OpenAI Responses streaming](https://developers.openai.com/api/docs/guides/streaming-responses),
[function calling](https://developers.openai.com/api/docs/guides/function-calling),
[Claude streaming](https://platform.claude.com/docs/en/build-with-claude/streaming),
[Claude models](https://platform.claude.com/docs/en/api/models/list),
[Ollama chat](https://docs.ollama.com/api/chat), [models](https://docs.ollama.com/api/tags),
[OpenRouter auth](https://openrouter.ai/docs/api_reference/authentication),
[OpenCode](https://opencode.ai/docs/cli/commands), [Antigravity headless](https://antigravity.google/docs/cli/headless/),
[LM Studio compatible API](https://lmstudio.ai/docs/developer/openai-compat).
Pour les ajouts : [Command Code headless](https://commandcode.ai/docs/headless),
[référence CLI](https://commandcode.ai/docs/reference/cli), [événements](https://commandcode.ai/docs/mods),
[CodeBuddy headless](https://www.codebuddy.ai/docs/cli/headless),
[Devin commandes et ACP](https://docs.devin.ai/cli/reference/commands),
[ACP initialisation](https://agentclientprotocol.com/protocol/v1/initialization),
[sessions](https://agentclientprotocol.com/protocol/v1/session-setup),
[prompts](https://agentclientprotocol.com/protocol/v1/prompt-turn),
[configuration](https://agentclientprotocol.com/protocol/v1/session-config-options).
Voir [Validation.md](Validation.md) pour distinguer fixtures C++, installation détectée et génération réelle.
