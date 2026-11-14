---
id: conversations
title: Conversations et historique
description: Joindre des fichiers, annuler un tour et reprendre exactement une conversation sans la rejouer.
section: Guides
parent: chat-lab
order: 2
sourceRevision: 1
---

## Pièces jointes {#attachments}

**Joindre des fichiers** capture les fichiers locaux sélectionnés sur le worker. Images, audio et
ressources texte/PDF sont acceptés selon le transport et le modèle. Le panneau affiche les noms ;
un aperçu multimédia enrichi nécessite les services de ressources fournis par l'application hôte.

La saisie accepte jusqu'à 16 fichiers et 64 Kio de texte. Des budgets de fichiers/contenus et la
validation MIME/base64 s'appliquent aussi. Un contenu incompatible est refusé explicitement.
Un modèle qui reçoit une image ne prend pas forcément en charge l'audio ou les documents.

## Conserver une conversation {#history}

1. Activer **Enregistrer l'historique** avant la connexion.
2. Choisir un dossier privé et un identifiant d'historique simple.
3. Envoyer les messages normalement ; chaque tour terminé ou interrompu est sauvegardé automatiquement.
4. Choisir les mêmes fournisseur/modèle, dossier et ID, puis **Reprendre le tchat enregistré**.

L'historique est optionnel. Il contient messages, résultats d'outils et pièces jointes intégrées : ce sont
des données utilisateur. Les clés et réglages de connexion n'y sont pas inclus. Une erreur de stockage
s'affiche séparément d'une génération réussie. Restaurer ne renvoie aucune consigne et ne répète aucune commande.

## Reprise et annulation {#resume}

Les fournisseurs natifs reprennent leur session exacte lorsqu'ils le permettent ; les API sans état
utilisent l'historique conservé par l'hôte. Si la session native ne peut pas être chargée, `SessionInvalid`
préserve l'historique au lieu de créer une conversation de remplacement. La reprise ACP dépend des
capacités négociées avec l'agent.

**Arrêter** annule l'opération active. **Reconnecter** rouvre explicitement la connexion si nécessaire.
Après interruption, choisir d'envoyer ou non un nouveau message ; aucune génération n'est retentée automatiquement.

## API C++ publique {#api}

`Conversation::checkpoint()` retourne un instantané restaurable. `ConversationStore::save(id, conversation)`
l'écrit de façon privée et atomique ; `load(id)` le lit pour `Conversation::restore(snapshot)`.
Le fournisseur/modèle doit correspondre. Garder les instances de fournisseur et l'executor en vie jusqu'à
la fin des opérations asynchrones. Les callbacks s'exécutent sur le worker appelant, pas sur le thread UI.
