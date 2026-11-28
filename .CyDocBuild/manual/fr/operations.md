---
id: operations
title: Exposer les opérations de l'app
description: Partager un registre typé entre appels natifs, SDK JSON, MCP et outils IA compatibles.
section: Développer avec le SDK
order: 21
sourceRevision: 4
parent: integration
previous: integration#build
next: permissions#policy
---

## Définir un registre {#registry}

Une `Operation` contient un nom portable, une description, des schémas objet d'entrée/sortie,
un indicateur de lecture seule et un handler. `Registry` valide les entrées, applique sa politique
et vérifie les sorties déclarées. L'indicateur de lecture seule ne remplace pas une politique de refus.

```cpp
#include <cymcp/core.hpp>

cymcp::Registry commands(cymcp::Policy::allow_all());
commands.add({"get_counter", "Lire le compteur de démonstration.",
    {{"type", "object"}, {"additionalProperties", false}},
    {{"type", "object"},
     {"properties", {{"value", {{"type", "integer"}}}}},
     {"required", {"value"}}, {"additionalProperties", false}},
    true,
    [](const auto&, const auto&) { return cymcp::Json{{"value", 42}}; }});
auto result = commands.invoke("get_counter", cymcp::Json::object());
```

Cet exemple autorise explicitement sa lecture sans effet de bord. Pour un domaine modifiable,
utiliser la politique de l'hôte et synchroniser ses objets. Les handlers MCP peuvent être concurrents ;
transférer les mutations liées à l'UI vers la boucle d'événements et garder les données capturées en vie.

## SDK JSON direct {#sdk}

`dispatch_app(registry, request, context)` accepte ces messages applicatifs :

```json
{"method":"describe"}
```

```json
{"method":"invoke","name":"get_counter","arguments":{}}
```

Le CLI d'exemple propose `describe`, `invoke` et le JSONL applicatif sur stdin. JSONL applicatif et
JSON-RPC MCP sont deux protocoles distincts. Dans Chat Lab, ces mêmes requêtes vont vers `/v1`
sur le serveur local activé, avec `Content-Type: application/json` et un jeton Bearer lorsque le profil l’exige.

## Serveur et client MCP {#mcp}

`McpServer(registry)` expose les opérations comme outils MCP. `serve_stdio(input, output)` sert
une connexion CLI ; `handle(request, context)` s'intègre à un endpoint HTTP. Garder registre et
serveur en vie pendant toute la connexion.

`McpClient(ProcessOptions)` se connecte par stdio ; `McpClient(url, token)` utilise HTTP local
lorsque ce support est compilé. Appeler `connect`, `list_tools` et `call_tool(name, arguments)`.
Vérifier `isError` dans le résultat MCP avant de traiter `structuredContent` comme une sortie réussie.

Pour qu'une IA dans l'app A utilise l'app B, connecter un client à B, enregistrer des opérations
de transfert explicites dans A et attacher ce registre à un fournisseur compatible. Conserver le
schéma de B et appliquer la politique de A avant le transfert. Une réponse de modèle ne doit pas
pouvoir enregistrer librement de nouvelles opérations.

## Découverte MCP locale {#discovery}

Un hôte HTTP peut publier son identité pour un hub local avec `LocalMcpEndpoint` :

```cpp
cymcp::McpServer mcp(commands, "Mon application");
cymcp::LocalMcpEndpoint identity;
identity.name = "Mon application";
identity.version = cymcp::version;
cymcp::LocalHttpServer server(cymcp::LocalHttpServer::UnauthenticatedLoopback{},
    [&](const auto& message, const auto& context) { return mcp.handle(message, context); },
    4, cymcp::provider_message_limit, identity);
const int port = server.start();
```

Cet exemple ouvre explicitement MCP aux appelants locaux. Les constructeurs avec jeton ou
authentificateur acceptent le même dernier argument. Sans cet argument, les constructeurs
existants conservent leur comportement. `GET /.well-known/cymcp` publie uniquement le nom,
la version, le chemin `/mcp` et l’indicateur `requiresToken`, sans demander de jeton.
Les contrôles d’Origin et de Host local restent appliqués. Chat Lab et l’exemple HTTP sans
interface publient cette identité dans tous les profils d’accès.

Cette annonce est une extension CyMCPSDK, lue par un hub qui sonde les écouteurs locaux.
Elle ne diffuse rien sur le réseau local et ne s’inscrit pas à un service central.
`windows_mcp_listeners(available)` énumère les véritables écouteurs Windows locaux ou sur toutes
les interfaces, avec leur PID. Sur les autres plateformes, la liste est vide et `available=false` ;
le hub fournit alors son propre inventaire des écouteurs.

`probe_local_mcp(url, cancellation, timeout)` fonctionne sous Windows et Linux. Il vérifie une
URL locale avec la découverte moderne, puis l’initialisation historique si nécessaire. Il gère JSON
et SSE, ferme les sessions historiques temporaires lorsque le délai le permet et n’énumère ni
n’appelle les outils. Le délai positif est un budget total, de 800 ms par défaut ; les réponses
sont limitées à 64 Kio. L’annulation de l’appelant est propagée. Une réponse 401/403 générique
reste un candidat anonyme à authentifier ; un MCP protégé est identifié uniquement s’il publie
des métadonnées d’identité publique valides.

Pour un handler MCP distinct, renseigner `identity.handler`. `identity.requires_token` peut ouvrir
`/mcp` tout en conservant l’authentification d’origine sur `/v1`. Exiger un jeton sans authentificateur
refuse les appels MCP. `identity.enabled` peut masquer `/mcp` et son annonce sans désactiver `/v1`.
Ces callbacks s’exécutent sur les workers HTTP ; synchroniser l’état mutable de l’hôte.
Les droits du registre restent indépendants de l’authentification du transport.

## Attacher les outils à une conversation {#tools}

Fournir un registre partagé dans `ProviderConfig.tools` pour les API compatibles ou les outils
dynamiques Codex. Ce registre porte la politique des opérations réelles de l'app. Pour les autres
CLI, exposer MCP et passer leur configuration MCP native. ACP utilise `settings.mcp_servers`
sous forme de tableau au format ACP. Consulter les [droits](doc:permissions) avant d'exposer des mutations.

Voir [les outils à la demande, ressources et grep](doc:optimization) pour réduire les échanges
tout en conservant toutes les commandes et leur politique d'accès.

## Suivre une requête {#request-flow}

Cette séquence décrit une invocation par le registre. Un refus de droits ou une entrée invalide
arrête la requête avant le gestionnaire. L'authentification du transport est vérifiée séparément,
avant l'invocation.

```diagram
{"type":"sequence","title":"Une opération, un contrôle des droits à l’invocation","description":"Le registre valide la requête et applique la politique de l’hôte avant d’appeler le gestionnaire. Il vérifie la sortie déclarée avant de renvoyer un résultat réussi.","actors":[{"id":"caller","label":"Appelant SDK / MCP / IA"},{"id":"registry","label":"Registre + droits"},{"id":"handler","label":"Gestionnaire hôte"}],"messages":[{"from":"caller","to":"registry","label":"Nom + arguments"},{"from":"registry","to":"handler","label":"Appel validé et autorisé"},{"from":"handler","to":"registry","label":"Résultat de l’opération","reply":true},{"from":"registry","to":"caller","label":"Résultat conforme au schéma","reply":true}]}
```

Pour un premier exercice complet, enregistrer l'opération de lecture `get_counter` ci-dessus,
l'exposer par `McpServer`, puis l'appeler depuis `McpClient`. **Résultat attendu :** le client reçoit
`{"value":42}` sans `isError`. Modifier la politique hôte pour refuser l'opération puis répéter la lecture :
l'appel doit être rejeté avant l'exécution du gestionnaire. Voir les [droits](doc:permissions#policy).
