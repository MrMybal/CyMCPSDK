---
id: troubleshooting
title: Resolve connection problems
description: Understand provider errors, interrupted sessions and missing local help.
section: Guides
parent: overview
order: 4
revision: 3
---

## Connection errors {#connection}

Open **Logs** to see which discovery, protocol, account, catalogue or session step failed.
Use **Copy log** or **Open log file** for the diagnostic record. See the [journal controls](doc:chat-lab#logs).

| Error or symptom | What to do |
| --- | --- |
| `ExecutableMissing` | Select the native CLI executable; verify it runs in the intended account/environment. |
| `AuthenticationRequired` | For native providers, use **Sign in**, complete the native login and reconnect. For APIs, supply the credential through its environment/memory field. |
| `ProviderError` after connecting | Verify the exact model ID, account entitlement and provider-native error outside the app. |
| Catalogue returns `Unsupported` | Enter a real model ID manually; some transports do not expose discovery. |
| Local endpoint unreachable | Start the local service and check its base URL/port and API format. |
| `UnsupportedContent` | Choose a model/transport accepting that attachment type, or remove the attachment. |

The CLI account used by a launched process can differ from another terminal's environment.
Use `process.environment` when the host intentionally needs child-specific values. A successful
connection is not a guarantee that the account can perform a generation.

## Interrupted conversations {#interrupted}

`Cancelled` is an interruption, not a successful final answer. A truncated stream can report
`UnexpectedEOF`. Keep the partial text, reconnect explicitly, and decide what to send next.
`SessionInvalid` means the exact native session could not be resumed; retain its saved history.

`ContextTooLarge` now means a memory guard (256 MiB per message) or the model's own context
window was reached: start a new conversation or reduce attachments. A slow or minimised UI no
longer fails a turn; very long transcripts only scroll their oldest rows out of the view.

## Missing manual {#help}

Keep `Documentation/Documentation.html` and the complete `Documentation/DocData` directory
beside Chat Lab. F1 opens that installed copy, not an online fallback. If the folder is absent,
restore it from the application package or rebuild the package. Open the HTML directly to check
its local scripts and images. The reader needs no Node.js or web server.
