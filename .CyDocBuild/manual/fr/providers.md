---
id: providers
title: Choisir un fournisseur
description: Sélectionner CLI, API native ou service local avec ses vrais modèles et identifiants.
section: Guides
parent: chat-lab
order: 1
sourceRevision: 3
---

## Profils intégrés {#profiles}

| Type | Identifiants du registre |
| --- | --- |
| Agents natifs | `codex`, `claude-code`, `antigravity`, `command-code`, `codebuddy` |
| Agents ACP v1 | `opencode`, `gemini`, `qwen-code`, `kimi`, `goose`, `devin`, `command-code-acp`, `codebuddy-acp`, `acp` |
| OpenCode historique 1.x | `opencode-cli` |
| API natives | `openai` (Responses), `anthropic` (Messages), `openrouter`, `ollama` |
| Serveurs HTTP compatibles | `openai-compatible`, `llama.cpp`, `lmstudio` |

Ces 22 profils décrivent des transports et des recettes de lancement. La version installée, le
serveur et le modèle déterminent les capacités réelles. ACP v1 négocie le chargement des sessions,
les modèles, modes et contenus acceptés. `opencode-cli` est l'ancien transport JSONL ; OpenCode
actuel utilise `opencode` avec ACP.

## Configurer une connexion {#configure}

Utiliser d'abord **Connexion automatique** : les CLI installés et les paquets npm pris en
charge sont résolus sans saisir de chemins ni d'arguments. Les CLI réutilisent leur compte
et leur environnement. La détection couvre chaque profil CLI nommé ci-dessus ; le profil
générique `acp` nécessite une recette fournie par l'hôte. Une installation ne prouve pas
que l'authentification ou la génération réussira.

Pour une installation particulière, passer aux paramètres manuels. L'application hôte peut
ajouter ses dossiers d'exécutables, ses racines npm privées et ses recettes de détection.
Les arguments sont des chaînes structurées ; le SDK ne les passe jamais à un shell.
`process.environment` transmet des variables uniquement au processus enfant.

La détection reconnaît `OPENAI_API_KEY`, `ANTHROPIC_API_KEY` et `OPENROUTER_API_KEY`.
Les adresses peuvent venir de `OPENAI_BASE_URL`, `ANTHROPIC_BASE_URL`,
`OPENROUTER_BASE_URL` ou `OLLAMA_HOST`. Les services locaux démarrés sont interrogés
pour leurs modèles ; un service arrêté ou sans modèles est signalé explicitement.
La recherche n'inspecte pas les fichiers privés pour y chercher des secrets.

Les identifiants API viennent d'une variable d'environnement nommée ou du champ masqué en mémoire
de Chat Lab. Le CLI et l'interface peuvent charger cette configuration sans secret avec `--config` :

```json
{
  "provider": "openai",
  "model": "",
  "api_key_env": "OPENAI_API_KEY"
}
```

En mode automatique, choisir un modèle retourné dans la liste ou conserver le réglage natif.
Seuls les services locaux reçoivent un modèle automatiquement ; une API distante affiche son
catalogue et attend un choix explicite. En mode manuel, choisir un fournisseur préremplit ses
paramètres conventionnels (adresse, variable de clé, délais) et l'installation détectée.
Le mode manuel accepte aussi un identifiant exact. Ne pas mettre de vraie
clé dans un fichier JSON. `settings.base_url` remplace l'adresse du service. Le profil Ollama
attend la base de son API native ; les serveurs compatibles utilisent généralement une base `/v1`.

## Découvrir modèles et capacités {#models}

L'exemple sans interface propose :

```sh
cymcp_chat providers
cymcp_chat discover
cymcp_chat models examples/chat-ollama.json
```

`providers` liste les capacités des transports configurés ; `models` contacte le fournisseur choisi.
Un catalogue non pris en charge ne signifie pas que le tchat est indisponible : utiliser un ID exact
fourni par l'outil natif. Le modèle choisi s'applique à une nouvelle connexion. Le SDK ne change pas
silencieusement de modèle et ne rejoue pas les générations échouées.

## Ajouter un fournisseur {#extension}

Implémenter l'interface publique `Provider`, puis enregistrer un `ProviderDescriptor` et sa factory
avec `ProviderRegistry::add`. Les méthodes `connect`, `start_session`, `send` et `disconnect`
doivent respecter les capacités annoncées. `ProviderConfig` fournit arguments, réglages,
identifiants, registre d'opérations optionnel et callbacks de droits.
Consulter l'[intégration](doc:integration#providers).
