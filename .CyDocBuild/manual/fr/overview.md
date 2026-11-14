---
id: overview
title: CyMCPSDK
description: Ajouter conversations IA et outils applicatifs à une application C++ native.
section: Guides
order: 0
sourceRevision: 1
---

## Choisir un parcours {#workflows}

CyMCPSDK est une bibliothèque C++20 sous MIT pour les développeurs d'applications.
Enregistrer les opérations une fois, les exposer par SDK direct ou MCP, puis connecter
l'application à des agents CLI, des API ou un service de modèle local. Le panneau CyUI
optionnel affiche les conversations.

| Votre besoin | Point de départ |
| --- | --- |
| Essayer l'application Windows | [Démarrage](doc:quickstart) |
| Configurer un tchat et ses outils | [Chat Lab](doc:chat-lab) |
| Ajouter la bibliothèque à votre application | [Intégration C++](doc:integration) |
| Permettre à une IA ou une autre app d'appeler vos opérations | [Opérations applicatives](doc:operations) |
| Choisir les règles d'accès | [Droits](doc:permissions) |
| Utiliser un moteur local installé | [Moteurs locaux](doc:local-engines) |

## Modules {#modules}

| Cible CMake | Rôle |
| --- | --- |
| `CyMCP::Core` | Registre d'opérations, schémas, politiques, annulation et workers bornés |
| `CyMCP::Platform` | Processus enfants natifs et arguments structurés |
| `CyMCP::Http` | Client HTTP(S), streaming et serveur loopback authentifié |
| `CyMCP::MCP` | Serveur/client d'outils MCP par stdio ou HTTP JSON local |
| `CyMCP::AI` | Profils fournisseurs, sessions, conversations et échanges d'outils |
| `CyMCP::Local` | Supervision des moteurs et téléchargements vérifiés |
| `CyMCP::ChatUI` | Contrôleur CyUI et panneau de tchat Markdown optionnels |
| `CyMCP::SDK` | Modules du SDK sans dépendance graphique |

## Périmètre {#scope}

La bibliothèque n'embarque ni modèle, CLI IA, moteur d'inférence, runtime CUDA ou pilote.
Installer et configurer le fournisseur choisi. Ses comptes natifs et conditions restent
applicables. Consulter les [fournisseurs](doc:providers) et les [licences](doc:licenses).

L'exemple Windows utilise Win32/DX11. ChatUI utilise le backend CyUI/ImGui de l'application
hôte. La distribution Linux/macOS reste en cours de réalisation ; Android est reporté.

## Lire ce manuel {#navigation}

Utiliser l'arbre des pages ou le champ de recherche ; **Ctrl+K** ouvre la recherche.
Basculer entre anglais et français dans le lecteur. Le dossier `Documentation` complet
s'ouvre directement depuis le disque. Dans Chat Lab, **Aide (F1)** ouvre le guide du tchat
dans la langue sélectionnée dans l'application.
