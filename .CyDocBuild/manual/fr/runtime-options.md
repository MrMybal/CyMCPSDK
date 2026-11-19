---
id: runtime-options
title: Optimisations d'exécution optionnelles
description: Configurer les gros volumes sur disque, les mesures, les notifications et le contexte API.
section: SDK
parent: optimization
order: 0
sourceRevision: 1
---

## Options indépendantes {#options}

Les quatre fonctions sont facultatives. Core fournit ressources, stockage temporaire, journal de
changements et mesures sans dépendance graphique ni IA. ContextWindow appartient à AI. Aucune
dépendance externe supplémentaire ni modèle de résumé n'est installé. Sans configuration, les
apps gardent l'historique API complet, les résultats conservés en RAM et les caches par TTL.

## Produire les gros volumes progressivement {#disk}

```cpp
#include <cymcp/resources.hpp>

cymcp::ResourceOptions storage;
storage.disk_directory = host_selected_cache_folder;
storage.max_stored_bytes = 8 * 1024 * 1024;
storage.spool_threshold = 1024 * 1024;
storage.max_disk_bytes = 1024ULL * 1024 * 1024;
auto resources = std::make_shared<cymcp::ResourceRegistry>(host_resource_policy, storage);
auto handle = resources->store_stream(
    [snapshot_blocks](std::ostream& output, const cymcp::CallContext& context) {
        for (const auto& block : snapshot_blocks) {
            context.cancellation.check();
            output.write(block.data(), block.size());
        }
    }, "text/plain", caller_context);
```

L'hôte choisit le dossier parent. Le SDK crée un sous-dossier aléatoire réservé au propriétaire et
des fichiers privés ; les droits du parent restent inchangés. Au-delà du seuil, la production passe
sur disque. Garder ce cache privé hors des dossiers exposés par d'autres outils de fichiers.
Le quota inclut fichiers conservés, producteurs actifs et lecteurs qui gardent un
instantané. Les anciens résultats peuvent être évincés ; un quota occupé par des opérations
actives produit une erreur explicite. Quotas RAM/disque distincts, producteur appelé une seule fois,
publication de la référence seulement après réussite complète.

Lire la référence par les manifestes/morceaux existants. Pour le binaire, passer true en quatrième
argument de store_stream : les contenus utilisent base64. Le texte respecte les frontières UTF-8.
Échec, annulation, expiration et destruction normale libèrent les fichiers possédés. Le nettoyage
disque vérifie les expirations environ chaque seconde ; une lecture active peut conserver son
fichier immuable. Un arrêt brutal peut laisser un dossier privé orphelin à nettoyer par l'hôte.
Aucun parcours/suppression des dossiers étrangers ni reprise implicite d'un producteur interrompu.

storage_stats() indique les octets RAM/disque conservés et le nombre de résultats, sans chemin.
ToolGateway sérialise ses gros résultats JSON directement dans le flux via store_json, sans créer
une deuxième copie sérialisée complète. L'objet JSON du handler reste alloué : pour de plus gros
volumes, produire directement une ressource progressive. Si tous les budgets sont dépassés, la
passerelle garde son repli historique vers le résultat complet en ligne ; l'API de flux directe renvoie
ResourceTooLarge sans tronquer. TTL/LRU sont temporaires et ne remplacent pas des archives.

## Mesurer sans enregistrer les contenus {#performance}

```cpp
#include <cymcp/performance.hpp>

cymcp::PerformanceOptions options;
options.jsonl_file = host_selected_private_log_file; // Facultatif : mémoire seule sinon.
auto performance = std::make_shared<cymcp::PerformanceMonitor>(options);
commands->set_performance_monitor(performance);
auto totals = performance->snapshot();
auto recent_records = performance->drain();
```

Autres points d'intégration : CallContext.performance, McpServerOptions.performance,
McpClientOptions.performance et ChatConnection.performance. Les lignes contiennent horodatage,
stage/name, statut, elapsed_ms, input_bytes, output_bytes et cache_hit. Aucun argument, réponse,
explication d'erreur, identité, secret ou ID de requête. Les étapes distinguent operation, mcp.server,
mcp.client et conversation. La sortie d'une conversation compte le texte UTF-8 final ; les autres
tailles comptent le JSON sérialisé. Ce ne sont pas des nombres de tokens. Le calcul parcourt le
JSON sans en allouer une copie sérialisée et ne s'effectue que si le moniteur est activé.

Les statistiques conservent appels/erreurs/cache, volumes, moyenne/maximum et p50_upper_ms,
p95_upper_ms : bornes de classes d'histogramme, pas percentiles exacts. Catégories et lignes sont
bornées ; les catégories excédentaires vont dans other. La file d'écriture est bornée ;
dropped_file_records et file_errors signalent les échecs. drain() ne supprime pas les agrégats.
Le chemin fourni est ouvert comme nouveau fichier privé ; rotation/rétention relèvent de l'hôte.
Les mesures ne changent ni les droits, ni les appels, ni les règles de cache.

## Recevoir les changements {#notifications}

```cpp
#include <cymcp/changes.hpp>
#include <cymcp/mcp.hpp>

auto changes = std::make_shared<cymcp::ChangeFeed>();
commands->set_change_feed(changes);
resources->set_change_feed(changes);
cymcp::McpServerOptions options;
options.resources = resources;
options.changes = changes;
cymcp::McpServer server(*commands, "My app", options);
// Côté client, après connect() :
client.start_notifications({{"toolsListChanged", true},
    {"resourcesListChanged", true},
    {"resourceSubscriptions", cymcp::Json::array({"app://state"})}});
```

add/replace/remove du registre publie les changements d'outils. Les ressources enregistrées
publient changements de catalogue et d'URI. Pour une modification externe, remplacer l'instantané
ou publier resource avec son URI dans le même journal. Aucun contenu n'est transmis. Les URI
sont autorisés à l'abonnement ; leur suppression/révocation invalide aussi les abonnements déjà
accordés. Les lectures appliquent toujours les droits actuels. Les résultats privés générés restent
absents des catalogues publics. Un journal débordé signale les événements manqués pour invalider
largement plutôt que garder silencieusement des données périmées.

Le serveur implémente subscriptions/listen MCP 2026 : accusé initial, ID d'abonnement, filtres
explicites et annulation. HTTP utilise un POST SSE ; LocalHttpServer fournit le canal. Un transport
personnalisé doit fournir CallContext.notifications (null représente un battement de maintien).
Stdio utilise un pool d'abonnements séparé, sans monopoliser le worker des commandes. Par défaut :
deux flux actifs et 64 URI par flux, réglables par max_subscriptions/max_resource_subscriptions dans
McpServerOptions. Chaque flux HTTP occupe un worker : en prévoir davantage que
de flux (quatre par défaut). La fermeture du serveur annule les abonnements.

Le client crée un lecteur uniquement avec start_notifications. Les événements invalident ses
caches avant le prochain accès, y compris lorsqu'ils arrivent pendant une requête précédente.
drain_notifications sert à l'intégration hôte/UI ; notification_status fournit filtre accepté, état
et code d'erreur terminal. Aucun callback hôte n'est exécuté sur le thread lecteur.
stop_notifications annule et joint ce thread sans réessayer de commande. Un échec invalide le
cache et reste visible ; reconnexion explicite. Les pairs MCP 2025 conservent TTL/clear_cache et
renvoient Unsupported pour cette API. Un serveur non configuré n'annonce pas les abonnements.

## Borner le contexte API et garder l'historique complet {#context}

```cpp
cymcp::ContextOptions context;
context.max_bytes = 128 * 1024; // Octets JSON, pas taille de fenêtre en tokens.
context.recent_turns = 8;
conversation.configure_context(context);
```

Configurer une Conversation inactive, avant utilisation ou restauration. max_bytes=0 désactive la
projection. La vue envoyée garde au plus recent_turns tours complets dans le budget. Instructions
système et tour utilisateur actif restent présents. Les appels/résultats d'outils ne sont jamais
séparés de leur tour. Un message de référence signale l'historique omis. Historique, transcription
et sauvegardes complets restent inchangés ; l'hôte peut exposer des commandes de lecture bornée
de l'historique si l'IA a besoin des détails anciens. Cela borne la vue envoyée, pas la RAM de toute
la transcription conservée par l'hôte.

ContextOptions.summarize est un callback facultatif recevant previous_summary, les nouveaux
messages omis et max_bytes. Il ne s'exécute que si nécessaire, ne lance aucune IA implicitement et
doit renvoyer un texte UTF-8 borné. Le résumé est une référence assistant, jamais une nouvelle
instruction système. Édition, suppression et restauration réinitialisent le résumé incrémental.
Options et callbacks restent une configuration de l'hôte, absente des sauvegardes.

Un système ou message courant trop gros produit ContextTooLarge avant génération. Si un tour
d'outils actif dépasse le budget, ses appels déjà exécutés et leurs résultats restent conservés,
sans rejeu. Un nouveau tour utilisateur peut reprendre avec une projection adaptée. Les API
vérifient le budget entre les tours d'outils. Schémas d'outils et enveloppes fournisseur ajoutent du
volume hors de ce budget de messages. Les CLI gardent leur comportement natif ; les configurer
explicitement via cette API renvoie Unsupported. ChatController applique ChatConnection.context
uniquement aux API dont l'historique est géré par l'hôte.

## Configuration Chat Lab {#chat-lab}

```json
{
  "context": {"max_bytes": 131072, "recent_turns": 8},
  "performance": {"enabled": true},
  "app_commands": {
    "notifications": true,
    "storage": {"directory": "./private-cache", "disk_bytes": 1073741824}
  }
}
```

Fusionner uniquement les options souhaitées dans une configuration fournisseur privée existante.
Les mesures apparaissent dans Logs ; performance.file choisit un fichier JSONL privé séparé.
Le mode notification expose aussi app://chat-lab/counter, mis à jour par les écritures SDK et MCP.
Les anciennes configurations et les deux commandes d'origine restent utilisables.
