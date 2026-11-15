# CyMCPSDK version notes

[Français](VERSION.fr.md) · [Manual](Documentation/Documentation.html#/en/overview)

## 0.5.3 — In development

- Chat Lab starts in automatic mode: installed native CLIs, supported npm installations,
  environment API keys and running local model services are detected in the background.
- Available connections open without executable paths, launch arguments or manually typed model IDs.
  Native providers retain their defaults; API text models come from their actual catalogues.
- Missing installations, keys, endpoints and local models have distinct setup states.
  Detection does not submit messages, install runtimes or read private credential files.
- Discovery is part of the AI SDK and accepts host-defined provider recipes and search locations.
  Manual connection settings remain available.
- Agent-oriented defaults without arbitrary limits: model-maximum output, unlimited tool
  rounds, no turn deadline (CLI and API); `timeout_ms` only detects a silent connection.
  Remaining sizes are 256 MiB memory guards.
- Every profile publishes its conventional settings in `describe()` (`defaults`); Chat Lab
  pre-fills them in manual mode together with the detected installation.
- Discovery: npm Codex launched through its native binary, parallel probes, installed-but-stopped
  local services reported, `OLLAMA_HOST` and `ANTHROPIC_BASE_URL` normalised, llama.cpp on port
  8080. Automatic model selection is limited to local services.
- Fixes: an interrupted tool call is completed in history; a minimised UI no longer fails a turn;
  ACP resets after any mid-turn error; exact connected state after a failed restore; MCP HTTP
  legacy fallback; truncated OpenAI answers kept; MCP/settings secrets kept off the command line;
  messages too long for the Windows command line reported explicitly.

## 0.5.2

- English remains the application's default language. The local Markdown demonstration
  now follows the selected English or French language too.
- Chat Lab includes a **Help (F1)** action that opens its local manual in the selected language.
- A bilingual CyDoc manual ships with the application and can be opened directly from disk.
- The README follows the same English/French structure, with module, build and license information.

### Known issues

- Linux builds currently encounter a GCC error in a core test declaration.
- On macOS, the conversation-store test fails when the temporary directory has a symlinked ancestor.
  Full Linux/macOS distributions are not offered yet. The graphical example uses Win32/DX11.
- Provider availability depends on the installed CLI/API version, account and selected model.
  The included provider profiles do not guarantee authentication with every service.

### Current limits

- The SDK does not include models, provider CLIs, inference engines, GPU runtimes or drivers.
- Local dependency downloads verify host-supplied manifests; automatic archive extraction,
  GPU package catalogues and HTTP Range resumption remain to be implemented.
- Direct Chat Lab operation attachment is available for Codex and supported API transports;
  other CLIs use their native MCP settings. ACP filesystem/terminal services require host callbacks.
- MCP tools support stdio and authenticated loopback HTTP JSON. Persistent subscriptions,
  resources/prompts and progress notifications are not advertised.
- Android is deferred. C++ source compatibility is evolving; no cross-compiler ABI is promised.

Report reproducible problems through the [issue tracker](https://github.com/MrMybal/CyMCPSDK/issues).
No public binary release has been published for this version; build from source.

## Earlier versions

- **0.5.1:** bidirectional Chat Lab commands over MCP/SDK, Windows HTTP streaming fixes,
  corrected thinking-text display accounting and preserved failed-turn timings.
- **0.5.0:** optional CyUI chat module and Windows Chat Lab example.
- **0.4.1:** native Claude settings preserved and blocking login precheck removed.
- **0.4.0:** extensible provider profiles, conversations, attachments, private history and exact resume.
- **0.3.0:** native modular SDK foundation.
