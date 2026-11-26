CyMCPSDK Chat Lab 0.7.5 - Windows / Linux

Windows: launch cymcp_chat_app.exe. Linux: launch ./cymcp_chat_app.
Choose English or French under Appearance.
Chat opens directly. Settings contains Providers, Local MCP, Logs and Help. Close returns to the draft.
The Settings gear is at the bottom right. The status is next to the provider and model.
Click the current Codex model in the footer to choose another for the next message.
Search provider cards, edit their own connection fields, then Connect / New chat.
Double-click a card to connect. Switching providers retains settings in memory.
Logs shows timestamped stages, errors and durations. Copy log copies diagnostics;
Open log file opens the private background-written session log. Message bodies,
API keys and native session IDs are excluded. Hover the filename for its full path.
English is the default, including the local Markdown demonstration.
F1 opens Help. Open full manual opens the offline manual in the selected language.
Keep the complete Documentation/ directory beside the executable.
Automatic connection is enabled at startup. Installed native CLIs and supported
npm packages, environment API keys and running local model services are detected
in the background. The first available connection opens without sending a message.
Click another detected connection to use it. CLI accounts and default models are
reused; local services choose from their actual catalogue. Remote APIs show their
catalogue and wait for your model choice. No manual paths are needed
for detected installations. Missing prerequisites are listed under Other providers.
Manual settings remain available in Providers alongside Automatic connection.
Demo / Markdown uses deterministic local text and needs no AI account or model.
Refresh model catalogue queries the connected provider. A selected model applies
to the next connection. Enter sends a message; Shift+Enter inserts a line. Ctrl+Enter also sends.

Native CLIs use their existing environment/account. For an unusual installation, expand
Executable and choose the actual native executable (or node with its script as a
JSON argument). Shell wrappers such as .cmd and .ps1 are not launched by this SDK.
API keys can come from an environment variable or the masked memory-only field.
Advanced settings accepts the same settings object as the SDK JSON examples.
When a native provider needs authentication, Sign in opens its own login in a console;
complete it and reconnect. Manual provider selection pre-fills conventional settings.

Allow all actions explicitly enables the provider's unrestricted permission mode.
The application does not advertise filesystem or terminal services to ACP agents.
Save history is optional. Select a folder and a simple ID, then connect. Turns are
saved automatically; Restore saved chat loads that exact history/session.
Native resume requires the selected CLI's support and the same model/provider.
History contains private messages and attachments. API keys are not stored in it.

App commands exposes get_counter and set_counter. AI may use app commands attaches
them on the next Codex or API connection.
Other CLIs use their own native MCP configuration; direct attachment is Unsupported.
In Settings, open Local MCP, choose an MCP access profile and use Expose local MCP to start on 127.0.0.1.
Free (selected without an app_commands section) allows reads and writes without a token.
Local controlled allows reads without a token. Token only requires a token for reads
and writes. Protected requires a token and allows reads. Use Copy MCP address and,
when required, Copy MCP token. Any program on this computer can call permitted
commands on /mcp or /v1 when the selected profile allows address-only access.
In JSON set app_commands.access_profile to free, local_controlled, token_only or
protected. An app_commands section without a profile keeps its legacy settings. Set app_commands.mcp_port
to a fixed port to reuse the address. The default 0 selects a free port.
The demonstration commands only access the counter, with no file or system access.
While enabled, the server publishes its name, version and token requirement at
/.well-known/cymcp for local hubs. The advertisement excludes tokens and tools.
It is available in Free and protected profiles; the hub still finds the listening port.

Optional arguments:
  --config PATH.json             Load an SDK chat configuration (manual by default).
                                 Set "automatic": true to enable discovery.
  --log-file PATH.log            Choose the private diagnostic file for this run.
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

Windows prerequisites: Windows 10/11 x64, Microsoft Visual C++ 2015-2022 x64 runtime,
and a DX11-capable graphics stack. WARP software rendering is used as fallback.
Linux prerequisites: a graphical X11 or Wayland session, libcurl with TLS and
the system libraries required by the selected SDL3 build. A build made on
Ubuntu 24.04 targets Ubuntu 24.04 or a compatible newer system, on the same CPU architecture.
SDL3 may be linked statically or supplied by the host. Use SDL_RENDER_DRIVER=software
when the VM's graphics driver cannot provide accelerated rendering.
Native file dialogs require a desktop portal or zenity; sign-in uses
x-terminal-emulator or xterm. Opening links and the manual requires xdg-open
and an associated browser. Histories and logs use XDG_DATA_HOME, or ~/.local/share.
No Python, browser engine, AI CLI, inference engine, model or CUDA is bundled.
The panel is reusable through the optional CyMCP::ChatUI CMake target.
Keep LICENSE, THIRD_PARTY_NOTICES.md and licenses/ with redistributed copies.
