---
id: licenses
title: Licences et composants
description: Comprendre la licence MIT du SDK et les conditions des composants optionnels.
section: Guides
parent: overview
order: 5
sourceRevision: 3
---

## CyMCPSDK {#sdk}

Copyright © 2026 Cyberalien. Les sources CyMCPSDK sont distribuées sous licence MIT.
Conserver copyright et autorisation dans les copies redistribuées. `LICENSE` et
`THIRD_PARTY_NOTICES.md` à la racine des sources contiennent les conditions et notices des composants.

## Composants {#components}

| Composant | Licence | Périmètre |
| --- | --- | --- |
| nlohmann/json 3.12.0 | MIT | Représentation JSON et validation du SDK |
| cpp-httplib 0.66.0 | MIT | Serveur HTTP local et transports de test |
| PicoSHA2 | MIT | Vérification des artefacts |
| libcurl de l'hôte | Licence curl et conditions TLS de l'hôte | HTTP(S) Windows/Linux/macOS |
| CyUI, Dear ImGui, MD4C | MIT | Rendu du tchat natif optionnel |
| SDL3 | zlib | Exemple Linux en fenêtre uniquement |
| stb image/image_write | Option MIT | Décodage des images et captures PNG de l’exemple Linux |
| Lexers Lexilla/Scintilla | Leurs notices permissives | Coloration du code optionnelle via CyUI |
| JetBrains Mono | SIL Open Font License 1.1 | Police embarquée par CyUI |
| Lecteur CyDoc | MIT | Ce lecteur de documentation statique |
| marked, PrismJS, esbuild | MIT | Outils de génération de documentation uniquement |

Les paquets de l’application contiennent les notices complètes de l’UI optionnelle dans `licenses/`.
Ce manuel inclut les textes MIT du SDK et de CyDoc dans `DocData/`.
Les moteurs d'inférence, modèles et runtimes GPU ne prennent pas la licence MIT du SDK :
les composants installés séparément et les services fournisseurs gardent leurs propres conditions.
