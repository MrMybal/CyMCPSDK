---
id: local-engines
title: Moteurs locaux et dépendances
description: Connecter un service existant ou superviser un moteur compatible llama.cpp installé.
section: SDK
parent: integration
order: 3
sourceRevision: 2
---

## Utiliser un service existant {#service}

Choisir `ollama` pour l'API native Ollama, `lmstudio` pour son serveur compatible, ou `llama.cpp`
pour une adresse compatible déjà active. Renseigner l'URL réelle du service et un modèle qu'il propose.
Choisir un profil fournisseur n'installe ni ne lance ce produit.

Chat Lab se connecte aux services locaux. Il ne propose pas encore d'installateur de modèles ou de
paquets GPU. L'application qui intègre le SDK peut fournir son propre parcours d'installation.

## Superviser un moteur natif {#supervise}

`LocalEngine` possède un processus enfant compatible llama.cpp. Ses options comprennent le chemin
de l'exécutable, le modèle, un dossier de travail privé, le contexte en tokens, les couches GPU et le
délai de démarrage. Exécutable et modèle doivent déjà exister et être compatibles.

```cpp
#include <cymcp/local.hpp>

void start_local(const std::filesystem::path& server_executable,
                 const std::filesystem::path& model,
                 const std::filesystem::path& private_work_directory) {
    cymcp::LocalEngineOptions options;
    options.process.executable = server_executable;
    options.model = model;
    options.private_work_directory = private_work_directory;
    cymcp::LocalEngine engine(options);
    engine.start();
    // Pendant la vie de engine, configurer un fournisseur API avec
    // engine.base_url() et engine.token(), puis exécuter la conversation hôte.
    engine.stop();
}
```

Garder le moteur en vie jusqu'à la fin des conversations. Le superviseur vérifie sa disponibilité,
fournit un fichier d'authentification privé et arrête l'arbre de processus qu'il possède. Il ne gère pas
un service utilisateur indépendant. Choisir les paramètres GPU selon le moteur et le runtime installés ;
aucun composant GPU n'est embarqué.

## Télécharger des composants vérifiés {#dependencies}

`DependencyManager` reçoit un manifeste `Package` de confiance fourni par l'hôte : URL des artefacts,
chemins relatifs, tailles, empreintes SHA-256 et arguments d'installation éventuels. Il propose
`install`, `verify`, `run_installer` et `uninstall`. La politique hôte décide des actions permises ;
un callback d'élévation gère les installateurs qui en ont besoin.

L'application contrôle les manifestes, jamais la sortie d'un modèle. Les chemins restent dans le cache
configuré ; HTTPS et les hôtes de redirection déclarés encadrent les téléchargements. Les paquets
externes gardent leurs licences. L’hôte peut déclarer des fichiers ordinaires dans une archive avec
leur destination, leur taille et leur SHA-256. Le lecteur tar du système les extrait dans un dossier
privé avant publication.

Sous Windows x64, sélectionner un service local dans **Fournisseurs** de Chat Lab, ouvrir
**Installation et connexion** puis choisir **Installer le runtime NVIDIA CUDA 12**. Chat Lab
télécharge NVIDIA CUDART 12.8.90 et sa licence dans le cache privé de l’utilisateur. Il affiche le
dossier du runtime après vérification. Un pilote NVIDIA compatible et les éventuelles autres
bibliothèques exigées par le moteur choisi sont des prérequis distincts.

Les applications Windows et Linux x64 peuvent exécuter la même installation sur un worker et
fournir le dossier à un moteur supervisé. Le superviseur applique le chemin de recherche des
bibliothèques au processus enfant.

```cpp
cymcp::DependencyManager dependencies({cache_prive, politique_hote});
auto installation = dependencies.install(cymcp::cuda_runtime_package(), contexte);
options.cuda_runtime_directory = installation / "runtime"; // LocalEngineOptions
```

Les empreintes proviennent du [manifeste de redistribution NVIDIA CUDA 12.8.1](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_12.8.1.json).
Le paquet installé contient la licence NVIDIA. La reprise HTTP Range et la sélection automatique du GPU restent à réaliser.
