---
id: operations
title: Exposer les opérations de l'app
description: Partager un registre typé entre appels natifs, SDK JSON, MCP et outils IA compatibles.
section: SDK
parent: integration
order: 1
sourceRevision: 1
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
sur le serveur local activé, avec `Content-Type: application/json` et le jeton Bearer.

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

## Attacher les outils à une conversation {#tools}

Fournir un registre partagé dans `ProviderConfig.tools` pour les API compatibles ou les outils
dynamiques Codex. Ce registre porte la politique des opérations réelles de l'app. Pour les autres
CLI, exposer MCP et passer leur configuration MCP native. ACP utilise `settings.mcp_servers`
sous forme de tableau au format ACP. Consulter les [droits](doc:permissions) avant d'exposer des mutations.
