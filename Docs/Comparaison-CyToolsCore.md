# CyMCPSDK, CyToolsCore et le tchat Markdown

Comparaison du 18 novembre 2026 sur les copies de travail locales. CyToolsCore annonce la version 0.10.1 (base Git `d531590`), CyMCPSDK la version 0.5.6 en développement (base `642c368`) et CyUI la version 1.2.0 (révision `6ef01cf`). La comparaison inclut les corrections locales de CyMCPSDK.

## Ce qu’une application peut faire

| Besoin | CyToolsCore Python | CyMCPSDK C++ |
| --- | --- | --- |
| Exposer et appeler des opérations | Le protocole CyTools JSON 1.0 expose la description, les ressources, la soumission de jobs et leur suivi. Le serveur MCP publie les opérations du Tool et ses commandes de contrôle. | Un registre natif C++20 valide les entrées et sorties et applique la politique de l’hôte. Les mêmes opérations sont disponibles en JSON et via le serveur MCP. Un client MCP embarqué peut appeler un autre processus ou serveur HTTP. |
| Reprendre un travail après un redémarrage | Le runtime conserve les jobs en SQLite, isole les espaces de travail et gère les réservations de ressources. Un client peut demander l’état et les sorties d’un job. | L’appel d’outil renvoie directement son résultat. L’hôte peut déléguer un travail durable au runtime Python par MCP. Les checkpoints de conversation du SDK restaurent l’historique ou la session native d’un agent. |
| Valider les données et installer les dépendances | Le validateur utilise JSON Schema Draft 2020-12. Le téléchargeur sait reprendre un transfert partiel compatible HTTP Range. | Le registre accepte un sous-ensemble explicite de JSON Schema et rejette les mots-clés non pris en charge. Le gestionnaire vérifie tailles et empreintes, avec un paquet privé NVIDIA CUDART prêt à installer. La reprise HTTP Range reste un écart. |
| Connecter un assistant | Le compagnon CyUEMCPSDK fournit les connecteurs. Son registre déclare huit transports implémentés répartis sur sept profils, dont les deux transports Codex. | Le registre natif intègre 23 profils CLI, API ou locaux, dont plusieurs partagent une même implémentation ACP ou HTTP. Une application peut utiliser le service de conversation indépendamment de l’interface. |
| Afficher une conversation Markdown | Le cœur Python est indépendant de l’interface. Les interfaces des Tools et des compagnons ont leur propre intégration. | Le composant facultatif ChatUI fournit un transcript CyUI virtualisé, un compositeur, un volet de réflexion et un affichage mis à jour à chaque delta de texte. |

Les deux API JSON applicatives ont des contrats différents. Un client Python qui soumet `SubmitJob` attend un identifiant puis utilise `GetJob`. Le point d’entrée JSON de CyMCPSDK expose `describe` et `invoke`. Un remplacement direct du premier par le second demande donc un adaptateur. MCP permet dès aujourd’hui d’utiliser ces deux runtimes ensemble. [Contrat Python](../../CyToolsCore/cytools_core/schemas/request.schema.json) · [Architecture Python](../../CyToolsCore/Docs/Architecture.md) · [API C++](../src/core.cpp) · [Connecteurs](Providers.md)

## Interopérabilité vérifiée

Le client C++ a lancé le véritable `cytool_test` Python dans un répertoire temporaire, découvert ses outils, soumis un écho synthétique, attendu le statut `Completed` avec `cy_get_job` et vérifié le texte renvoyé. Il a aussi contrôlé le rejet d’un paramètre invalide et la fin de la tâche. Chaque exécution utilise des données privées de test. [Test de développement](../tests/mcp_transport_tests.cpp)

Cet essai a révélé que la destruction du client C++ terminait le processus Python avant sa sortie normale. Le client ferme maintenant l’entrée standard et draine les sorties pendant une seconde au maximum, avant de terminer un processus qui persiste. Un marqueur `atexit` du véritable interpréteur Python a confirmé la sortie normale après correction. Pour une session MCP HTTP historique négociée, le client transmet également une requête `DELETE` à la fermeture. [Client MCP](../src/mcp.cpp)

## Mesures du transport MCP

Windows x64, build C++ Release, Python 3.11.5. Le même client officiel MCP 1.30.0 a négocié la révision `2025-11-25` sur stdio avec chaque serveur. Six paires ont alterné l’ordre des serveurs. Chaque serveur disposait d’un nouveau processus et d’un dossier temporaire à chaque paire.

| Mesure | CyToolsCore | CyMCPSDK | Échantillon par SDK |
| --- | ---: | ---: | --- |
| Du lancement du processus à la réponse d’initialisation, médiane | 580,03 ms | 17,14 ms | 6 |
| Ping sur connexion établie, médiane | 0,581 ms | 0,105 ms | 192 |
| Ping sur connexion établie, 95ᵉ centile | 0,881 ms | 0,181 ms | 192 |
| Écho jusqu’au texte vérifié, médiane | 35,00 ms | 0,622 ms | 36 |

Le ping exécute la même opération de protocole des deux côtés. Pour l’écho, le chemin Python inclut un job enregistré, son exécution par un worker et la récupération de ses sorties. Le chemin C++ appelle directement le handler. Un rapport JSON contient les mesures individuelles, l’ordre de passage, les versions et les résultats agrégés. [Banc reproductible](../benchmarks/compare_cytools_mcp.py)

Depuis la racine de CyMCPSDK, avec l’environnement de développement du dépôt Python voisin et le build Windows complet :

```powershell
../CyToolsCore/.venv/Scripts/python.exe benchmarks/compare_cytools_mcp.py --core ../CyToolsCore --cpp build/cytoolscore-comparison/cymcp_example.exe
./build/cytoolscore-comparison/cymcp_mcp_transport_tests.exe --cytoolscore ../CyToolsCore/.venv/Scripts/python.exe
```

## Effet de CyUI sur le tchat

La virtualisation du transcript et le compositeur Markdown étaient déjà intégrés à Chat Lab avec CyUI `1bdba05`. La révision `6ef01cf` ajoute notamment le suivi des positions des destinations de liens et d’images dans le texte source et corrige l’éditeur Markdown. La comparaison a mis en évidence des services de rendu que Chat Lab et ChatUI pouvaient mieux utiliser.

- Le volet de réflexion bénéficie du même traitement des images et des clics sur les liens que les réponses. L’hôte peut activer les cartes de liens avec son propre cache d’aperçus.
- L’aperçu d’un fichier Markdown joint peut afficher une image jointe au même message, y compris par définition de référence. Un nom ambigu ou un chemin vers un autre dossier laisse le visuel indisponible.
- Les tests du panneau vérifient la présence des textures dans les commandes de dessin ImGui pendant le streaming et après la fermeture d’un bloc de code. Un lien activé dans le volet de réflexion atteint le callback de l’hôte. [Tests du panneau](../tests/chat_ui_tests.cpp) · [Intégration dans Chat Lab](../examples/chat_app/attachments.hpp)

Le même scénario graphique a été rejoué avec l’ancien puis le nouvel exécutable de Chat Lab. Avec l’onglet de conversation masqué pour exclure le chargement par les vignettes du transcript, l’ancien aperçu échoue avec `ImagePreviewFailed`. Le nouvel aperçu charge et décode la texture de l’image référencée. Le test contrôle aussi les noms en double et les références vers un autre message ou dossier. [Scénario Windows](../tests/chat_app_tests.cpp)

Un essai distinct, avec Ollama et le modèle `qwen3:0.6b` déjà chargé, a mesuré 16,12 ms en médiane entre l’envoi et la première trame DX11 contenant le texte. Cette mesure comprend l’inférence locale, la réception du texte et l’attente d’une trame. [Méthode et résultats Ollama](ChatAppValidation.md)

### Mesure isolée du rendu Markdown

Deux builds MSVC Release ont exécuté le même code avec les révisions CyUI `350ad87` et `6ef01cf`. Le transcript contient 32 messages. La réponse Markdown de 1 944 octets arrive par fragments de 64 octets. La vue ImGui fait 1 100 × 900 pixels. Huit paires ont alterné l’ordre des versions. Le tableau présente la médiane des médianes des huit processus pour chaque version. Le chronomètre de trame couvre la préparation des commandes de dessin sur le CPU, jusqu’à `ImGui::Render`. [Source du banc](../benchmarks/markdown/main.cpp)

| Durée CPU | Avant, `350ad87` | Après, `6ef01cf` |
| --- | ---: | ---: |
| Analyse du texte après ajout d’un fragment | 120,95 µs | 128,15 µs |
| Trame qui reçoit un fragment | 160,80 µs | 171,95 µs |
| Trame sans modification du texte | 54,50 µs | 55,45 µs |

Les deux builds produisent 2 268 sommets sur la dernière trame. Les durées observées se recoupent entre processus. Sur ce scénario, la mise à jour apporte des capacités de traitement Markdown, sans accélération mesurable. Le GPU, la synchronisation écran, le réseau et l’inférence sont hors du chronométrage.

Pour reproduire le test, configurer séparément chaque révision avec `cmake -S benchmarks/markdown -B <build> -DCYMCP_CYUI_SOURCE_DIR=<sources-CyUI>`, compiler en Release puis exécuter `markdown_latency` dans les deux builds.

## Validation du build livré

Sous Windows x64, les 12 suites CTest passent. Dix exécutables totalisent 770 contrôles, dont 66 pour ChatUI et 162 pour les parcours bidirectionnels de l’application. S’y ajoutent les démarrages graphiques en anglais et en français. Un programme externe utilisant le composant ChatUI installé a également été recompilé et exécuté. L’exécutable du paquet portable a été ouvert en français et sa capture DX11 vérifiée.

Sous Alpine Linux x86_64 avec GCC 15.2, les suites ciblées passent également, avec 39 contrôles du transport MCP et 186 contrôles de conversation. Les mesures comparatives et les essais de rendu de Chat Lab ont été effectués sous Windows.
