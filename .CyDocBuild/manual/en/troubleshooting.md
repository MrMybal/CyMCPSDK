---
id: troubleshooting
title: Resolve connection problems
description: Understand provider errors, interrupted sessions and missing local help.
section: Help
order: 30
revision: 4
previous: conversations#resume
next:
---

## Connection errors {#connection}

Start from the failed action: detecting an installation, opening a session, loading models or sending
a message. Open **Settings → Logs**, enable **Errors only**, and read the provider's explanation
and error code. **Copy log** or **Open log file** retrieves the diagnostic record; see the
[journal controls](doc:chat-lab#logs). A connected session alone does not prove generation access.

| Symptom | Check and remedy | Recognize success |
| --- | --- | --- |
| `ExecutableMissing` or no detected CLI | Verify that the native executable runs in the intended account. In **Providers**, use **Detect again**, or set its actual executable path. | The provider is detected and **Connect / New chat** opens a session. |
| `AuthenticationRequired` | For a native CLI, choose **Sign in**, complete its login and reconnect. For an API, supply the key through its environment variable or masked memory field. | Account initialization succeeds; a short message returns a response. |
| `ProviderError` after connecting | Read the provider-native code in the failed turn or Logs. Verify the model ID and account entitlement; choose an available model and connect again. | A new, deliberately sent short message finishes without that error. |
| Catalogue returns `Unsupported` | Use an exact model ID from the provider in manual settings. Some transports have no model catalogue. | The selected model accepts a message. |
| Local endpoint unreachable | Start the service, verify its port and choose the matching API format. Ollama uses its native base URL; compatible APIs commonly end in `/v1`. | The catalogue contains installed models and the selected model responds. |
| `UnsupportedContent` | Remove the rejected attachment or choose a model/transport accepting its type. | A message with supported content completes. |

The CLI account used by a launched process can differ from another terminal's environment.
For host-specific child environment settings, see [provider configuration](doc:providers#configure).
Changing a connection never resends a failed message automatically.

## Interrupted conversations {#interrupted}

| Symptom | Remedy | Recognize success |
| --- | --- | --- |
| `Cancelled` after **Stop** | Keep the partial response. Decide whether to send a new message. | The conversation is idle and its draft remains editable. |
| `UnexpectedEOF` or a truncated stream | Keep the partial text, use **Reconnect**, then decide what to send next. | The connection is ready; no interrupted message is resent by reconnecting. |
| `SessionInvalid` | Keep the saved history. Verify that the selected provider can load the exact native session; create a new conversation explicitly if necessary. | The intended session resumes, or a deliberately created new chat is identified as such. |
| `ContextTooLarge` | Reduce attachments or start a new conversation. The error can come from the 256 MiB per-message memory guard or the model's context window. | A smaller new request completes. |

Long transcripts can scroll their oldest rows out of the view; a slow or minimised UI alone does
not fail a turn. For saving and restoring without replay, see [conversations and history](doc:conversations#history).

## Missing manual {#help}

**F1 opens the Help panel.** Use **Open full manual** there to open the installed reader.
If it reports `DocumentationMissing`, restore the complete `Documentation/` folder beside
`cymcp_chat_app.exe` (or the Linux executable), from the same application package.

Open `Documentation/Documentation.html` directly. Keep `DocData/`, its scripts and assets
in their original relative locations. The HTML source view on GitHub is not the reader;
download the complete folder first. No Node.js or web server is needed for reading.

**Check the repair:** the overview opens, a language switch stays on the same page,
and the [demonstration screenshot](doc:quickstart#demo) loads. If bookmarks or notes disappear,
check this browser's file-origin storage policy and export personal data through **My space**
before changing browser profiles or clearing storage.
