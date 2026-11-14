CyMCPSDK Chat Lab 0.5.1 - Windows x64

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

App commands exposes get_counter and set_counter. Allow counter changes permits
writes; AI may use app commands attaches them on the next Codex or API connection.
Other CLIs use their own native MCP configuration; direct attachment is Unsupported.
Expose local MCP starts an authenticated loopback endpoint. Copy MCP token copies
its private Bearer token for another client. SDK JSON commands use /v1 on that port.
The demonstration commands only access the counter, with no file or system access.
All three app_commands configuration options are false by default.

Optional arguments:
  --config PATH.json             Load an SDK chat configuration; connect manually.
  --language fr                  Start with French UI (default: en).
  --theme navy                   Select a CyUI theme.
  --smoke --capture PATH.png      Hidden local demo test, then quit.
  --exercise PLAN.json --report REPORT.json
                                 Run a scenario in the real window, then quit.
  --hidden                       Keep the test window hidden.
  --capture PATH.png             Capture the final scenario frame.
Smoke captures always use Demo. Configured captures require --exercise.
An exercise may perform actual generations with the configured provider/account.
Reports exclude message text, secrets and exact session IDs. Plans, captures and
optional histories can contain private messages. See Docs/ChatUI.md in the source.
Non-secret configuration examples are included in configs/.

Prerequisites: Windows 10/11 x64, Microsoft Visual C++ 2015-2022 x64 runtime,
and a DX11-capable graphics stack. WARP software rendering is used as fallback.
No Python, browser engine, AI CLI, inference engine, model or CUDA is bundled.
The panel is reusable through the optional CyMCP::ChatUI CMake target.
Keep LICENSE, THIRD_PARTY_NOTICES.md and licenses/ with redistributed copies.
