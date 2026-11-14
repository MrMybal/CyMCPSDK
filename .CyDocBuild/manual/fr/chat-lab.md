---
id: chat-lab
title: Commandes de Chat Lab
description: Connecter un fournisseur, envoyer des messages et exposer les commandes de démonstration.
section: Guides
parent: overview
order: 2
sourceRevision: 2
---

## Se connecter {#connect}

La **Connexion automatique** est activée au démarrage. Chat Lab détecte les CLI natifs,
les installations npm de l'utilisateur, les services Ollama/LM Studio/compatibles démarrés
à leurs adresses locales connues et les clés API des variables d'environnement. Il ouvre
la première connexion disponible sans envoyer de message. Cliquer sur une autre connexion
disponible pour l'utiliser directement.

Les CLI conservent leur compte et leur modèle par défaut. Les connexions API choisissent
un modèle texte du catalogue réel. La liste de modèles propose les identifiants retournés ;
un changement prend effet au prochain clic sur **Connecter**. La disponibilité dépend
toujours du compte et du fournisseur.

**Relancer la détection** recommence la recherche. **Autres fournisseurs / configuration**
explique les installations, identifiants, adresses ou modèles locaux manquants. La recherche
n'installe aucun fournisseur et ne connecte aucun compte. Les clés restent en mémoire.
Une API distante n'est contactée qu'à la sélection de sa connexion.

Désactiver **Connexion automatique** pour ouvrir le formulaire manuel facultatif.
**Actualiser le catalogue** récupère les vrais modèles. Les modifications s'appliquent
à la prochaine action **Connecter / Nouveau tchat**.

```image
{"id":"automatic-connection","src":"assets/chat-auto-fr.png","alt":"Chat Lab en français après détection et connexion automatiques, sans message envoyé","controls":"minimal"}
```

Déplier **Exécutable** pour le chemin du CLI natif, le tableau JSON d'arguments et le dossier
de travail. Utiliser le vrai `.exe`, ou `node.exe` accompagné des arguments de son script ;
les wrappers shell ne sont pas lancés. **Paramètres avancés (JSON)** reçoit les réglages du
fournisseur. **Consigne système** fournit les instructions à la création de session.
Consulter les [fournisseurs](doc:providers#configure).

## Envoyer et arrêter {#messages}

Écrire dans la zone de saisie. **Ctrl+Entrée** envoie ; **Entrée** ajoute une ligne.
**Joindre des fichiers** sélectionne les pièces jointes locales. Le texte arrive dans le
transcript Markdown ; l'activité et la réflexion apparaissent lorsque le fournisseur les émet.
**Suivre les réponses** règle le défilement automatique.

**Arrêter** demande l'annulation. La réponse partielle reste marquée interrompue.
**Reconnecter** reprend explicitement la session actuelle, sans renvoyer le message interrompu.
Consulter les [conversations et l'historique](doc:conversations).

## Permettre à une IA d'utiliser les commandes {#commands}

Déplier **Commandes de l'application**. La démonstration expose `get_counter` et `set_counter`,
pour des entiers entre −1 000 000 et 1 000 000. Le panneau affiche la valeur actuelle.

1. Activer **Autoriser la modification du compteur** pour permettre les écritures.
2. Activer **L'IA peut appeler les commandes de l'app**, puis créer une connexion Codex ou API compatible.
3. Demander à l'IA de modifier le compteur de démonstration, puis de le relire.

L'attachement direct prend en charge Codex, Responses, Messages, Chat Completions/OpenRouter et Ollama.
Les autres CLI utilisent leur configuration MCP native et renvoient `Unsupported` pour cette option.

## Connecter une autre application {#external}

**Activer le MCP local** crée un serveur loopback authentifié sur un port libre.
Copier l'adresse affichée et utiliser **Copier le jeton MCP** pour configurer le client.
Utiliser `/mcp` pour les outils MCP ou `/v1` sur le même port pour le SDK JSON direct.

```json
{"method":"invoke","name":"get_counter","arguments":{}}
```

Les deux routes partagent le compteur et la politique d'écriture active. Interdire les écritures
ne déconnecte pas le client. Désactiver le serveur ferme l'adresse ; le réactiver crée un nouveau
jeton. Consulter les [opérations](doc:operations) et les [droits](doc:permissions).
