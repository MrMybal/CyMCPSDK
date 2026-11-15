# CyMCPSDK agent guide

CyMCPSDK 0.5.4 is a modular MIT C++20 SDK. The human reader is
`Documentation.html#/en/overview` (French: `#/fr/overview`). Ship this entire
directory with `DocData/`. The reader needs no Node.js, Python or HTTP server.

## Work on the SDK

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
Fixtures do not establish authentication or inference compatibility with every CLI.
Consult the source version notes for confirmed platform limitations.

## Maintain this manual

Author in `.CyDocBuild/manual/en` and fully translate matching IDs and explicit
anchors under `manual/fr`. Increment English `revision` and French `sourceRevision`
together after reviewing the translation. Do not edit generated bundles.

From `.CyDocBuild`, install the pinned toolchain with `npm ci`, then run
`npm run build` and `npm run check`. Both commands write the export. Node.js 22+
is a generation dependency only. The bundled CyDoc generation-only archive comes from
upstream commit `93f64486d61dae27a94ae1e5a05a20f6edd99914`, version 0.1.0. Its reader code
is unchanged; unused examples/guides and development scripts are omitted.

Keep audit/test reports and documentation maintenance notes outside the human
content tree. Publish public API explanations and useful examples, not complete
implementation listings. Preserve stable routes and ship all generated assets.
