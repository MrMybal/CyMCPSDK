---
id: chat-lab
title: Chat Lab controls
description: Connect a provider, send messages and expose the demonstration app commands.
section: Guides
parent: overview
order: 2
revision: 3
---

## Connect {#connect}

**Automatic connection** is enabled at startup. Chat Lab detects installed native CLIs,
user npm installations, running Ollama/LM Studio/compatible services at their known local
addresses, and API keys supplied through environment variables. It prepares the first available
connection without sending a message. Click another available connection to use it directly.

Native CLIs retain their own default model and account. Local services can select a text model
from the actual catalogue. Remote APIs display their catalogue and wait for your model choice.
The model dropdown offers returned IDs; a changed selection takes effect when you click
**Connect**. Availability can still depend on your account and provider.

**Detect again** repeats the scan. **Other providers / setup** explains missing installations,
credentials, endpoints or local models. No provider is installed or authenticated by the scan.
API keys remain in memory. Cloud APIs are contacted only when their connection is selected.

If a native provider reports that authentication is required, **Sign in** opens its own login
in a console. Complete that login and reconnect here. This action uses the detected installation.

Disable **Automatic connection** to open the optional manual form. **Refresh model catalogue**
retrieves real model IDs. Model and connection changes apply to the next **Connect / New chat**.
Selecting a provider pre-fills its conventional endpoint, key variable and settings, together
with any detected installation.

```image
{"id":"automatic-connection","src":"assets/chat-auto-en.png","alt":"Automatic detection of installed CLIs and a real streamed Codex reply","controls":"minimal"}
```

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
