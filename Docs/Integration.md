# Intégration

## Une opération, plusieurs accès

```cpp
#include <cymcp/mcp.hpp>
using namespace cymcp;

Registry operations(Policy::allow_all());
Operation operation;
operation.name = "document.export";
operation.description = "Export the current document";
operation.input_schema = {
    {"type", "object"},
    {"properties", {{"format", {{"type", "string"}, {"enum", {"png", "jpeg"}}}}}},
    {"required", {"format"}},
    {"additionalProperties", false}
};
operation.output_schema = {{"type", "object"}};
operation.handler = [](const Json& arguments, const CallContext& context) {
    context.cancellation.check();
    // Appeler ici le vrai moteur de l'application et retourner ses résultats.
    return Json{{"format", arguments.at("format")}};
};
operations.add(std::move(operation));

auto result = operations.invoke("document.export", {{"format", "png"}});
McpServer mcp(operations, "My application");
```

Le handler s'exécute sur le thread de l'appelant : les serveurs utilisent leurs workers.
L'application doit transférer le travail sur son thread métier/UI lorsque son moteur l'impose,
et conserver la validité des objets capturés. Les callbacks sont exécutés hors du verrou du registre.
Les handlers longs vérifient régulièrement `context.cancellation`; aucun thread C++ métier n'est tué de force.

`Policy{}` et `Policy::deny_all()` refusent les appels. `Policy::allow_all()` autorise les opérations exposées
sans confirmation. Une politique hôte décide à partir de l'identité authentifiée, du nom et des paramètres :

```cpp
Policy policy([](const CallContext& context, const std::string& operation, const Json& arguments) {
    return context.principal == "trusted-plugin" ? Decision::allow : Decision::deny;
});
```

Le mode permissif conserve la validation et la protection des communications. Les droits du système et du
fournisseur restent applicables. Le serveur HTTP fourni représente une connexion locale authentifiée avec un seul
secret, pas un service multi-utilisateur avec délégation d'identité. Pour plusieurs identités, créer des endpoints
isolés ou adapter l'authentification hôte ; ne jamais accepter un `principal` arbitraire depuis les arguments JSON.

## Schémas

Le profil de schéma est volontairement explicite : `type`, `properties`, `required`, `additionalProperties`,
`items`, `enum`, `const`, bornes numériques et de taille, `uniqueItems`, `allOf`, `anyOf`, `oneOf`, `not`,
`if/then/else`, et métadonnées documentaires. Les mots-clés de validation inconnus sont rejetés à l'enregistrement.
Les références `$ref`, expressions `pattern` et `format` ne sont pas implémentées. Ce profil ne constitue pas
une implémentation complète de JSON Schema 2020-12. `default` est une annotation, pas une transformation des valeurs.

Les messages entrants sont bornés en taille et en profondeur ; les clés JSON dupliquées sont rejetées.
La validation des sorties d'un handler est aussi appliquée.
La racine des paramètres est de type `object`. Une sortie structurée déclarée utilise également `object` ;
envelopper une liste ou une valeur dans un champ nommé pour préserver le contrat MCP.

## MCP et autre application

`McpServer::serve_stdio()` sert les outils et permet des appels concurrents bornés.
Le serveur répond à `server/discover` et aux métadonnées par requête de la révision `2026-07-28`.
Il accepte aussi l'initialisation `2025-11-25` et `2025-06-18` pour les clients existants.
Sur HTTP, cette initialisation renvoie `Mcp-Session-Id`. Le client le renvoie pour les
appels suivants et pour `notifications/cancelled`. Des clients distincts peuvent donc utiliser le
même identifiant JSON-RPC. `DELETE /mcp` termine la session désignée. Après un redémarrage
ou l’expiration d’une session inactive, un HTTP 404 indique qu’il faut réinitialiser la session.
Le serveur n'annonce ni ressources, ni prompts, ni souscriptions : uniquement les outils implémentés.

Le client utilise d'abord la découverte moderne, puis l'initialisation historique si le serveur la refuse.
Il suit la pagination des outils et détecte les curseurs cycliques. Le transport HTTP accepte les réponses JSON
et les résultats SSE finis ; les souscriptions persistantes et la reprise d'événements HTTP ne sont pas livrées.
Un échec de découverte par timeout est actuellement remonté ; il ne déclenche pas de fallback implicite.

```cpp
auto remote = std::make_shared<McpClient>("http://127.0.0.1:8765/mcp", token);
remote->connect();
Json tools = remote->list_tools();
auto result = remote->call_tool("document.export", {{"format", "png"}});
```

Pour `IA -> application A -> application B`, enregistrer dans A une opération qualifiée qui appelle
`remote->call_tool()` sur B. Examiner `isError` et conserver le schéma de l'opération distante ; la réussite
du transport n'est pas une réussite métier. L'application choisit explicitement les outils à déléguer.
Le jeton d'A vers B est une autorisation de service ; les droits du demandeur d'origine doivent être appliqués dans A.

## Conversations sans interface

Le [guide des fournisseurs](Providers.md) couvre le registre extensible, les profils CLI et API, leurs
permissions et l'ajout d'un protocole externe. Les classes typées ci-dessous restent utilisables directement.

```cpp
#include <cymcp/ai.hpp>
HttpChatOptions options;
options.base_url = "http://127.0.0.1:9931/v1";
options.tools = shared_operation_registry;
auto provider = std::make_shared<HttpChatProvider>(options);
Conversation conversation(provider, exact_model_id, "You assist the user of this application.");
conversation.connect(); // à exécuter sur un worker si le connecteur lance un processus

EventQueue events;
Executor workers(2);
CallContext context;
context.events = [&](const Event& event) { events.push(event); };
auto result = conversation.send_async(workers, "Export the current document", context);
// L'interface appelle events.drain() depuis son propre thread.
// conversation.cancel() annule également un tour encore en attente dans le worker pool.
```

Une seule opération active ou planifiée par conversation. Chaque conversation native possède son connecteur.
Conserver l'executor, la file et les objets capturés jusqu'à la fin du futur. `history()` retourne un snapshot
de données. Le [guide des conversations](Conversations.md) décrit `ConversationStore`, les checkpoints,
les pièces jointes et la reconnexion. Le stockage est facultatif ; sa conservation et son chiffrement sont choisis par l'hôte.

Événements : `TurnStarted`, `TextDelta`, `ThinkingDelta` lorsque disponible, `ToolStarted`, `ToolCompleted`,
`UsageUpdated`, `TurnCompleted`, `TurnCancelled`, `TurnFailed`. Les deltas et le texte final sont des présentations
du même contenu : ne pas concaténer le snapshot final aux deltas. Aucun raisonnement interne n'est inventé.

Pour Codex, renseigner `CodexOptions.process.executable` avec le binaire natif. Le connecteur garde
`app-server --listen stdio://` actif, initialise la connexion, vérifie l'état du compte et utilise les IDs natifs.
`tools` expose le registre via les outils dynamiques expérimentaux ; les noms incompatibles sont traduits
en identifiants stables. Les droits des handlers restent appliqués. Les demandes natives sont refusées par défaut
ou routées par `approvals` et `request_handler`. `approval_policy` et `sandbox` contrôlent la politique du fournisseur.
Un host qui choisit explicitement l'accès complet peut sélectionner `danger-full-access` et sa politique permissive.

Pour Claude, renseigner `ClaudeOptions.process.executable`. L'adaptateur vérifie `--version`, puis utilise
`-p --output-format stream-json --verbose --include-partial-messages`. Il reprend l'ID exact avec `--resume`.
Le mode par défaut ajoute `--input-format stream-json` et conserve stdin ouvert entre les tours.
`persistent=false` garde le mode par tour. `mcp_servers` permet de connecter le serveur de l'application ; `allowed_tools`
sélectionne les outils natifs. `allow_all_actions=true` sélectionne explicitement le mode permissif du fournisseur.
`setting_sources="user"` conserve la configuration native de l'utilisateur ; cette source était désactivée
dans les versions précédentes. L'authentification est résolue par Claude lors du tour, sans login imposé par le SDK.

Les scripts `.cmd` et `.ps1` ne sont pas exécutés par un shell. Si nécessaire, fournir `node.exe` comme exécutable
avec le script d'entrée comme premier argument. Les prompts Claude/Antigravity/ACP passent par stdin ; le profil
historique `opencode-cli` les reçoit comme argument individuel après `--`. L'installation, la connexion au
compte et la disponibilité du modèle sont trois états distincts. Le SDK ne crée pas de compte et n'effectue pas
de génération facturée pendant ses tests hors ligne.

Le connecteur HTTP utilise `/models` et `/chat/completions`, le streaming SSE, les appels de fonctions fragmentés
et une limite de tours d'outils. Il préserve les résultats d'outils dans l'historique après une erreur afin que
l'application puisse inspecter ce qui s'est déjà exécuté. Aucune relance automatique d'une action n'est effectuée.
Le transport peut transmettre des blocs multimodaux ; leur prise en charge dépend de l'endpoint et du modèle.
Les capacités multimodales varient selon le transport et le modèle : voir [Conversations.md](Conversations.md).
Les clés sont fournies en mémoire par l'hôte,
par exemple depuis son coffre système ; aucun fichier de secrets ni journal brut de protocole n'est créé.

## Moteur local

```cpp
#include <cymcp/local.hpp>
LocalEngineOptions options;
options.process.executable = absolute_llama_server_path;
options.model = absolute_gguf_path;
options.private_work_directory = app_private_cache;
options.context_tokens = 4096;
options.gpu_layers = 0; // régler selon le moteur et le matériel réellement disponibles
LocalEngine engine(options);
engine.start(); // worker ; charge une seule fois et attend une disponibilité authentifiée

HttpChatOptions chat;
chat.base_url = engine.base_url();
chat.api_key = engine.token();
// Créer ensuite HttpChatProvider et Conversation.
// Garder engine vivant pendant les échanges, puis engine.stop().
```

Le moteur écoute sur loopback avec un secret aléatoire dans un répertoire privé. Son fichier d'authentification
est supprimé à l'arrêt ; les poids existants sont référencés sans copie. Le SDK termine uniquement ses processus
possédés. Sous Windows, un Job Object possède l'arbre ; sous POSIX, un groupe de processus est utilisé.
Un processus qui quitte volontairement son groupe POSIX n'est pas un enfant supervisé garanti.
Le lancement est vérifié par des fixtures ; les mesures de génération GPU sur un vrai moteur restent à établir.

## Installation depuis l'app

`DependencyManager` reçoit un `Package` provenant d'un catalogue **de confiance fourni par l'application**.
Chaque fichier déclare son chemin relatif, URL HTTPS, taille exacte et SHA-256. Le catalogue définit aussi les
hôtes de redirection autorisés. Une empreinte fournie par une source non fiable ne prouve pas l'authenticité.

`install()` télécharge dans un dossier temporaire privé, vérifie taille et empreinte, écrit un reçu et publie
le dossier par renommage. Les chemins absolus, traversées, noms réservés Windows, ADS, liens et doublons sont refusés.
Une installation valide est réutilisée ; une altération est signalée. Les versions sont isolées. Une installation
concurrente utilise un verrou de dossier ; un verrou abandonné après crash nécessite une récupération explicite.

`run_installer()` exécute uniquement un fichier vérifié du paquet avec des arguments structurés.
Le callback `ElevationHandler` appartient à l'app, qui fournit son parcours UAC/polkit adapté.
Les codes de redémarrage sont exposés. Le SDK ne choisit ni n'installe arbitrairement un pilote.
`uninstall()` retire le cache géré correspondant au reçu ; il ne désinstalle pas un pilote du système.

Les membres réguliers déclarés d’une archive ZIP ou tar sont extraits après vérification dans le
dossier temporaire de l’installation. Le gestionnaire vérifie également la taille et l’empreinte de
chaque fichier extrait. Les anciens manifestes de fichiers individuels restent acceptés.
Le profil `cuda_runtime_package()` déploie CUDART 12.8.90 et sa licence dans le cache privé
sur Windows/Linux x64. Renseigner `LocalEngineOptions::cuda_runtime_directory` avec le sous-dossier
`runtime/` pour qu’un moteur enfant trouve la bibliothèque. Chat Lab expose ce téléchargement dans
les paramètres des services locaux. La reprise HTTP Range et les autres paquets GPU restent à compléter.

Avec un GPU NVIDIA et un pilote compatible, ce test matériel facultatif utilise Python 3.9+
et le binaire de tests compilé avec le module Local. Le dernier argument désigne un dossier
réservé au cache privé du SDK. Le vérificateur y installe ou réutilise le runtime officiel et
valide ses empreintes. La sonde charge cette bibliothèque, compte les GPU et vérifie un
aller-retour de 4 096 octets en mémoire GPU, suivi de la libération. Le pilote système doit
déjà être installé.

```powershell
python tests/cuda_device.py build/cymcp_cuda_tests.exe build/cuda-device-cache
```

Sous Linux x64, utiliser le binaire `build/cymcp_cuda_tests`. Avec un générateur CMake
multi-configuration, ajuster le chemin du binaire, par exemple `build/Release/cymcp_cuda_tests.exe`.
Le test charge `cudart64_12.dll` sous Windows ou `libcudart.so.12` sous Linux.
[API mémoire du runtime CUDA](https://docs.nvidia.com/cuda/archive/12.8.1/cuda-runtime-api/group__CUDART__MEMORY.html)

Les droits `dependencies.install`, `dependencies.runInstaller` et `dependencies.uninstall` suivent la politique hôte.
Pour `dependencies.runInstaller`, la décision reçoit aussi le chemin relatif `installer`,
les arguments `arguments` et le booléen `requires_elevation`. L’hôte peut ainsi autoriser
une invocation précise d’un paquet vérifié.

## Références de protocole

- https://modelcontextprotocol.io/specification/2026-07-28/server/discover
- https://modelcontextprotocol.io/specification/2026-07-28/server/tools
- https://modelcontextprotocol.io/specification/2025-11-25
- https://learn.chatgpt.com/docs/app-server
- https://code.claude.com/docs/en/headless
- https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md


## Host contracts (0.7.1)

Operation::schema_validation optionally supplies both check and validate callbacks for
a host schema dialect. Both are required; default validation is unchanged. The host
is responsible for complete declaration/value validation and safe local references.
LocalHttpServer accepts a TokenAuthenticator returning an opaque authenticated principal,
plus a request byte bound. Never derive identity from business JSON. The strong-token
constructor remains supported. McpServerOptions::result_text optionally projects text
for existing clients; structuredContent and output validation retain the original result.
