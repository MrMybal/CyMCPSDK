---
id: quickstart
title: Démarrer Chat Lab
description: Ouvrir l’exemple Windows et envoyer un message de démonstration locale avant de choisir un fournisseur IA.
section: Commencer
order: 1
sourceRevision: 5
parent: overview
previous: overview#workflows
next: chat-lab#connect
---

## Ouvrir Chat Lab {#build}

**Objectif :** envoyer un message à la démonstration locale, puis connecter votre fournisseur.
La démonstration ne demande ni compte IA, ni modèle, ni connexion réseau.

Sous Windows 10/11 x64, extraire le [ZIP publié de Chat Lab 0.5.6](https://github.com/MrMybal/CyMCPSDK/releases/download/v0.5.6/CyMCPSDK-ChatLab-0.5.6-windows-x64.zip)
en entier et lancer `cymcp_chat_app.exe`. Cet exemple publié précède les sources actuelles
0.7.5 ; certaines commandes décrites ici demandent une compilation des sources.
Garder `Documentation/`, `configs/`, `licenses/`, `LICENSE` et `THIRD_PARTY_NOTICES.md`
à côté de l'exécutable. Installer le runtime Microsoft Visual C++ 2015–2022 x64 si nécessaire.
Le rendu utilise DX11, avec repli WARP.

Pour compiler l'exemple Windows actuel, installer la charge **Développement Desktop en C++**
de Visual Studio 2022, le Windows SDK, les outils CMake et les fichiers de développement
libcurl avec TLS. Obtenir une copie compatible de [CyUI](https://github.com/MrMybal/CyUI)
et passer son emplacement réel :

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe
```

Le script compile, teste et prépare `out/ChatLab-windows-x64`. Utiliser `-CurlRoot CHEMIN`
pour une installation libcurl personnalisée. Pour la compilation Linux et les prérequis SDL3,
consulter les [prérequis de compilation](doc:integration#build).

## Essayer sans compte {#demo}

Le tchat s'ouvre directement, en anglais. La détection automatique peut préparer un fournisseur
disponible sans envoyer de message. Choisir explicitement la démonstration pour ce tutoriel.

```steps
{"items":[{"title":"Choisir la démonstration locale","body":"Ouvrir **Settings → Providers**, choisir **Demo / Markdown (no AI)** puis **Connect / New chat**. En français : **Paramètres → Fournisseurs**, puis **Connecter / Nouveau tchat**.","result":"Le tchat s’ouvre avec la démonstration sélectionnée.","troubleshooting":"Revenir dans Fournisseurs et choisir explicitement Demo si la détection automatique a préparé une autre connexion."},{"title":"Envoyer un message court","body":"Saisir `Bonjour` puis appuyer sur **Entrée**, ou choisir **Envoyer**. **Ctrl+Entrée** envoie aussi ; **Maj+Entrée** insère une ligne.","result":"Une réponse Markdown locale fixe apparaît progressivement, avec du texte mis en forme et du code. Aucun modèle n’est appelé.","troubleshooting":"Vérifier que Demo est connecté. Pour une erreur d’un vrai fournisseur, suivre les [vérifications de connexion](doc:troubleshooting#connection)."},{"title":"Lire et copier la réponse","body":"Sélectionner du texte, ou survoler un message et choisir **Copier le message**. **Répondre** place une citation dans le brouillon.","result":"La citation reste dans le brouillon modifiable jusqu’à l’envoi. **Arrêter** interrompt une réponse encore en cours."}]}
```

```image
{"id":"chat-overview","src":"assets/chat-lab-fr.png","alt":"Chat Lab en français avec sa démonstration Markdown locale","controls":"minimal"}
```

Ensuite, [choisir un vrai fournisseur](doc:providers#configure) ou découvrir les
[commandes de Chat Lab](doc:chat-lab#connect). La capture de démonstration illustre
la réponse locale ; les réglages des fournisseurs figurent sur la page des commandes.

## Langue et aide {#language}

Dans **Paramètres → Fournisseurs → Apparence**, sélectionner **Français** ou **English**, puis un thème CyUI.
Reconnecter la démonstration après un changement de langue pour produire le nouveau texte
dans cette langue. Les réponses IA suivent le modèle et la consigne ; changer la langue
de l'interface ne traduit pas les messages existants.

**F1** ouvre le panneau Aide de Chat Lab. **Ouvrir le manuel complet** affiche ce lecteur
sur [Chat Lab](doc:chat-lab), dans la langue sélectionnée. Le dossier local complet doit
accompagner l'exécutable. La lecture ne demande ni Node.js ni serveur web.
