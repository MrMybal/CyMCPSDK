# CyMCPSDK agent guide

CyMCPSDK 0.7.5 is a modular MIT C++20 SDK. The human reader is
`Documentation.html#/en/overview` (French: `#/fr/overview`). Ship this entire
directory with `DocData/`. The reader needs no Node.js, Python or HTTP server.

## Work on the SDK

- Disk spooling, performance records, MCP change subscriptions and API context windows are
  independent opt-in facilities. See runtime-options in the manual. No implicit summarizer/model.
  Stream producers publish only complete results, and must never be automatically retried.
- Context projection retains full host history and pins systems/current turns. Keep tool-call
  groups intact. Native agents retain their own context logic. Budgets are bytes, not token claims.
- Performance records contain metadata only. Notification readers deliver no host callbacks;
  drain their bounded queues on the host thread and keep legacy TTL behavior available.

- Large API/Codex tool registries use five discovery tools by default (threshold 32).
  Search, load a full schema, then call the original operation. Every operation remains
  accessible and its policy is always enforced. `deferred_tools=false` opts out.
- MCP resources use bounded manifests/chunks. Follow firstUri/nextUri; never assume
  a preview is the entire result or retry a mutation to recover an expired handle.
- Resource grep is native. Optional FileSearch uses a separately supplied ripgrep
  executable and an explicit host-selected root; no workspace is exposed by default.
- MCP client caches only read-only positive-TTL results, privately per client.
  Use clear_cache() after known external changes. Never cache tools/call results.

- Read the host project's instructions before changing its integration.
- Use `CyMCP::Core` for operations and policies, `CyMCP::MCP` for tool transport,
  `CyMCP::AI` for provider conversations, `CyMCP::Local` for engine supervision,
  and optional `CyMCP::ChatUI` for CyUI rendering.
- Reuse actual provider catalogues and native session IDs. Do not invent a model
  ID, silently replace a resumed session or retry an executed generation/tool.
- Keep secrets in host memory, environment or its vault; never add them to
  examples, catalogues, screenshots, history configuration or logs.
- CLI commands use structured argv without a shell. Native provider policies and
  app operation policies are distinct; unrestricted behavior is explicit.
- Keep UI work nonblocking. Providers run on the controller worker; CyUI rendering
  stays on the host UI thread. English is the default, with complete French labels.
- Chat Lab opens Chat directly; Settings contains Providers, Local MCP, Logs and Help.
  F1 opens that Help panel; Open full manual opens the local CyDoc reader in the selected language.
  Per-provider fields stay in host memory.
  ChatPanel uses native messageComposer and virtual TranscriptSource. Enter/Ctrl+Enter submit,
  Shift+Enter inserts a line. Structured ChatAttachment contents are worker-loaded immutable
  portable blocks with opaque source IDs. Native texture services must remain nonblocking.
  Copy/reply are wired; provider-history edits/deletions require both explicit host callbacks.
  ChatViewState.diagnostics contains bounded structured lifecycle events; native payloads,
  message text, credentials and session IDs must never be copied into diagnostic records.
  The example writes a private session file on a separate worker and supports --log-file.
- Chat Lab defaults to automatic discovery and opens a detected connection without
  sending a message. ProviderRegistry::discover is reusable without CyUI; extensions
  declare discovery recipes and host-private installation locations. Manual settings
  remain available. Never change providers automatically after a connection failure.
- Automatic model selection applies to local services. Remote APIs return their
  actual catalogue and wait for the user's choice. Provider defaults are exposed by
  describe(); Sign in opens a native login only after the user's explicit activation.
- Inspect `Docs/Integration.md`, `Docs/Providers.md`, `Docs/Conversations.md` and
  `Docs/ChatUI.md` in the source checkout for detailed developer contracts.

## Build and validate

Use CMake 3.24+ and a C++20 compiler. `cmake --preset release`,
`cmake --build --preset release`, then `ctest --preset release` build the SDK.
The Windows example uses `tools/build-chat.ps1` with a host CyUI checkout.
The Linux windowed example uses `tools/build-chat-linux.sh` with the full CyUI checkout,
host libcurl and SDL3 3.4+. Set `CMAKE_PREFIX_PATH` for a custom SDL3 installation.
Run its graphical tests from an X11/Wayland session; `SDL_RENDER_DRIVER=software`
supports VMs without accelerated rendering. SDL3 is an example-only dependency.
Fixtures do not establish authentication or inference compatibility with every CLI.
Consult the source version notes for confirmed platform limitations.

## Maintain this manual

Author in `.CyDocBuild/manual/en` and fully translate matching IDs and explicit
anchors under `manual/fr`. Increment English `revision` and French `sourceRevision`
together after reviewing the translation. Do not edit generated bundles.

From `.CyDocBuild`, install the pinned toolchain with `npm ci`, then run
`npm run build` and `npm run check`. Both commands write the export. Node.js 22+
is a generation dependency only. The official bundled `vendor/cydoc-0.2.0.tgz` comes from
upstream commit `9837f815cdaa67e3206f68e15d04f4700f040dbf`, version 0.2.0, built with
`npm ci` and `npm pack`. Reader/generator code and package metadata are unchanged.
Read its installed `ai/README.md`, `UPDATE.md`, `INTEGRATION.md`, `AUTHORING.md` and
`dist/*.d.ts` before changing the integration. See `.CyDocBuild/TOOLCHAIN.md` in the source checkout.

The 13 public pages retain their IDs and explicit anchors. Parent IDs organize the topic tree;
`previous`/`next` define separate Chat Lab and SDK reading paths without changing routes.
Enable only `image` and `graph`; static diagrams need no module, and Three.js is unused.
Preserve screenshot/media IDs and graph node/port IDs across both languages. Use `href` on
graph nodes for their documentation links. Check the compiler and reader implementation
when an upstream example differs from the installed contract.

Help URLs already encode local paths and point to `#/en/chat-lab` or `#/fr/chat-lab`.
Ship all of `Documentation/`; exclude `.CyDocBuild/` from the application package.
Verify the export through `file://`, both locales, search, anchors, learning paths and enabled
media at wide/narrow sizes without network access. Report native F1 checks separately.

Keep audit/test reports and documentation maintenance notes outside the human
content tree. Publish public API explanations and useful examples, not complete
implementation listings. Preserve stable routes and ship all generated assets.
