---
id: overview
title: CyMCPSDK
description: Ajouter conversations IA et outils applicatifs à une application C++ native.
section: Commencer
order: 0
sourceRevision: 4
previous:
next: quickstart
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
| Charger les outils à la demande et rechercher du texte | [Optimisation et grep](doc:optimization) |
| Choisir les règles d'accès | [Droits](doc:permissions) |
| Utiliser un moteur local installé | [Moteurs locaux](doc:local-engines) |

```diagram
{"type":"architecture","title":"De l’application hôte vers l’IA et les outils externes","description":"L’hôte possède ses opérations et leurs droits. Le registre les expose aux clients SDK et MCP ; un fournisseur exécute les conversations et peut appeler les outils autorisés.","nodes":[{"id":"host","label":"Application hôte","x":20,"y":20,"width":190},{"id":"registry","label":"Registre + droits","x":20,"y":165,"width":190},{"id":"mcp","label":"Clients SDK / MCP","x":310,"y":165,"width":190},{"id":"provider","label":"Fournisseur IA + outils","x":310,"y":20,"width":190}],"edges":[{"from":"host","to":"registry"},{"from":"mcp","to":"registry"},{"from":"host","to":"provider"},{"from":"provider","to":"registry"}]}
```

Le tableau suivant associe ces rôles aux cibles CMake. Pour une première intégration,
suivre le parcours [compilation → opérations → droits](doc:integration#build).

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
hôte. Linux dispose aussi d’un exemple fenêtré à compiler avec SDL3. Le paquetage macOS
reste en cours. Sous Android, le transport HTTP demande libcurl compilé avec le NDK et une
configuration TLS ; l’intégration applicative complète reste reportée. Voir les [prérequis](doc:integration#build).

## Lire ce manuel {#navigation}

Utiliser l'arbre des pages ou le champ de recherche ; **Ctrl+K** ouvre la recherche.
Les résultats mènent directement aux rubriques correspondantes. L'arbre regroupe **Commencer**,
**Utiliser Chat Lab**, **Développer avec le SDK** et **Aide**. Le fil d'Ariane situe la page ;
**Précédent/Suivant** suit son parcours d'apprentissage.

Basculer entre anglais et français conserve la page et la rubrique. **Mon espace** contient
les signets et notes de ce navigateur ; leur conservation dépend de son stockage local.
Les captures et schémas s'agrandissent dans le lecteur ; **Échap** ferme la vue agrandie.
Le [graphe](doc:optimization#tool-map) de découverte des outils propose aussi une vue textuelle filtrable.

Ouvrir l'export complet `Documentation/Documentation.html` directement depuis le disque.
Dans Chat Lab, **F1** ouvre l'aide ; **Ouvrir le manuel complet** affiche le guide dans la langue sélectionnée.
