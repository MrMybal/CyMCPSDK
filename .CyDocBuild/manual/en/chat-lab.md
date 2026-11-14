---
id: chat-lab
title: Chat Lab controls
description: Connect a provider, send messages and expose the demonstration app commands.
section: Guides
parent: overview
order: 2
revision: 1
---

## Connect {#connect}

Select a provider, then its model. **Refresh model catalogue** asks the provider for
its actual models; otherwise enter an exact model ID manually. Model and connection
changes apply to the next **Connect / New chat**. A provider can reject unavailable models.

Expand **Executable** for a native CLI path, JSON argument array and working directory.
Use the actual `.exe`, or `node.exe` and its script arguments; shell wrappers are not launched.
**Advanced settings (JSON)** accepts provider-specific settings. **System prompt** supplies
instructions at session creation. See [providers](doc:providers#configure).

## Send and stop {#messages}

Type in the composer. **Ctrl+Enter** sends; **Enter** inserts a line. **Attach files** selects
local attachments. Text streams into the Markdown transcript; activity and thinking appear
when emitted by the provider. **Follow responses** controls automatic scrolling.

**Stop** requests cancellation. A partial answer remains marked interrupted.
**Reconnect** explicitly resumes the current session; it does not resend the interrupted message.
See [conversations and history](doc:conversations).

## Let an AI use app commands {#commands}

Expand **App commands**. The demonstration exposes `get_counter` and `set_counter`,
with integer values between −1,000,000 and 1,000,000. The current value is visible in the panel.

1. Enable **Allow counter changes** if you want writes to succeed.
2. Enable **AI may use app commands**, then make a new connection to Codex or a supported API transport.
3. Ask the AI to set the demonstration counter and read it back.

Direct attachment supports Codex, Responses, Messages, Chat Completions/OpenRouter and Ollama.
Other CLIs use their native MCP configuration and return `Unsupported` for this direct-attachment option.

## Connect another application {#external}

**Expose local MCP** creates an authenticated loopback server on a free port.
Copy the displayed endpoint and use **Copy MCP token** to configure your client.
Use `/mcp` for MCP tools, or `/v1` on the same port for direct SDK JSON.

```json
{"method":"invoke","name":"get_counter","arguments":{}}
```

Both routes share the same counter and live write policy. Disabling writes does not
disconnect the client. Disabling the MCP server closes the endpoint; enabling it again
creates a new token. See [operations](doc:operations) and [permissions](doc:permissions).
