CyMCPSDK Chat Lab 0.5.0 - Windows x64

Launch cymcp_chat_app.exe. Choose English or French under Appearance.
Demo / Markdown uses deterministic local text and needs no AI account or model.
Select a provider, enter a model ID when required, then Connect / New chat.
Refresh model catalogue queries the connected provider. A selected model applies
to the next connection. Enter inserts a line; Ctrl+Enter sends a message.

Native CLIs use their existing environment/account. If not found on PATH, expand
Executable and choose the actual .exe (or node.exe with its script as a JSON
argument). Shell wrappers such as .cmd and .ps1 are not launched by this SDK.
API keys can come from an environment variable or the masked memory-only field.
Advanced settings accepts the same settings object as the SDK JSON examples.

Allow all actions explicitly enables the provider's unrestricted permission mode.
The application does not advertise filesystem or terminal services to ACP agents.
Save history is optional. Select a folder and a simple ID, then connect. Turns are
saved automatically; Restore saved chat loads that exact history/session.
Native resume requires the selected CLI's support and the same model/provider.
History contains private messages and attachments. API keys are not stored in it.

Optional arguments:
  --config PATH.json             Load an SDK chat configuration; connect manually.
  --language fr                  Start with French UI (default: en).
  --theme navy                   Select a CyUI theme.
  --smoke --capture PATH.png      Hidden local demo test, then quit.
Captures always use Demo and cannot be combined with --config.
Non-secret configuration examples are included in configs/.

Prerequisites: Windows 10/11 x64, Microsoft Visual C++ 2015-2022 x64 runtime,
and a DX11-capable graphics stack. WARP software rendering is used as fallback.
No Python, browser engine, AI CLI, inference engine, model or CUDA is bundled.
The panel is reusable through the optional CyMCP::ChatUI CMake target.
Keep LICENSE, THIRD_PARTY_NOTICES.md and licenses/ with redistributed copies.
