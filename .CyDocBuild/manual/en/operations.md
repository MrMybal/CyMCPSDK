---
id: operations
title: Expose application operations
description: Share one typed registry between native calls, SDK JSON, MCP and supported AI tools.
section: Develop with the SDK
order: 21
revision: 4
parent: integration
previous: integration#build
next: permissions#policy
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
local server, with `Content-Type: application/json` and a Bearer token when its access profile requires one.

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

## Local MCP discovery {#discovery}

An HTTP host can publish its identity for a local hub with `LocalMcpEndpoint`:

```cpp
cymcp::McpServer mcp(commands, "My application");
cymcp::LocalMcpEndpoint identity;
identity.name = "My application";
identity.version = cymcp::version;
cymcp::LocalHttpServer server(cymcp::LocalHttpServer::UnauthenticatedLoopback{},
    [&](const auto& message, const auto& context) { return mcp.handle(message, context); },
    4, cymcp::provider_message_limit, identity);
const int port = server.start();
```

This example explicitly opens MCP to local callers. The token and authenticator constructors
accept the same final argument. Existing constructors keep their behavior without this argument.
`GET /.well-known/cymcp` publishes only the configured name, version, `/mcp` path and
`requiresToken` flag, without a token. Origin and loopback Host checks still apply.
Chat Lab and the headless HTTP example publish this identity in all access profiles.

The advertisement is a CyMCPSDK extension, read by a hub that probes local listeners. It does
not broadcast over the LAN or register with a central service. `windows_mcp_listeners(available)`
lists actual Windows loopback/wildcard listeners and their process IDs. On other platforms it
returns an empty list and `available=false`; the hub supplies its own listener inventory.

`probe_local_mcp(url, cancellation, timeout)` works on Windows and Linux. It checks a loopback
URL using modern discovery, falling back to legacy initialization when needed. It handles JSON
and SSE, closes temporary legacy sessions when time permits, and never lists or calls tools.
The positive timeout is one total budget, 800 ms by default; responses are limited to 64 KiB.
Caller cancellation propagates. A generic 401/403 remains an unnamed authentication candidate;
a protected MCP is identified only when it publishes valid public identity metadata.

For a separate MCP handler, set `identity.handler`. `identity.requires_token` can open `/mcp`
while `/v1` retains its original authentication. Requiring a token without an authenticator rejects
MCP calls. `identity.enabled` can hide both `/mcp` and its advertisement without disabling `/v1`.
These callbacks execute on HTTP workers; synchronize mutable host state. Registry permissions
still apply independently of transport authentication.

## Attach tools to a conversation {#tools}

Supply a shared registry in `ProviderConfig.tools` for supported API providers or Codex's dynamic
tools. The registry owns the policy for actual app operations. For other CLIs, expose MCP and pass
their provider-native MCP configuration. ACP uses `settings.mcp_servers` as an ACP-format array.
See [permissions](doc:permissions) before exposing mutations.

For large registries, see [on-demand tools, resources and grep](doc:optimization). These features
preserve the original commands and policy while reducing what is loaded into an AI's context.

## Follow one request {#request-flow}

This sequence applies to an invocation through the registry. A denied policy or invalid input
stops before the handler. Transport authentication is checked separately, before invocation.

```diagram
{"type":"sequence","title":"One operation, one permission check at invocation","description":"The registry validates the request and applies the host policy before calling the handler. It checks the declared output before returning a successful result.","actors":[{"id":"caller","label":"SDK / MCP / AI caller"},{"id":"registry","label":"Registry + policy"},{"id":"handler","label":"Host handler"}],"messages":[{"from":"caller","to":"registry","label":"Name + arguments"},{"from":"registry","to":"handler","label":"Validated, permitted call"},{"from":"handler","to":"registry","label":"Operation result","reply":true},{"from":"registry","to":"caller","label":"Schema-checked result","reply":true}]}
```

For a first end-to-end exercise, register the read-only `get_counter` operation above, expose it
through `McpServer`, then call it from `McpClient`. **Expected result:** the client receives
`{"value":42}` without `isError`. Change the host policy to deny the operation and repeat the read:
the call must be rejected before the handler runs. See [permissions](doc:permissions#policy).
