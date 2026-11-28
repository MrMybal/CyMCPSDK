---
id: overview
title: CyMCPSDK
description: Add AI conversations and application tools to a native C++ application.
section: Start here
order: 0
revision: 4
previous:
next: quickstart
---

## Choose your workflow {#workflows}

CyMCPSDK is an MIT C++20 library for application developers. Register application
operations once, expose them through a direct SDK or MCP, and connect the application
to CLI agents, APIs or a local model service. The optional CyUI panel displays conversations.

| Your task | Start here |
| --- | --- |
| Try the Windows application | [Quickstart](doc:quickstart) |
| Configure a chat and use its tools | [Chat Lab](doc:chat-lab) |
| Add the library to your application | [C++ integration](doc:integration) |
| Let an AI or another app call your operations | [Application operations](doc:operations) |
| Discover tools on demand, read chunks and search text | [Optimization and grep](doc:optimization) |
| Choose access rules | [Permissions](doc:permissions) |
| Use an installed local engine | [Local engines](doc:local-engines) |

```diagram
{"type":"architecture","title":"From the host application to AI and external tools","description":"The host owns its operations and permissions. The registry exposes them to SDK and MCP callers; a provider runs conversations and can call the permitted tools.","nodes":[{"id":"host","label":"Host application","x":20,"y":20,"width":190},{"id":"registry","label":"Registry + policy","x":20,"y":165,"width":190},{"id":"mcp","label":"SDK / MCP callers","x":310,"y":165,"width":190},{"id":"provider","label":"AI provider + tools","x":310,"y":20,"width":190}],"edges":[{"from":"host","to":"registry"},{"from":"mcp","to":"registry"},{"from":"host","to":"provider"},{"from":"provider","to":"registry"}]}
```

The table below maps these responsibilities to linkable CMake targets. For a first integration,
follow [build → operations → permissions](doc:integration#build).

## Modules {#modules}

| CMake target | Role |
| --- | --- |
| `CyMCP::Core` | Operation registry, schemas, policies, cancellation and bounded workers |
| `CyMCP::Platform` | Native child processes and structured arguments |
| `CyMCP::Http` | HTTP(S) client, streaming and authenticated loopback server |
| `CyMCP::MCP` | MCP tool server/client over stdio or local HTTP JSON |
| `CyMCP::AI` | Provider profiles, sessions, conversations and tool exchanges |
| `CyMCP::Local` | Engine supervision and verified dependency downloads |
| `CyMCP::ChatUI` | Optional CyUI controller and Markdown chat panel |
| `CyMCP::SDK` | SDK modules without the graphical dependency |

## Scope {#scope}

The library does not bundle a model, AI CLI, inference engine, CUDA runtime or driver.
Install and configure the provider you choose. Native accounts and service terms still apply.
See [provider choices](doc:providers) and [licenses](doc:licenses).

The Windows example uses Win32/DX11. ChatUI uses the host application's CyUI/ImGui backend.
Linux also has a windowed source example using SDL3. macOS packaging remains in progress.
Android HTTP builds require an NDK-built libcurl and TLS configuration; full Android application
integration remains deferred. See [build requirements](doc:integration#build).

## Read this manual {#navigation}

Use the page tree or search field; **Ctrl+K** opens search. Search results link directly
to matching sections. The tree groups **Start here**, **Use Chat Lab**, **Develop with the SDK**
and **Help**. Breadcrumbs locate the current page; **Previous/Next** follows its learning path.

Switch between English and French to keep the same page and section. **My space** holds
bookmarks and notes in this browser; their persistence depends on local browser storage.
Screenshots and diagrams enlarge inside the reader; **Escape** closes the enlarged view.
The tool-discovery [graph](doc:optimization#tool-map) also offers a searchable text view.

Open the complete `Documentation/Documentation.html` export directly from disk. In Chat Lab,
**F1** opens Help; **Open full manual** opens the guide in the selected language.
