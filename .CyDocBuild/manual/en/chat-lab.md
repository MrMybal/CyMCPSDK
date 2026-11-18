---
id: chat-lab
title: Chat Lab controls
description: Connect a provider, send messages and expose the demonstration app commands.
section: Guides
parent: overview
order: 2
revision: 8
---

## Connect {#connect}

Chat opens directly. **Settings** contains **Providers**, **Logs** and **Help**. **Close** returns
to the conversation and its draft. **F1** opens Help.
Open **Providers** to search the catalogue, select a provider and edit its own fields. Selecting
a provider keeps the other providers' fields in memory for this run. Use **Connect / New chat**
to open it and return to Chat, or double-click a provider card to connect directly.

```image
{"id":"provider-settings","src":"assets/providers-en.png","alt":"Searchable provider catalogue with the OpenAI API connection and model settings","controls":"minimal"}
```

**Automatic connection**, in Providers, is enabled at startup. Chat Lab detects installed native CLIs,
user npm installations, running Ollama/LM Studio/compatible services at their known local
addresses, and API keys supplied through environment variables. It prepares the first available
connection without sending a message. The initial offline demonstration does not block this selection.

Native CLIs retain their own default model and account. Local services can select a text model
from the actual catalogue. Remote APIs display their catalogue and wait for your model choice.
The model dropdown offers returned IDs; a changed selection takes effect when you click
**Connect**. Availability can still depend on your account and provider.

**Detect again** repeats the scan. Provider cards distinguish detected installations, active
connections and setup requirements. The selected provider explains missing installations,
credentials, endpoints or local models. No provider is installed or authenticated by the scan.
API keys remain in memory. Cloud APIs are contacted only when their connection is selected.

If a native provider reports that authentication is required, **Sign in** opens its own login
in a console. Complete that login and reconnect here. This action uses the detected installation.

Manual fields remain available in Providers with automatic mode enabled. **Refresh model catalogue**
retrieves real model IDs. Model and connection changes apply to the next **Connect / New chat**.
Selecting a provider pre-fills its conventional endpoint, key variable and settings, together
with any detected installation.

```image
{"id":"automatic-connection","src":"assets/chat-auto-en.png","alt":"Earlier interface capture of automatic CLI detection and a real streamed Codex reply","controls":"minimal"}
```

**Installation and connection** contains the CLI path and working directory or the API endpoint/key fields.
Expand **Advanced launch settings** for the JSON argument array and provider-specific settings.
Use the actual `.exe`, or `node.exe` and its script arguments; shell wrappers are not launched.
**Advanced settings (JSON)** accepts provider-specific settings. **System prompt**, under **Conversation and history**, supplies
instructions at session creation. See [providers](doc:providers#configure).

## Send and stop {#messages}

Type in the composer. **Enter** sends; **Shift+Enter** inserts a line. **Ctrl+Enter** also sends. **Attach files** selects
local attachments. Text streams into the Markdown transcript; activity and thinking appear
when emitted by the provider. **Follow responses** controls automatic scrolling.

The native input grows with the draft. Select text for the formatting toolbar, or use
Ctrl+B/I/U/E. Drafts remain editable during a response; sending while busy keeps the draft.
Use the chat’s **…** menu for a rendered draft preview. **Ctrl+F** searches the conversation.
Press **↑** in an empty composer to recall the last user message. In conversations whose history
the SDK manages, you can edit a sent message or confirm deletion of its turn. Both actions leave
generation idle.
Hover or right-click a message to **Copy message** or **Reply**; Reply inserts a quote into
the draft and waits for you to send it. Native session history stays under provider control.

Attachments have their own cards. Images decode asynchronously and enlarge inside the app.
To include an image in a Markdown preview, attach the document and image to the same message
and use its unique filename, for example `![Result](./plot.png)`. Reference definitions also work.
Click a text attachment to preview it, or **Save attachment** to choose a destination for its
embedded bytes. Press **Ctrl+V** to paste a clipboard image or copied Explorer files. Check
the thumbnail and remove an attachment before sending if needed. Preview services never
fetch arbitrary Markdown image URLs. Formats without a decoder retain their file card and Save action.

During generation **Stop** appears next to **Send**. Cancelling preserves the partial answer.
If the provider rejects a turn, that reply shows the provider’s explanation, the SDK error code
and the provider code when available. Text already received remains visible. The same diagnostic
is available in **Logs**, with credentials masked. You can change the model and send another
message when ready.
**Reconnect** explicitly resumes the current session; it does not resend the interrupted message.
See [conversations and history](doc:conversations).

## Read the journal {#logs}

Open **Settings → Logs**. **Raw JSONL** shows the same events as the session file. Scroll up
to read earlier entries while new events arrive. Rows show the local time, provider, operation,
event/error code, explanation and duration. Native connection stages include protocol initialization,
account checks and session creation. Generation events show response start and completion.

Use the text filter or **Errors only**, toggle **Follow log**, and use **Copy log** to share diagnostics.
**Open log file** opens the private session file saved in `%LOCALAPPDATA%/CyMCPSDK/ChatLab/logs`.
File writing runs in the background. Hover the filename for its full path. **Clear view** clears the
on-screen rows and preserves the file. Message contents, credentials and native session IDs are excluded.

The view retains the newest 2,000 rows. Each session file is limited to 8 MiB; a visible notice explains
when it stops growing or cannot be written. Logging never cancels a conversation.

```image
{"id":"connection-journal","src":"assets/logs-en.png","alt":"Chat Lab log showing a model refusal reproduced with a Codex simulator","controls":"minimal"}
```

## Let an AI use app commands {#commands}

In Providers, expand **Permissions and app tools**. The demonstration exposes `get_counter` and `set_counter`,
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
