---
id: operations
title: Expose application operations
description: Share one typed registry between native calls, SDK JSON, MCP and supported AI tools.
section: SDK
parent: integration
order: 1
revision: 1
---

## Define a registry {#registry}

An `Operation` has a portable name, a description, object input/output schemas, a read-only
flag and a handler. The `Registry` validates inputs, applies its policy and checks declared outputs.
The read-only flag describes the operation; it does not override a denied policy.

```cpp
#include <cymcp/core.hpp>

cymcp::Registry commands(cymcp::Policy::allow_all());
commands.add({"get_counter", "Read the demonstration counter.",
    {{"type", "object"}, {"additionalProperties", false}},
    {{"type", "object"},
     {"properties", {{"value", {{"type", "integer"}}}}},
     {"required", {"value"}}, {"additionalProperties", false}},
    true,
    [](const auto&, const auto&) { return cymcp::Json{{"value", 42}}; }});
auto result = commands.invoke("get_counter", cymcp::Json::object());
```

This example explicitly permits its harmless read operation. For an editable domain, use the
host's policy and synchronize access to its objects. MCP handlers can run concurrently; marshal
UI-bound mutations onto the host's event loop and keep captured state alive.

## Direct SDK JSON {#sdk}

`dispatch_app(registry, request, context)` accepts these application messages:

```json
{"method":"describe"}
```

```json
{"method":"invoke","name":"get_counter","arguments":{}}
```

The example CLI supports `describe`, `invoke`, and application JSONL on stdin. Application JSONL
and MCP JSON-RPC are separate protocols. In Chat Lab, the same requests go to `/v1` on its enabled
local server, with `Content-Type: application/json` and the Bearer token.

## MCP server and client {#mcp}

`McpServer(registry)` exposes operations as MCP tools. `serve_stdio(input, output)` serves a CLI
connection; `handle(request, context)` integrates with an HTTP endpoint. Keep the registry and
server alive for the entire connection.

`McpClient(ProcessOptions)` connects over stdio; `McpClient(url, token)` connects to local HTTP
when HTTP support is built. Call `connect`, `list_tools` and `call_tool(name, arguments)`.
Inspect an MCP result's `isError` before treating its `structuredContent` as successful output.

To let an AI in app A use app B, connect a client to B, register explicit forwarding operations
in A and attach that registry to a supported provider. Preserve B's schema and enforce A's policy
before forwarding. Do not let model output register arbitrary new operations.

## Attach tools to a conversation {#tools}

Supply a shared registry in `ProviderConfig.tools` for supported API providers or Codex's dynamic
tools. The registry owns the policy for actual app operations. For other CLIs, expose MCP and pass
their provider-native MCP configuration. ACP uses `settings.mcp_servers` as an ACP-format array.
See [permissions](doc:permissions) before exposing mutations.
