---
id: troubleshooting
title: Résoudre un problème de connexion
description: Comprendre les erreurs fournisseur, les interruptions et l'absence du manuel local.
section: Aide
order: 30
sourceRevision: 4
previous: conversations#resume
next:
---

## Erreurs de connexion {#connection}

Partir de l'action en échec : détecter une installation, ouvrir une session, charger les modèles
ou envoyer un message. Ouvrir **Paramètres → Journal**, activer **Erreurs uniquement**, puis lire
l'explication du fournisseur et le code d'erreur. **Copier le journal** ou **Ouvrir le fichier journal**
permet de récupérer les diagnostics ; voir les [commandes du journal](doc:chat-lab#logs).
Une session connectée ne prouve pas à elle seule que le compte peut générer une réponse.

| Symptôme | Vérification et correction | Reconnaître la réussite |
| --- | --- | --- |
| `ExecutableMissing` ou CLI non détecté | Vérifier que l'exécutable natif fonctionne dans le compte voulu. Dans **Fournisseurs**, choisir **Relancer la détection**, ou renseigner son chemin réel. | Le fournisseur est détecté et **Connecter / Nouveau tchat** ouvre une session. |
| `AuthenticationRequired` | Pour un CLI natif, choisir **Se connecter**, terminer sa connexion puis reconnecter ici. Pour une API, fournir la clé par variable d'environnement ou champ masqué en mémoire. | L'initialisation du compte réussit ; un message court reçoit une réponse. |
| `ProviderError` après connexion | Lire le code natif dans le tour en échec ou le Journal. Vérifier l'ID du modèle et les droits du compte, choisir un modèle disponible puis reconnecter. | Un nouveau message court, envoyé volontairement, se termine sans cette erreur. |
| Catalogue `Unsupported` | Saisir dans les réglages manuels un ID exact fourni par le fournisseur. Certains transports n'ont pas de catalogue de modèles. | Le modèle choisi accepte un message. |
| Adresse locale injoignable | Démarrer le service, vérifier son port et choisir le bon format API. Ollama utilise son URL native ; les API compatibles finissent souvent par `/v1`. | Le catalogue contient les modèles installés et le modèle choisi répond. |
| `UnsupportedContent` | Retirer la pièce jointe refusée ou choisir un modèle/transport acceptant son type. | Un message au contenu compatible se termine. |

Le compte CLI d'un processus lancé peut différer de l'environnement d'un autre terminal.
Pour les variables d'environnement propres à l'enfant, voir la [configuration des fournisseurs](doc:providers#configure).
Un changement de connexion ne renvoie jamais automatiquement un message en échec.

## Conversations interrompues {#interrupted}

| Symptôme | Correction | Reconnaître la réussite |
| --- | --- | --- |
| `Cancelled` après **Arrêter** | Conserver la réponse partielle. Décider d'envoyer ou non un nouveau message. | La conversation est au repos et son brouillon reste modifiable. |
| `UnexpectedEOF` ou flux tronqué | Conserver le texte partiel, utiliser **Reconnecter**, puis choisir le message suivant. | La connexion est prête ; reconnecter ne renvoie pas le message interrompu. |
| `SessionInvalid` | Garder l'historique enregistré. Vérifier que le fournisseur choisi peut charger la session native exacte ; créer explicitement une nouvelle conversation si nécessaire. | La session voulue reprend, ou le nouveau tchat créé volontairement est identifié comme tel. |
| `ContextTooLarge` | Réduire les pièces jointes ou commencer une nouvelle conversation. L'erreur peut venir de la garde mémoire de 256 Mio par message ou de la fenêtre de contexte du modèle. | Une nouvelle demande plus courte se termine. |

Un long historique peut faire sortir ses plus anciens messages de la vue ; une interface lente
ou réduite ne fait pas à elle seule échouer un tour. Pour enregistrer et restaurer sans rejouer,
consulter les [conversations et l'historique](doc:conversations#history).

## Manuel absent {#help}

**F1 ouvre le panneau Aide.** Choisir **Ouvrir le manuel complet** pour afficher le lecteur installé.
Si `DocumentationMissing` apparaît, restaurer le dossier `Documentation/` entier à côté de
`cymcp_chat_app.exe` (ou de l'exécutable Linux), depuis le même paquet applicatif.

Ouvrir directement `Documentation/Documentation.html`. Garder `DocData/`, ses scripts et ses
ressources à leurs emplacements relatifs d'origine. L'affichage du code HTML sur GitHub n'est
pas le lecteur ; télécharger d'abord le dossier complet. La lecture ne demande ni Node.js ni serveur web.

**Vérifier la correction :** la présentation s'ouvre, le changement de langue conserve la page
et la [capture de démonstration](doc:quickstart#demo) s'affiche. Si les signets ou notes disparaissent,
vérifier la politique de stockage des fichiers locaux du navigateur et exporter les données via
**Mon espace** avant de changer de profil de navigateur ou d'effacer son stockage.
