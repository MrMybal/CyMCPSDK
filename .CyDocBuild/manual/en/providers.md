---
id: providers
title: Choose a provider
description: Select a CLI, native API or local endpoint and supply its real model and credentials.
section: Guides
parent: chat-lab
order: 1
revision: 4
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

Use **Automatic connection** first: installed CLIs and supported npm packages are resolved
without entering paths or arguments. Native CLIs reuse their existing environment and account.
Detection covers every named CLI profile above; the generic `acp` profile requires a host recipe.
An installation is not proof of successful authentication or generation.

For an unusual installation, edit the selected provider in the **Providers** tab. A host application can add executable
directories, private npm roots and discovery recipes for its own providers. Provider-specific
arguments are structured strings; the SDK never runs them through a shell.
`process.environment` passes child-only environment values.

Cloud discovery recognizes `OPENAI_API_KEY`, `ANTHROPIC_API_KEY` and `OPENROUTER_API_KEY`.
Endpoint overrides use `OPENAI_BASE_URL`, `ANTHROPIC_BASE_URL`, `OPENROUTER_BASE_URL` or
`OLLAMA_HOST`. Running local servers are queried for their models; a stopped service or an
empty model catalogue is shown explicitly. Private configuration files are not searched for secrets.

API credentials come from a named environment variable or Chat Lab's masked memory field.
For example, the CLI and GUI can load this non-secret configuration with `--config`:

```json
{
  "provider": "openai",
  "model": "",
  "api_key_env": "OPENAI_API_KEY"
}
```

In automatic mode, choose a returned model in the dropdown or keep the provider default.
Only local services get a model automatically; a remote API shows its catalogue and waits for
an explicit choice. In manual mode, selecting a provider pre-fills its conventional settings
(endpoint, key variable, timeouts) and the detected installation.
Manual mode also accepts an exact model ID. Do not put a real key in a JSON file.
Use `settings.base_url` for an endpoint override. The Ollama profile expects its native
API base, whereas compatible servers commonly use a base ending in `/v1`.

## Discover models and capabilities {#models}

The headless example exposes:

```sh
cymcp_chat providers
cymcp_chat discover
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
