---
id: integration
title: Integrate the C++ library
description: Link only the modules your host needs and keep provider work outside its UI thread.
section: SDK
order: 0
revision: 3
---

## Build requirements {#build}

Use CMake 3.24+, C++20 and system threads. Windows uses Visual Studio 2022 C++, Windows SDK,
and BCrypt. Linux/macOS use POSIX APIs. HTTP(S) uses libcurl on all three platforms.
Install libcurl development files with a TLS backend and set `CURL_ROOT` or
`CMAKE_PREFIX_PATH` for a custom installation. Windows Chat Lab's build script accepts
`-CurlRoot PATH`. Installed SDK consumers also need `CURL::libcurl`.
Current cross-platform distribution limits are listed in the source version notes.

Android can build the HTTP transport with an NDK-built libcurl for the selected ABI.
HTTPS also requires a TLS backend and a trusted CA bundle configured for that build.
The SDK needs a C++20 standard library with `std::jthread` and `std::stop_token`.
The complete Android application integration, including native CLI providers, remains deferred.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
cmake --install build --config Release --prefix install
```

An installed host uses the exported targets:

```cmake
find_package(CyMCPSDK 0.5 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE CyMCP::Core CyMCP::MCP CyMCP::AI)
```

Alternatively use `add_subdirectory(external/CyMCPSDK)`. Disable unneeded modules with
`CYMCP_BUILD_HTTP`, `CYMCP_BUILD_PLATFORM`, `CYMCP_BUILD_MCP`, `CYMCP_BUILD_AI` and
`CYMCP_BUILD_LOCAL`; CMake checks dependencies. `core-only` builds the operation core.

## Create a conversation {#providers}

```cpp
#include <cymcp/ai.hpp>

cymcp::ProviderRegistry providers;
cymcp::ProviderConfig config;
config.settings["base_url"] = "http://127.0.0.1:11434";
auto provider = providers.create("ollama", config);
```

Construct `Conversation(provider, model_id)` with a real installed model ID, then call
`connect()` and `send(text_block(prompt))`. These synchronous calls belong on a worker.
`send_async(executor, content, context)` returns a future; keep the executor/provider alive.
`CallContext.events` receives deltas and activity. Transfer them through an `EventQueue`
to the UI. `cancel()` requests cancellation; `reconnect()` is explicit.

## Embed the CyUI panel {#chat-ui}

`CYMCP_BUILD_CHAT_UI` and `CYMCP_BUILD_CHAT_APP` are off by default. The host supplies
`CyUI::DocumentViews`, or sets `CYMCP_CYUI_SOURCE_DIR` to a compatible CyUI checkout.
An installed consumer creates its CyUI target before requesting `COMPONENTS ChatUI`.
Use CyUI `14e1c6f` or later for the transcript selection and wrapping fixes.
Build the SDK and the host with the same CyUI revision.

```cpp
#include <cymcp/chat_ui.hpp>

// After the host has created its ImGui context and initialized CyUI:
cymcp::ChatController controller;
cymcp::ChatPanel panel(controller);

// Inside the host window, between NewFrame and Render:
panel.draw();
```

`ChatController::open(ChatConnection)` schedules the connection. `send`, `refresh_models`,
`save`, `disconnect` and `reconnect` also schedule worker operations; `poll` transfers results.
Use the controller and its state from one host UI thread. Destroy it before the ImGui context
and provider modules. Its destructor cancels and joins the worker.

`ChatPanelOptions` supplies file selection, link opening and cached document resources.
Without a link callback, the panel offers a link to copy. Set the host's initial CyUI locale
to `en` for English; the Windows example does so by default.
