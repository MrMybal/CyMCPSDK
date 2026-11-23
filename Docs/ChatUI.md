# Tchat natif CyUI

Le module optionnel `CyMCP::ChatUI` apporte un contrôleur asynchrone et un panneau de
conversation natif C++20. Il utilise `CyUI::DocumentViews` : aucun navigateur, CEF,
Python ou serveur web n'est nécessaire. Les modules du SDK et son agrégat
`CyMCP::SDK` restent indépendants de l'interface graphique.

Le panneau emploie maintenant `cyui::messageComposer` et `TranscriptSource` de CyUI.
La saisie grandit jusqu’à 180 unités logiques, conserve le brouillon pendant un tour actif et
propose le formatage natif sur la sélection. Entrée ou Ctrl+Entrée envoie ; Maj+Entrée ajoute
une ligne. `set_draft`, `draft` et `add_attachments` permettent l’intégration avec l’hôte.
Seules les lignes visibles sont adaptées au rendu ; un delta ne recopie pas toute la conversation.

Les pièces jointes sont des `ChatAttachment` à ID stable, nom, MIME, taille et référence opaque.
Leur `content` partagé immuable contient le bloc portable embarqué, lu sur le worker et reconstruit
à la restauration. Il ne contient aucun chemin local. Les noms d’images restaurées sans nom dans
le format portable utilisent un libellé générique. `open_attachment(attachment, save)` reçoit une
activation explicite ; `document_services` fournit les textures déjà chargées sans bloquer le rendu.
L’exemple Windows décode les images en arrière-plan, borne ses vignettes à 8 textures de 1024 pixels,
prévisualise les textes et permet de choisir où enregistrer les octets embarqués.
Un aperçu Markdown peut afficher une image jointe au même message. Par exemple,
`![Résultat][figure]` peut désigner une définition `[figure]: ./resultat.png` quand une seule
pièce jointe de ce message porte ce nom. Le décodage utilise les octets embarqués.
Ctrl+V dans le compositeur accepte les images du presse-papiers Windows et les fichiers copiés
dans l’Explorateur. Les vignettes des pièces en attente permettent de les vérifier avant l’envoi.

Le transcript et le volet de réflexion utilisent les mêmes services Markdown de l’hôte.
`ChatPanelOptions::open_link` reçoit les activations de liens. Pour afficher des cartes
d’aperçu déjà disponibles, fournir `cyui::DocumentServices::preview` et activer
`ChatPanelOptions::link_previews`. Le callback s’exécute sur le thread d’affichage et
doit renvoyer immédiatement les données en cache. Compiler ChatUI et l’application
contre la même révision de CyUI.

Les mesures comparatives couvrent séparément le transport MCP et les trames Markdown sur
deux révisions de CyUI. [Comparaison avec CyToolsCore](Comparaison-CyToolsCore.md)

CyUI gère la copie native. Répondre cite le message dans le brouillon, sans envoyer automatiquement.
Les actions de modification et suppression des messages utilisateur apparaissent lorsque l’hôte
fournit **les deux** callbacks `edit_message` et `delete_message` ; l’éditeur natif applique alors
sa proposition via le callback. L’hôte assure la compatibilité avec son historique fournisseur.
Dans Chat Lab, les conversations dont l’historique est géré par le SDK (démo et API) permettent
de modifier le texte d’un message envoyé ou de supprimer un tour après confirmation.
La flèche ↑ dans un brouillon vide reprend le dernier message utilisateur. Pour une session native
comme Codex, elle en place une copie dans le brouillon. Le hook `paste` facultatif de l’hôte
importe du texte ou des chemins sélectionnés ; le collage texte natif fonctionne sans ce hook.

Le tchat occupe la fenêtre de Chat Lab. La roue **Paramètres** ouvre **Fournisseurs**, **Journal**
et **Aide**. **Fermer** revient au brouillon. Le catalogue de fournisseurs
est filtrable ; ses cartes sélectionnent un éditeur de paramètres et un double-clic connecte.
Les champs de chaque fournisseur sont conservés séparément en mémoire pendant l'exécution.
La démo initiale est un état de départ hors ligne, sans priorité sur une IA détectée en mode automatique.

`ChatViewState.diagnostics` contient au plus 1 000 événements avec leur identifiant, horodatage,
niveau, fournisseur, opération, code SDK, explication et durée. En cas de refus du fournisseur,
le tour concerné et le journal affichent aussi son code natif s’il est disponible. Le détail provient
du champ d’erreur du fournisseur. Les valeurs d’identification configurées et les formes courantes
de secrets sont masquées avant affichage et journalisation. Les événements `ConnectionStage`
conservent uniquement les étapes connues.

Une réponse déjà commencée reste lisible avec le motif de l’échec. Une annulation est marquée
« Interrompu ». La restauration et la reconnexion réaffichent le diagnostic enregistré et
attendent un nouvel envoi explicite. `ChatMessage::error_code`, `provider_code` et
`error_message` décrivent l’échec indépendamment du texte de la réponse.
`choosing_model` distingue le choix du catalogue d'une panne lors d'une connexion API automatique.

L'exemple rassemble ces événements avec ceux de la détection dans **Logs**, puis écrit un fichier
privé par exécution sous `%LOCALAPPDATA%/CyMCPSDK/ChatLab/logs` sur un worker distinct.
`--log-file PATH.log` choisit un emplacement explicite. La vue garde 2 000 lignes ; le fichier est
borné à 8 Mio et signale les erreurs ou sa saturation sans annuler de génération. **Clear view**
conserve le fichier. **JSONL brut** affiche les enregistrements tels qu’ils sont écrits.
**Copier le journal** copie les lignes filtrées au format affiché. La molette permet de remonter
le journal pendant la réception de nouveaux événements.

## Application Windows de test

Depuis PowerShell, avec Visual Studio 2022 C++ et son composant CMake :

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe
```

Le script prépare l'environnement Visual Studio x64, configure Ninja, compile, exécute les tests, installe le SDK
et rassemble l'exécutable et ses notices dans `out/ChatLab-windows-x64/`.
Il ne télécharge ni moteur IA ni dépendance GPU.
L'anglais est la langue de démarrage. Choisir Français dans Appearance pour changer
la langue de l'interface, ou utiliser explicitement `--language fr`. La démonstration
locale prend aussi la langue choisie à la connexion. **F1** ouvre l’onglet d’aide.
**Ouvrir le manuel complet** ouvre le manuel local fourni dans `Documentation/`.

Pour une compilation CMake directe, dans un environnement C++ configuré :

```powershell
cmake -S . -B build-chat -DCYMCP_BUILD_CHAT_APP=ON -DCYMCP_CYUI_SOURCE_DIR=../CyUI
cmake --build build-chat --config Release --parallel 4
ctest --test-dir build-chat -C Release --output-on-failure
```

L'application démarre en **Automatic connection** : une recherche en arrière-plan
détecte les CLI natifs et npm installés, les clés API d'environnement et les services
locaux démarrés. La première connexion disponible s'ouvre sans envoyer de message.
Choisir une autre connexion dans la liste pour changer de fournisseur. Les CLI gardent
leurs modèles natifs ; les services locaux choisissent un modèle texte du catalogue réel.
Les API distantes affichent leur catalogue et attendent un choix explicite de modèle.
L’onglet **Providers** indique les prérequis manquants, sans lire les secrets
des fichiers privés. Une erreur ne déclenche pas de changement silencieux de fournisseur.

Dans Providers, sélectionner **Demo / Markdown (no AI)** puis
**Connect / New chat** et envoyer un message permet de vérifier le rendu sans compte.
Cette démonstration produit du texte déterministe ; elle ne mesure pas un modèle.
Les 23 profils du registre sont aussi accessibles, avec leurs exigences réelles de
CLI installé, de compte, d'API ou de service local. Les modèles ne sont pas inventés :
le catalogue provient du fournisseur connecté, et un ID peut être saisi manuellement.
Le modèle choisi s'applique à la prochaine connexion.

Les CLIs installés via npm peuvent être des wrappers `.cmd`/`.ps1`. Le SDK résout
automatiquement le binaire natif ou le point d'entrée Node du paquet installé.
Pour une installation inhabituelle, sélectionner
leur exécutable natif dans **Executable**, ou `node.exe` avec le script dans le
tableau d'arguments. Le SDK lance des arguments structurés, jamais un shell.
`--config PATH.json` charge les exemples JSON du SDK ; la connexion reste manuelle.
La clé API vient d'une variable d'environnement ou du champ masqué, en mémoire.
Le tableau `process.arguments` et la map `process.environment` sont transmis au processus enfant.
Ils permettent de reproduire un contexte de lancement sans modifier les réglages globaux du CLI.

La section **App commands** expose deux opérations de démonstration : `get_counter` et `set_counter`.
Le compteur apparaît dans l'interface. **Allow counter changes** autorise son écriture, et peut être
modifié pendant une connexion. **AI may use app commands** attache le registre à la prochaine connexion
Codex ou API compatible outils (Responses, Messages, Chat Completions/OpenRouter, Ollama).
Les autres CLI conservent leurs propres mécanismes MCP : cette case retourne `Unsupported` pour eux.
**Expose local MCP** démarre un serveur sur `127.0.0.1`, avec port et jeton aléatoires ; **Copy MCP token**
permet de configurer un client externe. Le même serveur expose les commandes SDK JSON sur `/v1`.
Ces opérations ne donnent accès à aucun fichier, terminal ou commande système.

Configuration facultative :

```json
"app_commands": {"attach_to_ai": true, "allow_changes": true, "mcp": true}
```

Chaque option est désactivée par défaut. Le serveur local exige le Bearer token, même lorsque les
écritures du compteur sont autorisées ; l'absence de blocage d'une opération reste une décision de l'hôte.
Dans une autre application, l'hôte fournit son propre registre et sa propre politique.

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

Utiliser CyUI `14e1c6f` ou une révision ultérieure pour intégrer les corrections de sélection
et de retour à la ligne du tchat. Le SDK et l’application hôte doivent être compilés avec
la même révision de CyUI, car le correctif modifie la disposition d’états internes au rendu.

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
find_package(CyMCPSDK 0.7.3 CONFIG REQUIRED COMPONENTS ChatUI)
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
deltas sans les dupliquer. Après un refus, le tour affiche **Échec**, le texte partiel
et l’explication du fournisseur. **Interrompu** indique une annulation.

`ChatPanelOptions.pick_attachments` fournit le sélecteur de fichiers de l'hôte.
`document_services` fournit les ressources natives déjà mises en cache ; ses
callbacks restent non bloquants et sous la politique de l'application. Sans
`open_link`, un clic affiche le lien à copier. Le module n'ouvre aucun navigateur,
ne lance aucun script Markdown et ne télécharge aucune image distante.

## Limites et validation

La file UI fusionne les deltas successifs : une interface qui ne lit plus (fenêtre réduite)
ne fait jamais échouer une génération ; les payloads natifs privés n'y sont pas copiés.
Le compositeur s'agrandit avec le texte et accepte jusqu'à 256 pièces jointes.
Les lectures refusent les liens de fichiers. L'affichage est une fenêtre : au-delà de 16 MiB
une réponse est affichée tronquée, et au-delà de 64 MiB ou 4 000 messages les plus anciens
quittent la vue. La conversation elle-même n'est jamais coupée ni annulée par ces valeurs.
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
`chat_app_bidirectional` lance dix instances graphiques contre des transports simulés et des clients MCP/SDK réels.
Les [essais supplémentaires avec de vraies générations](ChatAppValidation.md) sont documentés séparément.

Pour piloter un essai réel reproductible :

```powershell
Start-Process ./out/ChatLab-windows-x64/cymcp_chat_app.exe -WindowStyle Hidden -Wait -ArgumentList @(
  '--hidden', '--config', 'config.json', '--exercise', 'plan.json',
  '--report', 'report.json', '--capture', 'screen.png')
```

Le plan contient `steps`, une liste d'actions `connect`, `send`, `models`, `reconnect`, `disconnect`,
`save`, `rights` ou `wait`. Exemple minimal, à adapter au fournisseur configuré :

```json
{"steps":[
  {"action":"connect"},
  {"action":"send","text":"Reply exactly TEST_OK.","reply":"TEST_OK","trim_reply":true},
  {"action":"reconnect","same_session":true}
]}
```

Les assertions facultatives vérifient `reply`/`contains`, `error`, `counter`, `messages`,
`streaming` et `same_session`. `cancel_after_ms` annule un tour et `timeout_ms` borne chaque action.
Le test s'arrête au premier échec, sauf `continue_after_failure:true` dans le plan.
Le code de sortie est nul uniquement si le scénario se termine avec succès.
Les rapports contiennent l'état, les durées et les compteurs, sans texte des messages, secret ou ID natif.
Les plans, captures et historiques peuvent en revanche contenir des messages : les garder privés si nécessaire.
`--test-ready-stdout` est réservé au pilote de test : il transmet le jeton sur le pipe privé du processus parent.
