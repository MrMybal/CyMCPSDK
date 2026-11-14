# Validation 0.3.0

Validation locale le **14 novembre 2026**, Windows x64, Visual Studio 2022 / MSVC 19.40,
CMake 3.31 et Ninja, configuration Release. Il s'agit d'une première base native de développement.
Une compilation réussie et des fixtures ne constituent pas un audit de sécurité indépendant ni une validation fournisseur.

## Exécuté localement

- Compilation de tous les modules et des exemples natifs d'application et de conversation.
- Suite CTest : validation des schémas, droits, annulation, workers, JSON borné et clés dupliquées.
  Dernière exécution : **26 contrôles du cœur, 50 d'intégration, 90 de fournisseurs et 67 d'extensions**, tous réussis.
- Sous-processus réels : arguments sans shell, processus silencieux, blocage sur stdin, dépassement de ligne/file.
- Serveur HTTP réel sur loopback : authentification, rejet d'Origin, propagation de l'annulation à la fermeture du client.
- MCP HTTP réel entre client et serveur C++ : découverte, invocation, erreurs et délégation d'une app vers une autre.
- Client MCP officiel Python **1.30.0** contre le serveur **C++** sur stdio : initialisation, catalogue,
  résultat de `sum(17,25)=42`, et erreur sur paramètres invalides. Python sert uniquement à ce test optionnel.
- Fixtures Codex : connexion, catalogue, session, deltas, outils dynamiques, interruption et réutilisation du processus.
- Fixtures Claude : streaming, session/reprise, échec après texte partiel et code de sortie non nul après un résultat.
- Fixture HTTP de modèle : deltas, assemblage d'arguments d'outils et détection de flux tronqué.
- Fixtures CLI OpenCode 1.x/Antigravity : catalogue, prompt Unicode sans shell, sessions/reprise exacte,
  permissions par défaut et mode permissif, échec après texte partiel, crash, EOF tronqué,
  annulation pendant un tour silencieux et fermeture du connecteur. OpenCode 2 est refusé explicitement.
- Fixtures API OpenAI Responses, Claude Messages, Ollama et OpenRouter : en-têtes d'authentification,
  pagination Anthropic et curseurs répétés, corps natifs, streaming, appels d'outils fragmentés et résultats,
  noms d'opérations traduits, politique hôte, budget de tours, refus HTTP 401/429, EOF et erreur après texte partiel.
  Les items de raisonnement Responses sont conservés au tour suivant. Une réponse HTTP silencieuse peut expirer.
- Fixtures Command Code/CodeBuddy : stdin UTF-8 sans shell, sessions/reprise exacte, événements et snapshots
  répétés, résultat final/usage, mode permissif explicite, refus, limite de tours, JSON invalide, crash après résultat,
  EOF incomplet, annulation et fermeture pendant un tour silencieux.
- Fixture ACP dans un vrai processus : initialisation et version, session/load et replay ignoré, deux tours
  avec le processus persistant, sélection confirmée modèle/mode, catalogue négocié ou commande JSON native,
  requêtes de permissions acceptées/refusées selon la politique hôte et préférence pour une autorisation ponctuelle,
  annulation pendant une demande d'autorisation et rejet préalable des demandes d'une autre session,
  requêtes de fichiers refusées, événements d'outils/usage, session étrangère ignorée, refus et limites natifs,
  EOF/erreurs après du texte partiel, version/ID RPC invalides, authentification requise, timeout et annulation.
- Registre : quinze profils créés, extension externe exécutée, doublons/IDs inconnus/factories nulles refusés,
  catalogue sans clé, séparation des secrets, validation des paramètres et factory réentrante hors verrou.
- Profils compatibles llama.cpp/LM Studio : dialogue avec un endpoint simulé configuré explicitement.
- Fixture de moteur local dans un vrai processus : démarrage, secret privé, disponibilité authentifiée,
  dialogue HTTP et retrait du fichier d'authentification à l'arrêt.
- Installation réelle de petits fichiers de test depuis HTTP loopback (autorisation réservée aux tests) :
  SHA-256, cache, altération, nettoyage d'échec, chemins interdits et refus par politique hôte.
- Compilation minimale sans réseau, MCP, processus natifs, IA ni moteur local ; tests du cœur.
- Installation CMake, compilation et exécution d'un projet consommateur séparé avec `find_package`.

Les exécutables des fixtures ne font pas d'inférence. Aucun modèle, CUDA ou pilote n'a été téléchargé/installé.
Les comptes fournisseurs n'ont pas été utilisés pour générer une réponse. Les interfaces ont été confrontées
à la documentation officielle et aux schémas du Codex CLI installé **0.155.0** ; Claude installé : **2.1.276**.
Antigravity **1.5.9** répond réellement à `--version` et expose les flags utilisés dans son aide ; sa commande
`models` retourne un échec dans ce contexte. Le connecteur C++ remonte cet échec sans annoncer une authentification.
Le paquet local OpenCode porte la version **1.18.30**, mais son exécutable échoue sur son répertoire de configuration
avant de pouvoir se connecter. Aucun contournement ni modification des configurations natives n'a été effectué.
Les quatre nouveaux transports API sont validés sur serveurs HTTP simulés, sans requête de génération vers le cloud.
Command Code (`command-code`/`cmdc`), CodeBuddy (`codebuddy`/`cbc`) et Devin ne sont pas dans le PATH de
cette session. Les échanges C++ correspondants sont testés avec des exécutables simulés ; aucune génération
réelle ni compatibilité avec une version installée de ces trois CLI n'est revendiquée. Le protocole ACP v1
et le sous-programme `devin acp` ont été confrontés à leur documentation officielle. Les adaptations de
Command Code/CodeBuddy ont aussi été confrontées aux sources de CyAIOrchestrator.

## Mesures initiales

Sur cette machine, avant toute comparaison matérielle ou optimisation spécifique :

- Exemple CLI/MCP/HTTP Release : environ **618 ko** (octets décimaux, hors runtime C++ système).
- Exemple de conversation avec catalogue/factories : environ **642 ko**, sans modèle ni runtime fournisseur.
- Microbenchmark : environ **5 à 10 microsecondes par appel** selon la charge de la machine sur 100 000 appels natifs avec politique et
  validation entrée/sortie. Ce test ne mesure ni réseau, ni interface, ni génération de tokens.
- Les archives statiques `.lib` de développement contiennent des instanciations de templates et des sections
  éliminées à l'édition de liens : leur somme n'est pas le surcoût de l'application distribuée.
  Les tailles du paquet et des bibliothèques sont enregistrées dans `out/validation-windows-x64.json`.

Mesurer séparément le premier token, les tours chauds, la mémoire du moteur et le SDK avant toute promesse
de vitesse équivalente à un client fournisseur. Aucune performance d'inférence CPU/GPU n'est encore mesurée.

## À valider avant une distribution de production

- **Linux** : adaptateurs POSIX/libcurl et workflow présents, mais aucun runtime Linux disponible dans cette
  session Windows pour exécuter la suite. Le workflow doit encore produire ses preuves.
- **macOS** : architecture et workflow préparés, compilation et exécution matérielles à réaliser.
- Générations réelles des CLI et API, authentification, catalogues, reprise et outils dans les versions effectivement distribuées.
- Vrai moteur llama.cpp, modèles compatibles, CPU/GPU, consommation RAM/VRAM et reprise après crash.
- HTTPS sur les plateformes cibles, proxys d'entreprise, certificats et scénarios réseau dégradés.
- Installateurs réels, élévation Windows/Linux, redémarrages et licences des paquets sélectionnés.

## Limites du périmètre livré

Pas d'interface, Android, réseau public, OAuth multi-utilisateur, coffre de secrets, historique durable,
jobs persistants, sandbox OS propre au SDK, téléchargement automatique de catalogues GPU,
extraction d'archives ou reprise HTTP Range. Le mode natif Claude persistant n'est pas implémenté.
Les CLI OpenCode, Antigravity, Command Code et CodeBuddy utilisent un processus par tour avec reprise native.
Le profil OpenCode intégré cible son CLI JSONL 1.x. ACP v1 dispose d'un adaptateur générique persistant et
d'un profil Devin. Les autres agents qui annoncent ACP ne sont pas automatiquement validés par ce profil générique.
ACP v2, la délégation d'un système de fichiers/terminal client à l'agent ACP, le transport historique Devin texte/ATIF
et les tâches CodeBuddy survivant au tour ne sont pas implémentés. Après annulation ACP, la connexion doit être recréée.
Les souscriptions MCP persistantes, ressources/prompts et notifications de progression MCP ne sont pas annoncées.
Le profil de schéma est documenté dans Integration.md ; les mots-clés inconnus ne sont pas ignorés.

La compatibilité source C++ est en développement. Utiliser des headers et bibliothèques de la même version,
avec un compilateur et une bibliothèque standard compatibles. Aucun ABI C++ stable entre compilateurs n'est promis.

## Rejouer

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCYMCP_BUILD_BENCHMARKS=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

`cymcp_benchmark` affiche ses mesures en JSON. Pour l'interopérabilité facultative :
installer `mcp==1.30.0` dans un environnement de développement séparé et lancer
`python tests/mcp_official.py CHEMIN_ABSOLU_VERS_cymcp_example`.
Le SDK et ses tests C++ n'ont aucune dépendance Python.
