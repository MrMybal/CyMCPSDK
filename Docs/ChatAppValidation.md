# Validation de Chat Lab 0.5.1 dans les deux sens

Essais du **14 novembre 2026**, Windows x64, Release MSVC 19.40, Win32/DX11 et CyUI fourni
depuis une copie locale de CyUI. Les scénarios tournent dans la boucle graphique réelle,
avec un rendu pendant les appels. Une fenêtre cachée capture son framebuffer DX11.

## Parcours vérifiés

| Parcours | Preuve |
|---|---|
| App → IA → texte dans le tchat | Générations réelles Codex, Antigravity et Ollama ; deltas affichés avant la fin |
| App → IA → commande de l'app → résultat IA | Codex réel appelle `set_counter(42)` puis `get_counter`, résultat et valeur vérifiés |
| Client externe → MCP de l'app | Client C++ réel, découverte des deux outils, lecture, écriture et refus selon droits |
| Client externe → SDK JSON de l'app | HTTP `/v1`, `describe`/`invoke`, même registre et mêmes droits ; changements visibles par MCP |
| Formats API → commandes et retours d'outils | Cinq serveurs de transport simulés, exécutés contre l'application réelle |
| Annulation, erreur et reprise | Flux tronqué, HTTP 401, réponse silencieuse annulée, reconnexion exacte et historique restauré |

Le domaine de test est un compteur entier borné à ±1 000 000. Aucun test d'outil ne donne
accès au système de fichiers ou au terminal. Les écritures sont refusées initialement,
puis permises explicitement. Le MCP reste authentifié et refuse les requêtes avec `Origin`.

## Suite automatisée sans compte

`tools/build-chat.ps1` compile puis exécute **8 suites CTest, 500 contrôles**, dont
**59 contrôles bidirectionnels** dans dix instances de l'application et **44 contrôles ChatUI**.
Les autres contrôles couvrent le cœur (26), l'intégration (50), les fournisseurs (98),
les extensions (67) et les conversations (156). Le smoke test graphique complète ces contrôles.

Les cinq formats sont Chat Completions compatible OpenAI, OpenAI Responses, Anthropic Messages,
OpenRouter et Ollama NDJSON. Le scénario Codex utilise un sous-processus simulant son protocole natif.
Chaque format API reçoit exactement deux requêtes pour un aller-retour d'outil, puis une requête
pour le message suivant : la reconnexion ne relance aucune génération.

Ces fixtures prouvent le comportement des transports, des outils et de l'interface ; elles ne font
pas d'inférence et ne valident pas l'authentification d'un service cloud. Le rapport agrégé est généré
dans `build-chat-native/chat-app-validation/summary.json`, avec une capture par scénario.

## Générations réellement effectuées

| Fournisseur installé | Résultat dans l'application | Mesures ponctuelles |
|---|---|---|
| Codex CLI 0.155.0, `gpt-6-luna`, effort `low` | Deux tours réussis ; une écriture du compteur, deux lectures ; même session après reconnexion | Premier texte 5,34 s puis 5,82 s ; tours 5,54 s et 6,02 s |
| Antigravity 1.5.9, modèle natif non annoncé | Trois tours réussis ; code mémorisé au deuxième tour puis après reconnexion, même ID natif | Premier texte 7,16 s, 8,39 s et 8,54 s ; tours 7,27 s, 8,50 s et 8,64 s |
| Ollama 0.40.5, `qwen3:0.6b` déjà installé | Catalogue puis réponse locale non vide, affichée pendant la génération | Premier texte 6,01 s ; tour 6,02 s, service fraîchement démarré |
| Claude Code 2.1.276 | Connexion native établie ; premier tour renvoie `AuthenticationRequired` | Aucune génération réussie revendiquée |

Le premier texte peut arriver après plusieurs appels d'outils ou une phase de réflexion. Ces nombres
ne sont ni un benchmark de débit, ni une comparaison des modèles, ni une garantie de latence.
Le rendu reste sur le thread UI, les appels passent sur le worker ; les rapports enregistrent aussi
le nombre de frames et le maximum ponctuel du temps CPU d'une frame, sans prétendre mesurer les FPS.

Le modèle Codex laissé par défaut dans le contexte initial, `gpt-6.1-sol`, était rejeté par le
CLI avec `model_not_found` pour ce compte ChatGPT. Un essai distinct a sélectionné explicitement
`gpt-6-luna`, présent dans le catalogue natif, sans modifier la configuration globale. Aucun repli de modèle
ni renvoi automatique d'un tour échoué n'a été ajouté au SDK.

Les processus CLI ont repris le profil Windows existant ; `HOME` a été passé explicitement à leur
environnement enfant. Aucun login, changement de compte ni écriture de réglages d'authentification
n'a été effectué. Le refus Claude décrit le contexte de ce processus de test, pas l'état d'un autre terminal.
Le test local a démarré un service Ollama temporaire sur un port loopback libre, puis arrêté son propre
arbre de processus. Aucun modèle, moteur, runtime CUDA ou pilote n'a été téléchargé ou installé.

Les rapports privés sources sont sous `build-temp/app-live/`. Les copies de résultats partageables sont
rassemblées dans `out/ChatLab-windows-x64/validation/` après validation. Aucun jeton, ID natif exact ou
texte de réponse n'est inclus dans les rapports JSON. Les captures de ces essais contiennent seulement
les consignes de démonstration et les marqueurs de réponse.

## Défauts trouvés et corrigés

- **Streaming HTTP Windows** : `WinHttpReadData` avec un grand buffer attendait davantage de données
  sur les petits flux. En présence d'un consommateur streaming, le SDK interroge maintenant les octets
  disponibles puis les lit immédiatement. Le test exige du texte visible pendant que le tour est encore actif.
  Ce comportement suit la [documentation Microsoft de WinHttpQueryDataAvailable](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpquerydataavailable).
- **Réflexion remplacée** : le texte effacé entre deux tours occupait encore le budget d'affichage.
  Sa taille est maintenant déduite ; cinq tours de réflexion volumineuse vérifient cette régression.
- **Tours interrompus** : les durées mesurées sont conservées dans l'état graphique après échec ou annulation.
- **Compilation incrémentale MSVC localisée** : un préfixe UTF-8 mal décodé empêchait Ninja de suivre
  les headers. Le script répare le préfixe et reconstruit les objets une seule fois si nécessaire.

Le pilote graphique a aussi été corrigé pour comparer les réponses courtes avec un retour de ligne facultatif,
et attendre qu'un CLI ait réellement annoncé son ID natif avant de vérifier la reconnexion.

## Périmètre restant

Les autres CLI du catalogue et les API cloud avec de vraies clés restent à tester dans leurs environnements.
Les pièces jointes et le rendu multimodal sont couverts par les tests de protocole et du panneau, sans preuve
de génération multimodale réelle pour chaque fournisseur. Le module de supervision local possède ses tests
propres ; ce scénario Ollama ne valide pas tous les installateurs ni llama.cpp. Les builds Linux/macOS et
la validation matérielle de leurs interfaces restent à exécuter. Ces essais ne remplacent pas un audit de sécurité.
