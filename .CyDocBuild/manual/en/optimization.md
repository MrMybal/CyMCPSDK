---
id: optimization
title: On-demand tools, resources and grep
description: Keep every app command accessible while reducing catalogue and result traffic.
section: SDK
parent: integration
order: 2
revision: 2
---

## Discover without losing capabilities {#tools}

Optional [disk storage, performance logs, change subscriptions and API context windows](doc:runtime-options)
extend these facilities without changing their default behavior.

`Registry` remains the source of truth. Direct SDK calls and ordinary MCP tools preserve their
names, arguments, output schemas and policies. `describe()` still returns the complete catalogue.
Immutable `catalogue()` and schema-free `summaries()` snapshots are reused until `add`, `replace`
or `remove` changes the registry; `describe(name)` loads one full definition.

`ToolGateway` exposes five standard callable tools instead of every definition:

| Tool | Purpose |
| --- | --- |
| `cy_tools_search` | Search names/descriptions; omit query to browse every command. Follow nextCursor with the same query. |
| `cy_tools_describe` | Retrieve one complete input/output schema. |
| `cy_tools_call` | Invoke the original command with its arguments and the caller's context. |
| `cy_resources_read` | Read an application resource, manifest or chunk. |
| `cy_resources_grep` | Search registered text resources without loading their entire contents. |

Search and describe never execute app commands. Invocation still validates the original schemas
and applies the original policy, including explicit allow-all. No command is permanently hidden.
Search cursors become invalid after registry changes; restart discovery explicitly.

```cpp
#include <cymcp/mcp.hpp>
#include <cymcp/resources.hpp>

auto commands = std::make_shared<cymcp::Registry>(host_policy);
// Register all application operations on commands.
cymcp::ToolGateway gateway(commands);
cymcp::McpServerOptions options;
options.resources = gateway.resources();
cymcp::McpServer server(*gateway.tools(), "My application", options);
// Keep gateway/commands alive while serving. For an API: config.tools = gateway.tools().
```

API transports and Codex automatically choose this mode when constructed with more than
32 tools. `settings.tool_discovery_threshold` changes the threshold; `settings.deferred_tools=false`
keeps full definitions. Typed options expose `tool_discovery_threshold` (`size_t` maximum disables it).
An explicitly created gateway works with any tool-capable transport. Other native agents retain
their native MCP configuration; the SDK does not invent unsupported CLI tool injection.

## Read only the data needed {#resources}

`ResourceRegistry` belongs to `CyMCP::Core`; it also works without MCP or HTTP. Register a
`Resource` with URI, name, MIME type, byte size and a bounded read callback over an **immutable
host snapshot**. No filesystem or network is implicitly exposed. Resource reads have their own
policy (`resources.read`, arguments containing the requested URI), cancellation and ownership checks.

MCP advertises `resources/list`, `resources/read` and an empty template catalogue only when
`McpServerOptions.resources` is supplied. Listing resources does not read their contents.
Small contents are returned directly; larger contents return a JSON manifest
(`type: cymcp.resource.v1`) with `firstUri`, original MIME type, size and chunk size.
Read firstUri and follow `_meta["com.cymcp/chunk"].nextUri` until eof. Text chunks preserve UTF-8;
binary chunks use standard base64. Never treat an excerpt as a full result.

Gateway results over 16 KiB become private resource handles; MCP also includes a standard
`resource_link`. Read with native resources/read or cy_resources_read. Defaults are 32 KiB per
chunk, 64 MiB retained bytes, 128 results and five-minute retention; all are host-configurable.
Results are not listed publicly and are bound to `CallContext.principal`. The host must assign
separate principals for callers that must be isolated. Replacing a registered snapshot invalidates
its old chunk URIs. Expiry or LRU eviction returns ResourceNotFound; it never reruns a command.

A result larger than the retained byte budget remains inline without truncation or re-execution.
To keep arbitrarily large datasets bounded, expose them as host-backed snapshot resources or
paginated application commands from the start. The gateway cannot remove memory already
allocated by a handler returning a large JSON object. Ordinary unwrapped tools keep their original
structured output; the gateway does not violate their outputSchema by substituting a handle.

## Cache and compatibility {#cache}

MCP tools/list and resources/list have revision-bound cursors (64 entries per page by default).
`McpClient::list_tools()` still aggregates every page; `list_tools_page()` reads just one.
An absent nextCursor ends pagination; empty cursors are accepted from other implementations.
`get_tool(name)` uses advertised `cymcp/tools/get` when supported and falls back to full discovery
for older peers. The extension is optional; the five gateway tools use ordinary MCP tool calls.

Client caches are private per instance and keyed by method plus all parameters. Only discovery,
schema lookup and resource reads with positive server ttlMs are cached. Defaults: 256 entries,
16 MiB serialized data and a maximum 30-second TTL. Zero TTL or zero local limits disables caching.
Tools/call and errors are never cached. No background polling occurs. Changes may remain cached
until TTL: use clear_cache() for an immediate refresh after known external changes. Reconnect,
close and received catalogue/resource change notifications invalidate the cache. A tool error also
clears it, without retrying the operation. `cache_stats()` reports hits, misses and retained bytes.

## Fast text search {#grep}

Native cy_resources_grep scans registered text snapshots incrementally, with literal matching,
optional ASCII ignore-case, line numbers, byte offsets and readUri links. It returns at most 50
matching lines per page. Continue with the same text/ignore_case and nextCursor. Binary resources
and resources denied by policy are excluded. This does not traverse the filesystem.

For workspace files, `CyMCP::Platform` offers an optional **ripgrep** adapter:

```cpp
#include <cymcp/search.hpp>

cymcp::FileSearchOptions search_options;
search_options.root = host_selected_folder; // Explicit host/user choice.
// search_options.ripgrep = trusted_rg_executable; // Otherwise resolves rg from PATH.
cymcp::FileSearch search(search_options);
search.add_to(*commands); // workspace.grep and workspace.read; also discoverable through gateway.
```

The host supplies a trusted ripgrep executable separately; the SDK neither bundles nor downloads it.
Regex uses its default Rust engine. Arguments are passed directly without a shell or rg config.
`workspace.grep` accepts pattern, regex (default true), ignore_case, glob and include_ignored
(default false). Hidden files are included; Git metadata and symlinks are excluded. Default ignore
rules remain active unless include_ignored is enabled or an explicit file glob overrides them.
This root scope is not an OS sandbox
against another process changing the filesystem concurrently; use host isolation for untrusted trees.

Pages contain up to 100 matching lines by default, path/line/byteOffset and bounded UTF-8 excerpts.
`truncated` and lineBytes identify longer lines; workspace.read retrieves the full text in ranges.
Continue a search with **cursor only**; each returned cursor replaces the previous one. Search
processes are owned, cancellable, limited to eight concurrent searches and expire after 60 idle
seconds. Non-UTF-8 matches skipped by this text adapter are counted in skippedNonUtf8.
Process/line limits fail explicitly instead of silently discarding results.

`workspace.read` accepts a relative path, offset, length (1–65536 bytes) and optional revision.
Follow nextOffset with the same revision to detect file changes. Reads preserve UTF-8 boundaries;
binary/non-UTF-8 content is rejected. No writes or directories outside the chosen root are exposed.
The registry's host policy still decides whether either command may be invoked.

## Try in Chat Lab {#example}

The default two counter commands stay unchanged. In a private JSON configuration supplied with
`--config`, enable compact exposure and optionally choose a read-only workspace:

```json
{
  "app_commands": {
    "deferred_tools": true,
    "workspace": {"root": "./sample-workspace"}
  }
}
```

Choose an existing folder. Optional workspace.ripgrep specifies its trusted executable. A workspace
is never configured automatically. Compact exposure applies to MCP and direct supported AI tools;
the JSON SDK endpoint retains original command names. Both paths share the same registry/policy.

## Protocol references {#references}

The implementation follows MCP [tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools),
[resources](https://modelcontextprotocol.io/specification/2026-07-28/server/resources),
[caching](https://modelcontextprotocol.io/specification/2026-07-28/server/utilities/caching) and
[pagination](https://modelcontextprotocol.io/specification/2026-07-28/server/utilities/pagination).
Chunk manifests and targeted lookup are optional CyMCPSDK conventions, not mandatory MCP methods.
