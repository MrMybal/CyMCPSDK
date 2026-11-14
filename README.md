# CyMCPSDK

Bibliothèque C++20 modulaire sous **MIT**, pour exposer les opérations d'une application aux IA,
intégrer des conversations et appeler les outils d'autres applications. Version de développement **0.5.1**.

Le SDK est natif : aucune dépendance Python à l'exécution. Un module de tchat **CyUI optionnel**
et une petite application Windows de test sont disponibles. Aucun modèle, moteur d'inférence,
runtime GPU ou pilote n'est embarqué.
Android est reporté. Windows et Linux constituent les cibles initiales ; macOS est prévu par les adaptateurs.
Voir [les preuves de validation et les limites](Docs/Validation.md) avant d'intégrer.

## Modules

| Cible CMake | Fonction |
|---|---|
| `CyMCP::Core` | Registre d'opérations, schémas, politiques de droits, annulation, événements et worker pool |
| `CyMCP::Platform` | Processus natifs sans shell, stdin, stdout/stderr simultanés, limites et arrêt des processus possédés |
| `CyMCP::Http` | HTTP(S) client, streaming, serveur JSON loopback authentifié |
| `CyMCP::MCP` | Serveur et client MCP : stdio, HTTP JSON, découverte et appels d'outils |
| `CyMCP::AI` | Conversations, registre extensible, agents CLI/ACP et API natives, streaming et outils |
| `CyMCP::Local` | Supervision d'un moteur local et installation de composants vérifiés depuis l'app |
| `CyMCP::ChatUI` | Module optionnel : contrôleur asynchrone et panneau de tchat natif CyUI/Markdown |
| `CyMCP::SDK` | Agrégat des modules du SDK, sans dépendance graphique |

Les modules lourds se désactivent avec `CYMCP_BUILD_HTTP`, `CYMCP_BUILD_MCP`, `CYMCP_BUILD_AI`
et `CYMCP_BUILD_LOCAL`. Le processus natif a son option `CYMCP_BUILD_PLATFORM`.
Les dépendances entre modules sont contrôlées par CMake. Le preset `core-only` compile uniquement le socle.
`CYMCP_BUILD_CHAT_UI` et `CYMCP_BUILD_CHAT_APP` sont désactivés par défaut ; CyUI est fourni par l'hôte.

## Tester le tchat Windows

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe --language fr
```

Le [guide du module ChatUI](Docs/ChatUI.md) décrit l'intégration dans une autre app.
L'exemple propose les 22 profils de fournisseurs, un catalogue de modèles réel ou une saisie manuelle,
streaming Markdown, pièces jointes, arrêt, reconnexion et historique facultatif.
Le mode **Demo / Markdown (no AI)** vérifie l'interface sans compte ; il produit du texte déterministe.
L'interface est en anglais par défaut, avec français et cinq thèmes CyUI disponibles.
La section **App commands** permet d'exposer un compteur de démonstration à l'IA,
à un client MCP local ou directement au SDK JSON. Les modifications s'autorisent explicitement.
Les [tests de l'application dans les deux sens](Docs/ChatAppValidation.md) distinguent
les échanges simulés des générations réellement exécutées avec Codex, Antigravity et Ollama.

## Compiler

Prérequis : CMake 3.24+, compilateur C++20, threads système.
Windows : Visual Studio 2022 C++ et Windows SDK. Linux : GCC 12+ ou Clang compatible, plus les
headers et bibliothèques de développement libcurl lorsque le module HTTP est activé.
Sur macOS, les sources utilisent libcurl et les API POSIX ; la validation sur machine macOS reste à réaliser.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Avec Ninja, depuis un environnement de compilation configuré :

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Les petites dépendances MIT sont versionnées dans `third_party/` : aucun téléchargement requis pour compiler.
Le SDK utilise WinHTTP et BCrypt sous Windows, libcurl et le générateur aléatoire système sur Unix.
Le workflow `.github/workflows/build.yml` prépare les builds Windows, Linux et macOS ; sa présence ne prouve
pas leur exécution. Aucune CI n'est déclenchée par une compilation locale.

## Essayer sans IA ni compte

Dans `build/Release/` avec Visual Studio, ou directement dans `build/` avec Ninja :

```sh
cymcp_example describe
cymcp_example invoke sum examples/sum.json
cymcp_example mcp
```

`invoke` lit un fichier JSON pour éviter les problèmes d'échappement du shell.
`jsonl` accepte `{"method":"invoke","name":"sum","arguments":{"a":2,"b":3}}`, une requête par ligne.
Le protocole applicatif JSONL et MCP sont distincts.

`serve [PORT]` expose `/v1` (JSON applicatif) et `/mcp` sur `127.0.0.1`.
Configurer au préalable `CYMCP_TOKEN` avec une valeur aléatoire de 32 caractères minimum.
Chaque requête utilise `Authorization: Bearer ...`. Le secret n'est pas affiché ni écrit dans un fichier de configuration.

## Intégrer à une application

```cmake
set(CYMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CYMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/CyMCPSDK)
target_link_libraries(my_app PRIVATE CyMCP::Core CyMCP::MCP)
```

Une installation CMake est également fournie :

```sh
cmake --install build --config Release --prefix install
```

Puis `find_package(CyMCPSDK CONFIG REQUIRED)` et les mêmes cibles `CyMCP::*`.
Lire [le guide d'intégration](Docs/Integration.md) pour les droits, les conversations,
les autres applications et les moteurs locaux. Les exemples en ligne de commande restent indépendants
de toute interface graphique ; l'application Windows utilise le module optionnel ChatUI.

Le [guide des fournisseurs](Docs/Providers.md) décrit les **22 profils intégrés**, leurs capacités et l'ajout
de nouveaux connecteurs par factory. `cymcp_chat providers` imprime le catalogue ; `cymcp_chat models CONFIG.json`
découvre les modèles et `cymcp_chat CONFIG.json` dialogue par événements JSONL.
OpenCode utilise désormais ACP persistant ; le CLI 1.x reste disponible sous `opencode-cli`.
Les agents intégrés comprennent aussi Gemini CLI, Qwen Code, Kimi et goose. Command Code et CodeBuddy
possèdent chacun un profil ACP persistant supplémentaire. Les capacités sont négociées avec chaque agent.
Claude Code et Antigravity gardent leur processus de conversation ouvert par défaut dans le registre.
Claude conserve les réglages utilisateur natifs et ne bloque pas la connexion sur un contrôle de login préalable.
Le [guide des conversations](Docs/Conversations.md) couvre les pièces jointes portables, la sauvegarde privée
optionnelle, la reprise exacte après déconnexion et les mesures du premier token et des tours chauds.
`cymcp_chat benchmark CONFIG.json` mesure un fournisseur configuré avec de vrais échanges ; il ne lance
aucune installation. Les essais cloud de cette machine n'ont pas encore produit de génération réussie.

## Licence

Copyright (c) 2026 Cyberalien. Code du SDK sous [MIT](LICENSE).
Conserver également les [notices des dépendances](THIRD_PARTY_NOTICES.md).
Les composants externes téléchargés conservent leurs licences.
