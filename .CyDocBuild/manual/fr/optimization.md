---
id: optimization
title: Outils à la demande, ressources et grep
description: Garder toutes les commandes accessibles en réduisant les catalogues et les résultats transmis.
section: SDK
parent: integration
order: 2
sourceRevision: 2
---

## Découvrir sans perdre de fonctions {#tools}

Le [stockage disque, les mesures, les abonnements et les fenêtres de contexte API](doc:runtime-options)
complètent ces fonctions par des options indépendantes, désactivées par défaut.

`Registry` reste la référence. Les appels SDK directs et les outils MCP classiques gardent leurs
noms, arguments, schémas de sortie et droits. `describe()` renvoie toujours le catalogue complet.
Les instantanés immuables `catalogue()` et `summaries()` (sans schémas) sont réutilisés jusqu'à un
`add`, `replace` ou `remove` ; `describe(name)` charge une définition complète précise.

`ToolGateway` expose cinq outils standard à la place de toutes les définitions :

| Outil | Fonction |
| --- | --- |
| `cy_tools_search` | Rechercher noms/descriptions ; sans query, parcourir toutes les commandes. Suivre nextCursor avec la même query. |
| `cy_tools_describe` | Obtenir un schéma d'entrée/sortie complet. |
| `cy_tools_call` | Appeler la commande d'origine avec ses arguments et le contexte de l'appelant. |
| `cy_resources_read` | Lire une ressource, un manifeste ou un morceau. |
| `cy_resources_grep` | Rechercher dans les ressources texte sans charger tout leur contenu. |

Recherche et description n'exécutent aucune commande. L'appel valide les schémas d'origine
et applique leur politique, y compris l'autorisation totale explicite. Aucune commande n'est
définitivement masquée. Un changement de registre invalide les curseurs de recherche.

```cpp
#include <cymcp/mcp.hpp>
#include <cymcp/resources.hpp>

auto commands = std::make_shared<cymcp::Registry>(host_policy);
// Enregistrer toutes les opérations de l'application dans commands.
cymcp::ToolGateway gateway(commands);
cymcp::McpServerOptions options;
options.resources = gateway.resources();
cymcp::McpServer server(*gateway.tools(), "My application", options);
// Garder gateway/commands vivants pendant le service. Pour une API : config.tools = gateway.tools().
```

Les transports API et Codex choisissent ce mode à leur construction au-delà de 32 outils.
`settings.tool_discovery_threshold` règle le seuil ; `settings.deferred_tools=false` conserve les
définitions complètes. Les options C++ exposent tool_discovery_threshold (maximum de size_t
pour désactiver). Une passerelle explicite fonctionne avec tout transport acceptant les outils.
Les autres agents natifs gardent leur configuration MCP ; aucune injection CLI non supportée
n'est simulée.

## Lire uniquement les données nécessaires {#resources}

`ResourceRegistry` appartient à `CyMCP::Core` et fonctionne sans MCP ni HTTP. Une `Resource`
déclare URI, nom, MIME, taille en octets et callback de lecture bornée sur un **instantané immuable
fourni par l'hôte**. Aucun fichier ni réseau n'est exposé implicitement. Les lectures appliquent leur
politique (`resources.read`, argument uri demandé), l'annulation et l'identité de l'appelant.

MCP annonce resources/list, resources/read et un catalogue de modèles vide seulement si
McpServerOptions.resources est fourni. Lister ne lit aucun contenu. Les petites ressources sont
renvoyées directement ; les grandes donnent un manifeste JSON (type cymcp.resource.v1), avec
firstUri, MIME d'origine, taille et taille des morceaux. Lire firstUri puis suivre
`_meta["com.cymcp/chunk"].nextUri` jusqu'à eof. Le texte préserve l'UTF-8 ; le binaire utilise base64.
Un extrait ne représente jamais le résultat complet.

Les résultats de passerelle au-delà de 16 Kio deviennent des références privées ; MCP ajoute un
resource_link standard. Les lire avec resources/read ou cy_resources_read. Valeurs par défaut :
morceaux de 32 Kio, 64 Mio conservés, 128 résultats et cinq minutes de rétention, tous réglables.
Les résultats privés ne sont pas listés et sont liés à CallContext.principal. L'hôte doit distinguer
les identités à isoler. Remplacer un instantané invalide ses anciens URI de morceaux. Expiration
ou éviction LRU renvoie ResourceNotFound sans réexécuter la commande.

Un résultat dépassant le budget de rétention reste en ligne, sans troncature ni nouvel appel.
Pour les jeux de données arbitrairement grands, fournir dès le départ une ressource sur instantané
ou une commande paginée. La passerelle ne supprime pas la mémoire déjà allouée par un handler
qui renvoie un gros objet JSON. Les outils classiques gardent leur sortie structurée : leur
outputSchema n'est pas remplacé par une référence incompatible.

## Cache et compatibilité {#cache}

tools/list et resources/list ont des curseurs liés à leur révision (64 entrées par page par défaut).
McpClient::list_tools() agrège toujours toutes les pages ; list_tools_page() en lit une seule.
L'absence de nextCursor termine la liste ; un curseur vide reçu d'un autre serveur reste accepté.
get_tool(name) utilise cymcp/tools/get si annoncé, sinon le catalogue complet d'un ancien serveur.
L'extension est facultative ; les cinq outils compacts utilisent des appels MCP ordinaires.

Le cache est privé à chaque client, indexé par méthode et ensemble des paramètres. Seules les
découvertes, descriptions et lectures avec ttlMs positif sont conservées. Par défaut : 256 entrées,
16 Mio sérialisés et TTL maximal de 30 secondes. TTL nul ou limites locales nulles désactivent
le cache. tools/call et les erreurs ne sont jamais mis en cache. Aucun sondage périodique.
Une modification peut rester invisible jusqu'au TTL : clear_cache() force un rafraîchissement
après un changement connu. Reconnexion, fermeture et notifications de changement reçues
invalident le cache. Une erreur d'outil le vide sans réessayer l'opération. cache_stats() indique
les accès, défauts de cache et octets conservés.

## Recherche texte rapide {#grep}

cy_resources_grep parcourt les instantanés texte par morceaux : recherche littérale, ignore_case
ASCII facultatif, numéros de ligne, positions en octets et liens readUri. Une page contient au plus
50 lignes correspondantes. Continuer avec les mêmes text/ignore_case et nextCursor. Le binaire
et les ressources refusées par la politique sont exclus. Aucun parcours du système de fichiers.

Pour les fichiers, CyMCP::Platform propose un adaptateur **ripgrep** optionnel :

```cpp
#include <cymcp/search.hpp>

cymcp::FileSearchOptions search_options;
search_options.root = host_selected_folder; // Choix explicite de l'hôte/utilisateur.
// search_options.ripgrep = trusted_rg_executable; // Sinon rg est résolu dans PATH.
cymcp::FileSearch search(search_options);
search.add_to(*commands); // workspace.grep et workspace.read, découvrables via la passerelle.
```

L'hôte fournit séparément un exécutable ripgrep de confiance ; le SDK ne l'embarque ni ne le
télécharge. Les regex utilisent le moteur Rust par défaut. Arguments directs, sans shell ni config rg.
workspace.grep accepte pattern, regex (true par défaut), ignore_case, glob et include_ignored
(false par défaut). Les fichiers cachés sont inclus ; métadonnées Git et liens symboliques exclus.
Les exclusions restent actives sauf include_ignored ou sélection explicite par un glob de fichier. Ce périmètre
n'est pas une sandbox système contre un autre processus modifiant simultanément les fichiers ;
l'hôte doit isoler les arborescences non fiables.

Les pages contiennent par défaut au plus 100 lignes, path/line/byteOffset et des extraits UTF-8
bornés. truncated et lineBytes signalent les longues lignes ; workspace.read permet de lire tout
le texte par plages. Continuer avec **cursor seul** ; chaque curseur remplace le précédent.
Les processus sont possédés, annulables, limités à huit recherches simultanées et expirent après
60 secondes d'inactivité. skippedNonUtf8 compte les correspondances non UTF-8 ignorées par
cet adaptateur texte. Les limites de processus/ligne déclenchent une erreur explicite.

workspace.read accepte un chemin relatif, offset, length (1–65536 octets) et revision facultative.
Suivre nextOffset avec la même revision détecte un changement de fichier. Les frontières UTF-8
sont préservées ; les contenus binaires/non UTF-8 sont refusés. Aucune écriture ni dossier hors
périmètre n'est exposé. La politique du registre décide toujours des appels autorisés.

## Essayer dans Chat Lab {#example}

Les deux commandes du compteur restent inchangées par défaut. Dans une configuration JSON
privée passée à --config, activer le mode compact et choisir éventuellement un dossier en lecture seule :

```json
{
  "app_commands": {
    "deferred_tools": true,
    "workspace": {"root": "./sample-workspace"}
  }
}
```

Choisir un dossier existant. workspace.ripgrep peut indiquer l'exécutable de confiance. Aucun
dossier n'est configuré automatiquement. Le mode compact s'applique à MCP et aux outils IA
directs supportés ; le SDK JSON garde les noms d'origine. Registre et droits sont partagés.

## Références du protocole {#references}

L'implémentation suit les sections MCP [outils](https://modelcontextprotocol.io/specification/2026-07-28/server/tools),
[ressources](https://modelcontextprotocol.io/specification/2026-07-28/server/resources),
[cache](https://modelcontextprotocol.io/specification/2026-07-28/server/utilities/caching) et
[pagination](https://modelcontextprotocol.io/specification/2026-07-28/server/utilities/pagination).
Les manifestes et la description ciblée sont des conventions CyMCPSDK facultatives, pas des
méthodes MCP imposées.
