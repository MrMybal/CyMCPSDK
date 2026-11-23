---
id: integration
title: Intégrer la bibliothèque C++
description: Lier uniquement les modules nécessaires et déporter les fournisseurs hors du thread UI.
section: SDK
order: 0
sourceRevision: 2
---

## Prérequis de compilation {#build}

Utiliser CMake 3.24+, C++20 et les threads système. Windows utilise Visual Studio 2022 C++,
Windows SDK, WinHTTP et BCrypt. Linux/macOS utilisent les API POSIX et les fichiers de
développement libcurl pour HTTP(S). Les limites de distribution multiplateforme actuelles
figurent dans les notes de version des sources.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
cmake --install build --config Release --prefix install
```

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
