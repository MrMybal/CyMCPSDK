---
id: providers
title: Choisir un fournisseur
description: Sélectionner CLI, API native ou service local avec ses vrais modèles et identifiants.
section: Guides
parent: chat-lab
order: 1
sourceRevision: 1
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

Les CLI natifs utilisent leur environnement et leur compte existants. Sélectionner le vrai
exécutable si le gestionnaire de paquets ne fournit qu'un wrapper shell. Les arguments sont
des chaînes structurées ; le SDK ne les passe pas à un shell. `process.environment` transmet
des variables uniquement au processus enfant.

Les identifiants API viennent d'une variable d'environnement nommée ou du champ masqué en mémoire
de Chat Lab. Le CLI et l'interface peuvent charger cette configuration sans secret avec `--config` :

```json
{
  "provider": "openai",
  "model": "",
  "api_key_env": "OPENAI_API_KEY"
}
```

Saisir un modèle retourné par le fournisseur avant d'envoyer un message. Ne pas mettre de vraie
clé dans un fichier JSON. `settings.base_url` remplace l'adresse du service. Le profil Ollama
attend la base de son API native ; les serveurs compatibles utilisent généralement une base `/v1`.

## Découvrir modèles et capacités {#models}

L'exemple sans interface propose :

```sh
cymcp_chat providers
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
