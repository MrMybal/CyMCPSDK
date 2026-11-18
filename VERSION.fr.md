# Notes de version de CyMCPSDK

[English](VERSION.md) · [Manuel](Documentation/Documentation.html#/fr/overview)

## 0.5.6 — En développement

- Corriger les quinze écarts de l’audit de conformité et ajouter leurs tests de régression.
  Les douze suites Windows et les huit suites Linux passent. [Corrections et validations](Docs/Audit-0.5.6.md)
- Ajouter le profil `codex-exec` avec reprise et l’installation privée de NVIDIA CUDART 12.8.90 sur Windows et Linux x64.
- Utiliser la nouvelle saisie native CyUI : hauteur adaptative, formatage, Entrée/Maj+Entrée et brouillons conservés pendant la génération.
- Afficher les conversations via TranscriptSource et ses caches bornés, sans recopier tout le transcript à chaque delta.
- Afficher les cartes de pièces jointes, les aperçus d’images asynchrones et les fichiers texte ; enregistrer leur contenu embarqué vers une destination choisie.
- Relier copie/réponse. L’hôte peut activer modification et suppression avec des callbacks compatibles avec son fournisseur.

## 0.5.5

- Onglets Tchat, Fournisseurs et Journal. Le catalogue se recherche et présente des états explicites ;
  les paramètres ont leur éditeur et restent en mémoire lors d'un changement de fournisseur.
- Correction du démarrage automatique : la démo initiale ne bloque plus la sélection d'une IA
  installée. Une préférence explicite ne provoque pas de repli sur un autre fournisseur.
- Diagnostics horodatés pour la détection, le protocole natif, le compte, les sessions,
  les catalogues, le début de réponse, la fin, les erreurs et l'annulation.
- Le journal se filtre et se copie ; un fichier privé par session est écrit en arrière-plan.
  Les événements excluent le contenu des messages, les clés API et les identifiants natifs de session.
- Le choix du modèle API distant est présenté comme une étape de configuration avec son catalogue.

## 0.5.4

- Défauts pensés pour les agents, sans limites arbitraires : sortie au maximum du modèle,
  tours d'outils illimités, aucun délai de tour (CLI et API) ; `timeout_ms` détecte seulement
  une connexion silencieuse. Les tailles restantes sont des gardes mémoire de 256 Mio.
- Chaque profil publie ses paramètres conventionnels dans `describe()` (`defaults`) ;
  Chat Lab les préremplit en mode manuel avec l'installation détectée.
- Détection : Codex npm lancé par son binaire natif, sondages en parallèle, service local
  installé mais arrêté signalé, `OLLAMA_HOST` et `ANTHROPIC_BASE_URL` normalisés,
  llama.cpp sur le port 8080. Le modèle automatique est réservé aux services locaux.
- Corrections : appel d'outil interrompu complété dans l'historique ; une interface réduite ne
  fait plus échouer un tour ; ACP réinitialisé après toute erreur en cours de tour ; état
  « connecté » exact après un échec de restauration ; repli MCP HTTP vers le protocole legacy ;
  réponse tronquée OpenAI conservée ; secrets MCP/réglages hors de la ligne de commande ;
  message trop long pour la ligne de commande Windows signalé explicitement.
- Chat Lab propose **Se connecter** pour ouvrir le parcours de connexion natif du fournisseur dans une console.

## 0.5.3

- Chat Lab démarre en mode automatique : détection en arrière-plan des CLI natifs,
  installations npm prises en charge, clés API d'environnement et services de modèles locaux démarrés.
- Les connexions disponibles s'ouvrent sans saisir de chemin, d'arguments ni d'identifiant de modèle.
  Les fournisseurs natifs gardent leurs réglages ; les modèles texte API viennent du catalogue réel.
- Les installations, clés, adresses et modèles manquants ont des états de configuration distincts.
  La détection n'envoie aucun message, n'installe rien et ne lit pas les fichiers privés d'identifiants.
- La détection fait partie du SDK IA ; l'hôte peut ajouter ses recettes et emplacements.
  Les paramètres manuels restent disponibles.

## 0.5.2

- L'anglais reste la langue par défaut de l'application. La démonstration Markdown locale
  suit maintenant elle aussi la langue sélectionnée, anglais ou français.
- Chat Lab propose une action **Aide (F1)** qui ouvre le manuel local dans la langue sélectionnée.
- Un manuel CyDoc bilingue accompagne l'application et s'ouvre directement depuis le disque.
- Le README suit la même structure en anglais et en français, avec les modules, la compilation et les licences.

### Problèmes connus

- Le test du cœur compile avec GCC 15.2. Le stockage de conversation reconnaît les alias système
  des dossiers temporaires de macOS. La validation sur une machine macOS reste à effectuer.
  L'exemple graphique utilise Win32/DX11.
- La disponibilité d'un fournisseur dépend de la version du CLI/API, du compte et du modèle choisi.
  La présence d'un profil ne garantit pas l'authentification auprès de chaque service.

### Limites actuelles

- Le SDK n'inclut ni modèles, CLI fournisseurs, moteurs d'inférence, runtimes GPU ou pilotes.
- Les dépendances locales gèrent les membres vérifiés d’une archive et le paquet NVIDIA CUDART
  12.8.90 sous Windows/Linux x64. Les autres paquets GPU et la reprise HTTP Range restent à réaliser.
- L'attachement direct des opérations dans Chat Lab est disponible pour Codex et les transports API
  compatibles ; les autres CLI utilisent leurs réglages MCP natifs. Les services ACP fichiers/terminal
  nécessitent des callbacks de l'hôte.
- Les outils MCP passent par stdio ou HTTP JSON loopback authentifié. Les souscriptions persistantes,
  ressources/prompts et notifications de progression ne sont pas annoncés.
- Android est reporté. La compatibilité source C++ évolue ; aucun ABI entre compilateurs n'est garanti.

Signaler les problèmes reproductibles dans le [suivi des problèmes](https://github.com/MrMybal/CyMCPSDK/issues).
Aucune release binaire publique n'a été publiée pour cette version ; compiler depuis les sources.

## Versions précédentes

- **0.5.1 :** commandes bidirectionnelles de Chat Lab par MCP/SDK, correction du streaming HTTP Windows,
  du budget d'affichage de la réflexion et conservation des durées des tours échoués.
- **0.5.0 :** module de tchat CyUI optionnel et exemple Windows Chat Lab.
- **0.4.1 :** réglages natifs de Claude conservés et suppression du contrôle de login bloquant.
- **0.4.0 :** profils fournisseurs extensibles, conversations, pièces jointes, historique privé et reprise exacte.
- **0.3.0 :** socle natif modulaire du SDK.
