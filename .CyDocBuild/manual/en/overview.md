---
id: overview
title: CyMCPSDK
description: Add AI conversations and application tools to a native C++ application.
section: Guides
order: 0
revision: 1
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
| Choose access rules | [Permissions](doc:permissions) |
| Use an installed local engine | [Local engines](doc:local-engines) |

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
Linux/macOS distribution work is still in progress; Android is deferred.

## Read this manual {#navigation}

Use the page tree or search field; **Ctrl+K** opens search. Switch between English and
French in the reader. The complete `Documentation` folder opens directly from disk.
In Chat Lab, **Help (F1)** opens the chat guide in the selected application language.
