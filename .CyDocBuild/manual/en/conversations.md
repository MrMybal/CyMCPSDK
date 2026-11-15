---
id: conversations
title: Conversations and history
description: Attach files, cancel a turn and restore an exact conversation without replaying it.
section: Guides
parent: chat-lab
order: 2
revision: 2
---

## Attachments {#attachments}

**Attach files** snapshots the selected local files on the worker. Supported types include images,
audio and text/PDF resources, depending on the transport and model. The panel lists attachment names;
rich media preview requires resource services supplied by the host application.

The composer grows with the text and accepts up to 256 files. File/content budgets and MIME/base64
validation also apply. Unsupported content is rejected explicitly. A model receiving an image
does not imply support for audio or documents.

## Preserve a conversation {#history}

1. Enable **Save history** before connecting.
2. Choose a private directory and a simple history ID.
3. Send messages normally; each completed or interrupted turn is saved automatically.
4. Select the same provider/model, directory and ID, then **Restore saved chat**.

History is optional. It contains messages, tool results and embedded attachments: treat it as user data.
Keys and connection configuration are not included. A storage error appears separately from a successful
generation. Restoring does not resend a prompt or repeat an executed command.

## Resume and cancel {#resume}

Native providers resume their exact session when supported; stateless APIs use the host's saved history.
If the native session cannot be loaded, `SessionInvalid` preserves the history instead of creating a
replacement conversation. ACP session loading depends on the agent's negotiated capabilities.

**Stop** cancels the active operation. **Reconnect** explicitly reopens the connection when needed.
After interruption, decide whether to send another message; there is no automatic generation retry.

## Public C++ API {#api}

`Conversation::checkpoint()` returns a restorable snapshot. `ConversationStore::save(id, conversation)`
writes it privately and atomically; `load(id)` reads it for `Conversation::restore(snapshot)`.
The provider/model must match the snapshot. Keep provider instances and the executor alive until all
asynchronous operations finish. Callbacks run on the calling worker, not the UI thread.
