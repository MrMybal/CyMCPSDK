---
id: integration
title: Intégrer la bibliothèque C++
description: Lier uniquement les modules nécessaires et déporter les fournisseurs hors du thread UI.
section: Développer avec le SDK
order: 20
sourceRevision: 5
previous:
next: operations#registry
---

## Prérequis de compilation {#build}

**Objectif :** lier le SDK, enregistrer une opération autorisée, puis connecter un fournisseur
sur un worker. Poursuivre avec les [opérations](doc:operations#registry) et les [droits](doc:permissions#policy).

Utiliser CMake 3.24+, C++20 et les threads système. Windows utilise Visual Studio 2022 C++,
Windows SDK et BCrypt. Linux/macOS utilisent les API POSIX. HTTP(S) utilise libcurl
sur les trois plateformes. Installer ses fichiers de développement avec un backend TLS
et renseigner `CURL_ROOT` ou `CMAKE_PREFIX_PATH` pour une installation personnalisée.
Le script de compilation de Chat Lab accepte `-CurlRoot CHEMIN`. Les consommateurs du
SDK installé ont aussi besoin de `CURL::libcurl`. Les limites de distribution multiplateforme actuelles
figurent dans les notes de version des sources.

L’exemple Chat Lab en fenêtre sous Linux nécessite les fichiers de développement de SDL3 3.4+
et le dépôt CyUI complet, avec son hôte d’exemple portable. Depuis une session graphique, lancer
`bash tools/build-chat-linux.sh ../CyUI`. Renseigner `CMAKE_PREFIX_PATH` pour une installation SDL3
personnalisée. Le script compile, teste et prépare une archive locale de l’application. SDL3 reste
limité à l’exemple ; la bibliothèque `CyMCP::ChatUI` conserve le backend de rendu de l’application hôte.

Le transport HTTP peut se compiler sur Android avec libcurl compilée par le NDK pour l’ABI choisie.
HTTPS nécessite aussi un backend TLS et un ensemble de certificats de confiance configuré pour ce build.
Le SDK nécessite une bibliothèque standard C++20 qui fournit `std::jthread` et `std::stop_token`.
L’intégration Android complète, y compris les fournisseurs CLI natifs, reste reportée.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
cmake --install build --config Release --prefix install
```

**Résultat attendu :** `install/include/cymcp/` contient les en-têtes publics et l'arbre installé
contient le paquet CMake exporté. Faire pointer `CMAKE_PREFIX_PATH` de l'hôte vers ce préfixe.
Si la configuration ne trouve pas `CURL::libcurl`, installer les fichiers de développement libcurl
avec TLS et renseigner `CURL_ROOT` avant de relancer la configuration.

Un hôte qui consomme l'installation utilise les cibles exportées :

```cmake
find_package(CyMCPSDK 0.5 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE CyMCP::Core CyMCP::MCP CyMCP::AI)
```

Autre possibilité : `add_subdirectory(external/CyMCPSDK)`. Désactiver les modules inutiles
avec `CYMCP_BUILD_HTTP`, `CYMCP_BUILD_PLATFORM`, `CYMCP_BUILD_MCP`, `CYMCP_BUILD_AI` et
`CYMCP_BUILD_LOCAL` ; CMake vérifie leurs dépendances. `core-only` compile le cœur des opérations.

## Créer une conversation {#providers}

```cpp
#include <cymcp/ai.hpp>

cymcp::ProviderRegistry providers;
cymcp::ProviderConfig config;
config.settings["base_url"] = "http://127.0.0.1:11434";
auto provider = providers.create("ollama", config);
```

Construire `Conversation(provider, model_id)` avec l'ID réel d'un modèle installé, puis appeler
`connect()` et `send(text_block(prompt))`. Ces appels synchrones s'exécutent sur un worker.
`send_async(executor, content, context)` retourne un futur ; garder executor et fournisseur en vie.
`CallContext.events` reçoit les deltas et l'activité. Les transférer par `EventQueue` vers
l'interface. `cancel()` demande l'annulation ; `reconnect()` reste explicite.

**Résultat attendu :** les événements de connexion arrivent sur le worker ; un message envoyé
volontairement produit des deltas de texte et un événement de fin. Vérifier d'abord le catalogue
de modèles et l'authentification du fournisseur. L'exemple crée uniquement le fournisseur ;
une conversation demande un service Ollama démarré et un modèle installé pour répondre.
Voir le [dépannage de connexion](doc:troubleshooting#connection).

## Intégrer le panneau CyUI {#chat-ui}

`CYMCP_BUILD_CHAT_UI` et `CYMCP_BUILD_CHAT_APP` sont désactivés par défaut. L'hôte fournit
`CyUI::DocumentViews`, ou renseigne `CYMCP_CYUI_SOURCE_DIR` avec une copie compatible de CyUI.
Un consommateur installé crée sa cible CyUI avant de demander `COMPONENTS ChatUI`.
Utiliser CyUI `14e1c6f` ou une révision ultérieure pour les corrections de sélection et de retour
à la ligne. Compiler le SDK et l’application hôte avec la même révision de CyUI.

```cpp
#include <cymcp/chat_ui.hpp>

// Après la création du contexte ImGui et l'initialisation de CyUI par l'hôte :
cymcp::ChatController controller;
cymcp::ChatPanel panel(controller);

// Dans la fenêtre hôte, entre NewFrame et Render :
panel.draw();
```

`ChatController::open(ChatConnection)` programme la connexion. `send`, `refresh_models`,
`save`, `disconnect` et `reconnect` programment aussi des opérations sur le worker ; `poll`
transfère leurs résultats. Utiliser le contrôleur et son état depuis un seul thread UI hôte.
Le détruire avant le contexte ImGui et les modules fournisseurs. Son destructeur annule et attend le worker.

`ChatPanelOptions` fournit la sélection des fichiers, l'ouverture des liens et les ressources
de documents en cache. Sans callback de lien, le panneau propose le lien à copier.
Initialiser la locale CyUI de l'hôte à `en` pour l'anglais ; c'est le défaut de l'exemple Windows.
