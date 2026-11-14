---
id: troubleshooting
title: Résoudre un problème de connexion
description: Comprendre les erreurs fournisseur, les interruptions et l'absence du manuel local.
section: Guides
parent: overview
order: 4
sourceRevision: 1
---

## Erreurs de connexion {#connection}

| Erreur ou symptôme | Action |
| --- | --- |
| `ExecutableMissing` | Sélectionner le CLI natif et vérifier son exécution dans le compte/environnement voulu. |
| `AuthenticationRequired` | Vérifier l'authentification dans l'outil natif, ou fournir l'identifiant API par variable/champ en mémoire. |
| `ProviderError` après connexion | Vérifier l'ID exact du modèle, les droits du compte et l'erreur native hors de l'app. |
| Catalogue `Unsupported` | Saisir un vrai ID de modèle ; certains transports n'exposent pas la découverte. |
| Adresse locale injoignable | Démarrer le service et vérifier URL de base, port et format API. |
| `UnsupportedContent` | Choisir un modèle/transport compatible avec cette pièce jointe, ou la retirer. |

Le compte CLI d'un processus lancé peut différer de l'environnement d'un autre terminal.
Utiliser `process.environment` si l'hôte a besoin de valeurs propres à l'enfant. Une connexion
réussie ne garantit pas que le compte peut effectuer une génération.

## Conversations interrompues {#interrupted}

`Cancelled` indique une interruption, pas une réponse finale réussie. Un flux tronqué peut produire
`UnexpectedEOF`. Conserver le texte partiel, reconnecter explicitement, puis choisir le message suivant.
`SessionInvalid` indique que la session exacte n'a pas pu être reprise ; garder son historique enregistré.

`Backpressure` ou `ContextTooLarge` indique un budget de file, d'affichage ou de contenu atteint.
Sauvegarder puis créer une conversation, ou réduire les pièces jointes. ChatUI borne ses événements
et son texte affiché ; il ne supprime pas silencieusement une conversation pour respecter son budget.

## Manuel absent {#help}

Conserver `Documentation/Documentation.html` et le dossier complet `Documentation/DocData` à côté
de Chat Lab. F1 ouvre cette copie installée, sans repli en ligne. Si le dossier manque, le restaurer
depuis le paquet applicatif ou reconstruire le paquet. Ouvrir directement le HTML pour vérifier ses
scripts et images locaux. Le lecteur ne demande ni Node.js ni serveur web.
