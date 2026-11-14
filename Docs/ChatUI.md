# Tchat natif CyUI

Le module optionnel `CyMCP::ChatUI` apporte un contrôleur asynchrone et un panneau de
conversation natif C++20. Il utilise `CyUI::DocumentViews` : aucun navigateur, CEF,
Python ou serveur web n'est nécessaire. Les modules du SDK et son agrégat
`CyMCP::SDK` restent indépendants de l'interface graphique.

## Application Windows de test

Depuis PowerShell, avec Visual Studio 2022 C++ et son composant CMake :

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe --language fr
```

Le script prépare l'environnement Visual Studio x64, configure Ninja, compile, exécute les tests, installe le SDK
et rassemble l'exécutable et ses notices dans `out/ChatLab-windows-x64/`.
Il ne télécharge ni moteur IA ni dépendance GPU.

Pour une compilation CMake directe, dans un environnement C++ configuré :

```powershell
cmake -S . -B build-chat -DCYMCP_BUILD_CHAT_APP=ON -DCYMCP_CYUI_SOURCE_DIR=../CyUI
cmake --build build-chat --config Release --parallel 4
ctest --test-dir build-chat -C Release --output-on-failure
```

L'application démarre sans connexion. Sélectionner **Demo / Markdown (no AI)** puis
**Connect / New chat** et envoyer un message permet de vérifier le rendu sans compte.
Cette démonstration produit du texte déterministe ; elle ne mesure pas un modèle.
Les 22 profils du registre sont aussi accessibles, avec leurs exigences réelles de
CLI installé, de compte, d'API ou de service local. Les modèles ne sont pas inventés :
le catalogue provient du fournisseur connecté, et un ID peut être saisi manuellement.
Le modèle choisi s'applique à la prochaine connexion.

Les CLIs installés via npm peuvent être des wrappers `.cmd`/`.ps1`. Sélectionner
leur exécutable natif dans **Executable**, ou `node.exe` avec le script dans le
tableau d'arguments. Le SDK lance des arguments structurés, jamais un shell.
`--config PATH.json` charge les exemples JSON du SDK ; la connexion reste manuelle.
La clé API vient d'une variable d'environnement ou du champ masqué, en mémoire.

**Allow all actions** active explicitement le mode permissif du connecteur. Sans
cette option, les politiques de refus existantes restent applicables. Le panneau
réutilisable accepte les politiques fournies par l'hôte dans `ProviderConfig` ;
il ne remplace pas leur décision. L'exemple n'implémente pas de service ACP de
fichiers/terminal et ne contourne pas les restrictions imposées par un fournisseur.

**Save history** rend la sauvegarde automatique après chaque tour, même interrompu.
Choisir le même dossier et le même ID puis **Restore saved chat** reprend l'historique
API ou la session native exacte. Aucun tour n'est renvoyé automatiquement. La
sauvegarde reste privée, atomique, facultative, et exclut les clés/configurations.
Une erreur de sauvegarde est affichée séparément de la réussite de la génération.

## Intégration dans une autre application

Avec le SDK et CyUI comme sources :

```cmake
set(CYMCP_BUILD_CHAT_UI ON CACHE BOOL "" FORCE)
set(CYMCP_CYUI_SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/CyUI" CACHE PATH "" FORCE)
set(CYMCP_BUILD_CHAT_APP OFF CACHE BOOL "" FORCE)
add_subdirectory(external/CyMCPSDK)
target_link_libraries(my_app PRIVATE CyMCP::ChatUI)
```

Si l'application fournit déjà `CyUI::DocumentViews`, le SDK réutilise cette cible
et son ImGui, sans en créer un second. Sinon, il compile uniquement DocumentViews
et ses dépendances Core/Code/Images/Canvas. Audio, Gallery, Web et CEF sont exclus.
Pour l'exemple Windows avec un ImGui fourni par l'hôte, configurer aussi
`CYMCP_IMGUI_BACKEND_DIR` vers les backends Win32/DX11 correspondants.

Avec une installation CMake du SDK compilée avec l'interface :

```cmake
# Ajouter d'abord CyUI::DocumentViews, avec l'ImGui utilisé par l'application.
find_package(CyMCPSDK 0.5 CONFIG REQUIRED COMPONENTS ChatUI)
target_link_libraries(my_app PRIVATE CyMCP::ChatUI)
```

Le composant ChatUI est exporté séparément. Un consommateur du SDK sans ce
composant ne charge aucune dépendance CyUI.

```cpp
#include <cymcp/chat_ui.hpp>
#include <cyui/Core.h>

// Après ImGui::CreateContext(), avec le backend de l'application :
cyui::apply();
cyui::loadFonts();
cymcp::ChatController controller;
cymcp::ChatPanel panel(controller);

cymcp::ChatConnection connection;
connection.provider_id = "ollama";
connection.model = "MODEL_ID_INSTALLED_IN_OLLAMA";
controller.open(std::move(connection)); // programme le worker, retourne immédiatement

// Chaque frame, dans une fenêtre ImGui entre NewFrame et Render :
panel.draw(); // poll non bloquant, événements, Markdown et compositeur
```

L'hôte possède le contexte ImGui, le backend graphique, les textures, la boucle
d'événements et les services de ressources. Détruire le panneau et le contrôleur
avant leur contexte et avant le déchargement des fournisseurs. Un contrôleur
possède un worker ; son destructeur annule puis attend sa fermeture. Les
fournisseurs personnalisés doivent respecter l'annulation.

Le contrôleur et son `state()` s'utilisent depuis un seul thread hôte. Il sérialise
connexion, génération, catalogue, sauvegarde, reprise et déconnexion. Le rendu ne
lance aucun appel réseau ni lecture de pièce jointe ; ces opérations passent sur
le worker. `cancel()` signale l'annulation sans attendre.

Le panneau propose Markdown, code coloré, tableaux, sélection/copie, suivi du
défilement, saisie multiligne, pièces jointes, arrêt, reconnexion, sauvegarde,
activité et réflexion lorsqu'elles sont émises. Le résultat final remplace les
deltas sans les dupliquer. Une erreur conserve le texte partiel et le marque
interrompu. Les durées viennent des événements réellement reçus.

`ChatPanelOptions.pick_attachments` fournit le sélecteur de fichiers de l'hôte.
`document_services` fournit les ressources natives déjà mises en cache ; ses
callbacks restent non bloquants et sous la politique de l'application. Sans
`open_link`, un clic affiche le lien à copier. Le module n'ouvre aucun navigateur,
ne lance aucun script Markdown et ne télécharge aucune image distante.

## Limites et validation

La file UI est bornée à 2 048 événements et 4 MiB ; les payloads natifs privés
n'y sont pas copiés. Le compositeur accepte 64 KiB et au plus 16 pièces jointes.
Les lectures utilisent les budgets existants du SDK et refusent les liens de
fichiers. L'affichage est limité à 2 MiB par réponse, 8 MiB et 2 000 messages.
Une limite provoque une erreur explicite et une annulation, sans
historique silencieusement tronqué ni nouvelle génération automatique.

L'exemple est Win32/DX11, avec fallback WARP. Le module réutilisable contient
uniquement des appels CyUI/ImGui portables ; ses builds Linux/macOS restent à
valider avec les backends d'une application hôte. Android reste reporté.
Les animations/images et médias nécessitent les services de l'hôte. L'exemple
affiche les pièces jointes par leur nom ; il ne contient pas de pipeline média.

Les tests couvrent le worker, le streaming avant terminaison, la réconciliation
du texte final, l'annulation silencieuse, les erreurs, les pièces jointes, la
persistance/reprise et le rendu du panneau dans les cinq thèmes et deux langues.
`chat_app_smoke` lance une démonstration native cachée et capture un PNG. Aucun
test démo ne valide une authentification ou une génération IA réelle.
