---
id: quickstart
title: Démarrer Chat Lab
description: Compiler et ouvrir l'exemple Windows, puis essayer la démonstration Markdown locale.
section: Guides
parent: overview
order: 1
sourceRevision: 3
---

## Compiler l'exemple {#build}

Utiliser Windows 10/11 x64 avec la charge **Développement Desktop en C++** de Visual
Studio 2022, le Windows SDK et les outils CMake. Placer une copie compatible de
[CyUI](https://github.com/MrMybal/CyUI) à côté du SDK, ou passer son emplacement réel au script.

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe
```

Le script compile et prépare `out/ChatLab-windows-x64`. Conserver ensemble l'exécutable,
`Documentation`, `licenses`, `LICENSE` et `THIRD_PARTY_NOTICES.md`.
Le runtime Microsoft Visual C++ 2015–2022 x64 est nécessaire. Le rendu utilise DX11,
avec repli WARP.

## Essayer sans compte {#demo}

1. Lancer l'application. **L'anglais** est la langue par défaut.
2. Ouvrir **Providers**, choisir **Demo / Markdown (no AI)** puis **Connect / New chat**.
3. Saisir un message et choisir **Send**, ou appuyer sur **Ctrl+Entrée**.
4. Lire le Markdown pendant son affichage progressif, sélectionner du texte ou copier un message.

```image
{"id":"chat-overview","src":"assets/chat-lab-fr.png","alt":"Chat Lab en français avec sa démonstration Markdown locale","controls":"minimal"}
```

La démonstration produit un texte local fixe ; elle n'appelle aucun modèle. Choisir un
[fournisseur](doc:providers) pour une vraie conversation. **Entrée** ajoute une ligne dans la saisie.

## Langue et aide {#language}

Dans **Fournisseurs → Apparence**, sélectionner **Français** ou **English**, puis un thème CyUI.
Reconnecter la démonstration après un changement de langue pour produire le nouveau texte
dans cette langue. Les réponses IA suivent le modèle et la consigne ; changer la langue
de l'interface ne traduit pas les messages existants.

**Aide (F1)** ouvre ce manuel sur [Chat Lab](doc:chat-lab). Le dossier local complet doit
accompagner l'exécutable. Le lecteur n'a pas besoin de serveur HTTP.
