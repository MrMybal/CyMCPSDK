---
id: providers
title: Choose a provider
description: Select a CLI, native API or local endpoint and supply its real model and credentials.
section: Guides
parent: chat-lab
order: 1
revision: 1
---

## Built-in profiles {#profiles}

| Type | Registry IDs |
| --- | --- |
| Native agents | `codex`, `claude-code`, `antigravity`, `command-code`, `codebuddy` |
| ACP v1 agents | `opencode`, `gemini`, `qwen-code`, `kimi`, `goose`, `devin`, `command-code-acp`, `codebuddy-acp`, `acp` |
| Historical OpenCode 1.x | `opencode-cli` |
| Native APIs | `openai` (Responses), `anthropic` (Messages), `openrouter`, `ollama` |
| Compatible HTTP servers | `openai-compatible`, `llama.cpp`, `lmstudio` |

These 22 profiles describe transports and launch recipes. The installed version, server and model
determine actual capabilities. ACP v1 negotiates session loading, models, modes and prompt content.
`opencode-cli` is the older JSONL transport; current OpenCode uses `opencode` with ACP.

## Configure a connection {#configure}

Native CLIs use their existing environment and account. Select the native executable when a
package manager exposes only a shell wrapper. Provider-specific arguments are structured strings;
the SDK does not run them through a shell. `process.environment` passes child-only environment values.

API credentials come from a named environment variable or Chat Lab's masked memory field.
For example, the CLI and GUI can load this non-secret configuration with `--config`:

```json
{
  "provider": "openai",
  "model": "",
  "api_key_env": "OPENAI_API_KEY"
}
```

Enter a model returned by your provider before sending. Do not put a real key in a JSON file.
Use `settings.base_url` for an endpoint override. The Ollama profile expects its native
API base, whereas compatible servers commonly use a base ending in `/v1`.

## Discover models and capabilities {#models}

The headless example exposes:

```sh
cymcp_chat providers
cymcp_chat models examples/chat-ollama.json
```

`providers` lists configured transport capabilities; `models` contacts the selected provider.
An unsupported catalogue is not proof that chat is unavailable: use an exact model ID from the
native provider. A chosen model applies to a new connection. The SDK does not silently switch
models or replay failed generations.

## Add a provider {#extension}

Implement the public `Provider` interface and register a `ProviderDescriptor` plus factory with
`ProviderRegistry::add`. Keep `connect`, `start_session`, `send` and `disconnect` consistent with
the advertised capabilities. `ProviderConfig` supplies arguments, settings, credentials,
an optional operation registry and permission callbacks. See [integration](doc:integration#providers).
