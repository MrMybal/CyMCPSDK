---
id: chat-lab
title: Commandes de Chat Lab
description: Connecter un fournisseur, envoyer des messages et exposer les commandes de démonstration.
section: Utiliser Chat Lab
order: 10
sourceRevision: 14
previous: quickstart#demo
next: providers#configure
---

## Se connecter {#connect}

Le tchat s’ouvre directement. La roue **Paramètres** donne accès aux onglets **Fournisseurs**,
**MCP local**, **Journal** et **Aide**. **Fermer** revient à la conversation et à son brouillon. **F1** ouvre l’aide.
Dans Fournisseurs, rechercher le catalogue, sélectionner une carte et modifier ses propres champs.
Les réglages des autres fournisseurs restent en mémoire pour cette exécution. **Connecter / Nouveau tchat**
ouvre la connexion et revient au tchat ; un double-clic sur une carte connecte directement.

Sous Windows, lancer `cymcp_chat_app.exe`. Sous Linux, lancer `./cymcp_chat_app` depuis
un bureau X11 ou Wayland. Les réglages des fournisseurs, les commandes MCP local et le journal
sont identiques. Si le pilote graphique d’une machine virtuelle ne permet pas l’affichage, lancer
`SDL_RENDER_DRIVER=software ./cymcp_chat_app`. Garder les dossiers complets `Documentation/`,
`configs/` et `licenses/` avec l’exécutable.

```image
{"id":"provider-settings","src":"assets/providers-fr.png","alt":"Catalogue filtrable et paramètres de connexion et de modèle pour OpenAI API","controls":"minimal"}
```

La **Connexion automatique**, dans Fournisseurs, est activée au démarrage. Chat Lab détecte les CLI natifs,
les installations npm de l'utilisateur, les services Ollama/LM Studio/compatibles démarrés
à leurs adresses locales connues et les clés API des variables d'environnement. Il prépare
la première connexion disponible sans envoyer de message. La démonstration initiale ne bloque pas ce choix.

Les CLI conservent leur compte et leur modèle par défaut. Les services locaux peuvent choisir
un modèle texte du catalogue réel. Les API distantes affichent leur catalogue et attendent
votre choix de modèle. La liste propose les identifiants retournés ; un changement prend effet
au prochain clic sur **Connecter**. La disponibilité dépend toujours du compte et du fournisseur.

**Relancer la détection** recommence la recherche. Les cartes distinguent installation détectée,
connexion active et configuration nécessaire. Le panneau du fournisseur sélectionné explique
les installations, identifiants, adresses ou modèles locaux manquants. La recherche
n'installe aucun fournisseur et ne connecte aucun compte. Les clés restent en mémoire.
Une API distante n'est contactée qu'à la sélection de sa connexion.

Si un fournisseur natif demande une authentification, **Se connecter** ouvre son propre parcours
de connexion dans une console. Terminer cette connexion puis reconnecter ici. Cette action utilise
l'installation détectée.

Les champs manuels restent disponibles dans Fournisseurs avec le mode automatique activé.
**Actualiser le catalogue** récupère les vrais modèles. Les modifications s'appliquent
à la prochaine action **Connecter / Nouveau tchat**.
Choisir un fournisseur préremplit son adresse conventionnelle, sa variable de clé et ses
paramètres, ainsi que l'installation détectée.

```image
{"id":"automatic-connection","src":"assets/chat-auto-fr.png","alt":"Capture historique de l’interface après détection et connexion automatiques, sans message envoyé","controls":"minimal"}
```

**Installation et connexion** contient l'exécutable et le dossier de travail ou les champs API.
Déplier **Lancement avancé** pour les arguments JSON et les paramètres particuliers.
Utiliser le véritable exécutable natif, ou `node` accompagné des arguments de son script ;
les wrappers shell ne sont pas lancés. **Paramètres avancés (JSON)** reçoit les réglages du
fournisseur. **Consigne système**, dans **Conversation et historique**, fournit les instructions de session.
Consulter les [fournisseurs](doc:providers#configure).

Sous Linux, la connexion du fournisseur ouvre `x-terminal-emulator` ou `xterm`. Les sélecteurs
de fichiers utilisent un portail de bureau ou `zenity` ; l’ouverture du manuel et des liens
nécessite un navigateur associé via `xdg-open`.

## Envoyer et arrêter {#messages}

Écrire dans la zone de saisie. **Entrée** envoie ; **Maj+Entrée** ajoute une ligne. **Ctrl+Entrée** envoie également.
**Joindre des fichiers** sélectionne les pièces jointes locales. Le texte arrive dans le
transcript Markdown ; l'activité et la réflexion apparaissent lorsque le fournisseur les émet.
**Suivre les réponses** règle le défilement automatique.

La saisie grandit avec le brouillon. Sélectionner du texte pour la barre de formatage, ou utiliser
Ctrl+B/I/U/E. Le brouillon reste modifiable pendant une réponse ; un envoi pendant ce temps le conserve.
Dans le menu **…** du tchat, activer l’aperçu Markdown du brouillon. **Ctrl+F** recherche
dans les messages. La flèche **↑** dans une saisie vide reprend le dernier message utilisateur.
Pour les conversations gérées par le SDK, on peut aussi modifier un message envoyé ou supprimer
son tour après confirmation. Ces actions attendent un nouvel envoi explicite.
Survoler un message ou faire un clic droit pour **Copier le message** ou **Répondre** : répondre insère
une citation dans le brouillon et attend l’envoi. L’historique natif reste sous le contrôle du fournisseur.

Les pièces jointes ont leurs propres cartes. Les images se chargent en arrière-plan et s’agrandissent
dans l’app. Cliquer sur un texte pour son aperçu, ou **Enregistrer la pièce jointe** pour choisir une
destination pour les octets embarqués. Pour inclure une image dans un document Markdown,
joindre les deux fichiers au même message, puis utiliser le nom unique de l’image, par exemple
`![Résultat](./graphe.png)`. Les définitions de référence fonctionnent aussi.
Collez une image du presse-papiers ou les fichiers copiés dans le gestionnaire de fichiers avec **Ctrl+V**.
Vérifiez la vignette et retirez au besoin la pièce jointe avant l’envoi.
Les aperçus ne chargent pas les URL d’images arbitraires du Markdown. Un format sans décodeur conserve
sa carte et son action d’enregistrement.

Pendant la génération, **Arrêter** apparaît à côté d’**Envoyer**. Une annulation conserve la réponse partielle.
Si le fournisseur refuse le tour, la réponse affiche son explication, le code d’erreur du SDK
et le code du fournisseur lorsqu’il est disponible. Le texte déjà reçu reste visible.
Le **Journal** conserve le même diagnostic après masquage des secrets. Vous pouvez ensuite
changer de modèle et envoyer un nouveau message.
**Reconnecter** reprend explicitement la session actuelle, sans renvoyer le message interrompu.
Consulter les [conversations et l'historique](doc:conversations).

## Lire le journal {#logs}

Ouvrir **Paramètres → Journal**. **JSONL brut** affiche les mêmes événements que le fichier.
La molette permet de lire les entrées précédentes pendant que le journal reçoit de nouvelles lignes.
Les lignes indiquent l'heure locale,
le fournisseur, l'opération, le code d'événement ou d'erreur, l'explication et la durée. La connexion native
précise l'initialisation du protocole, le contrôle du compte et la création de session. La génération
indique le début de la réponse et sa fin.

Utiliser le filtre ou **Erreurs uniquement**, **Suivre le journal** et **Copier le journal** pour les diagnostics.
**Ouvrir le fichier journal** ouvre le fichier privé de session dans `%LOCALAPPDATA%/CyMCPSDK/ChatLab/logs`.
L'écriture se fait en arrière-plan. Survoler le nom du fichier pour son chemin complet. **Vider la vue**
efface les lignes affichées et conserve le fichier. Messages, identifiants et clés API sont exclus.

La vue garde les 2 000 dernières lignes. Chaque fichier est limité à 8 Mio ; une notification visible
signale sa saturation ou un problème d'écriture. Le journal n'annule jamais une conversation.

```image
{"id":"connection-journal","src":"assets/logs-fr.png","alt":"Journal de Chat Lab affichant le refus de modèle reproduit avec un simulateur de Codex","controls":"minimal"}
```

## Permettre à une IA d'utiliser les commandes {#commands}

Dans Fournisseurs, déplier **Droits et outils de l’app**. La démonstration expose `get_counter` et `set_counter`,
pour des entiers entre −1 000 000 et 1 000 000. L’onglet **MCP local** affiche la valeur actuelle.

1. Dans **MCP local**, choisir **Free** ou **Jeton seul** pour autoriser les écritures.
2. Dans **Fournisseurs**, activer **L'IA peut appeler les commandes de l'app**, puis créer une connexion Codex ou API compatible.
3. Demander à l'IA de modifier le compteur de démonstration, puis de le relire.

L'attachement direct prend en charge Codex, Responses, Messages, Chat Completions/OpenRouter et Ollama.
Les autres CLI utilisent leur configuration MCP native et renvoient `Unsupported` pour cette option.

## Connecter une autre application {#external}

Le serveur MCP local activé publie son nom, sa version et l’exigence de jeton dans
`/.well-known/cymcp` pour les hubs locaux, y compris dans les profils protégés. Le hub trouve
toujours le port d’écoute ; l’annonce ne contient ni jeton ni catalogue d’outils.
Consulter la [découverte MCP locale](doc:operations#discovery).

Dans **Paramètres → MCP local**, choisir le **Profil d’accès MCP**, puis **Activer le MCP local**.
En l’absence de configuration `app_commands`, Chat Lab sélectionne **Free** au démarrage.

| Profil | Valeur de configuration | Connexion | Opérations sur le compteur |
| --- | --- | --- | --- |
| Free | `free` | Adresse seule | Lecture et écriture |
| Local contrôlé | `local_controlled` | Adresse seule | Lecture seule |
| Jeton seul | `token_only` | Adresse et jeton | Lecture et écriture |
| Protégé | `protected` | Adresse et jeton | Lecture seule |

Utiliser **Copier l’adresse MCP** pour configurer le client et **Copier le jeton MCP** pour les
profils qui l’exigent. Le serveur écoute sur `127.0.0.1`. Avec un profil sans jeton, tout
programme sur cet ordinateur peut appeler les opérations autorisées. Arrêter le MCP local
avant de modifier l’exigence de jeton. Les droits des opérations peuvent changer à chaud.

Utiliser `/mcp` pour les outils MCP ou `/v1` sur le même port pour le SDK JSON direct. Les deux
routes et le chat intégré partagent la politique des commandes métier. Les droits natifs du
fournisseur se configurent séparément.

Pour retrouver la même adresse à chaque démarrage, choisir `mcp_port` dans la configuration
passée à `--config`. La valeur par défaut `0` sélectionne un port libre. Cette configuration
démarre le MCP local avec le profil Free sur le port 8765.

```json
{"provider":"demo","app_commands":{"mcp":true,"access_profile":"free","mcp_port":8765}}
```

Dans un fichier de configuration, `access_profile` est prioritaire sur les anciennes options
`mcp_without_token` et `allow_changes`. Une section `app_commands` sans profil garde ses
réglages et ses valeurs par défaut précédentes.

Pour appeler une opération autorisée directement sur `/v1`, envoyer la requête SDK JSON suivante.

```json
{"method":"invoke","name":"get_counter","arguments":{}}
```

Arrêter le MCP local ferme l’adresse. En mode protégé, le démarrage suivant génère un nouveau
jeton. Consulter les [opérations](doc:operations) et les [droits](doc:permissions).
