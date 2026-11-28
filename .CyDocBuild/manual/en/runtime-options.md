---
id: runtime-options
title: Optional runtime optimizations
description: Configure disk-backed volumes, metadata-only performance logs, change subscriptions and API context windows.
section: Develop with the SDK
order: 25
revision: 2
parent: optimization
previous: optimization#resources
next:
---

## Independent options {#options}

All four facilities are opt-in. Core supplies resources, disk spooling, change journals and
performance monitoring without graphical or AI dependencies. ContextWindow belongs to AI.
There are no added external runtime dependencies. No model is downloaded or launched to summarize
history. Unconfigured applications retain full API history, memory-only retained results and TTL caches.

## Produce large resources incrementally {#disk}

```cpp
#include <cymcp/resources.hpp>

cymcp::ResourceOptions storage;
storage.disk_directory = host_selected_cache_folder;
storage.max_stored_bytes = 8 * 1024 * 1024;
storage.spool_threshold = 1024 * 1024;
storage.max_disk_bytes = 1024ULL * 1024 * 1024;
auto resources = std::make_shared<cymcp::ResourceRegistry>(host_resource_policy, storage);
auto handle = resources->store_stream(
    [snapshot_blocks](std::ostream& output, const cymcp::CallContext& context) {
        // Produce bounded blocks from an immutable application snapshot.
        for (const auto& block : snapshot_blocks) {
            context.cancellation.check();
            output.write(block.data(), block.size());
        }
    }, "text/plain", caller_context);
```

The host chooses the parent folder. The SDK creates its own random, owner-only child directory
and private files; it does not change the parent's permissions. Above the threshold, the producer
spills to disk. Keep this private cache outside directories exposed through other file tools.
The disk quota counts retained files, in-flight writers and pinned readers. Older
retained disk results can be evicted; occupied active quotas fail explicitly. RAM retention and
disk quotas are separate. Concurrent producers run once and publish a handle only after success.

Read the handle through the existing manifest/chunk APIs. For binary output pass `true` as the
fourth store_stream argument; chunk contents use base64. UTF-8 text is read at character boundaries.
Failures, cancellation, expiry and normal destruction release owned files. The optional disk
janitor checks expiry approximately once per second; an active reader can pin its immutable file.
Abnormal process termination can leave an orphaned private directory for the host to clean up.
The SDK never scans/deletes unrelated directories or automatically resumes an interrupted producer.

`storage_stats()` reports retained RAM/disk bytes and result count, without local paths.
ToolGateway uses `store_json` to serialize large JSON output directly into this sink, avoiding a
second complete serialized string. The handler still owns its original JSON allocation. For much
larger data, return a handle from a streaming producer directly. If a gateway result exceeds all
configured storage budgets, its legacy complete-inline fallback remains; direct stream APIs fail
ResourceTooLarge without truncation. TTL and LRU retention are temporary, not archival storage.

## Measure without recording contents {#performance}

```cpp
#include <cymcp/performance.hpp>

cymcp::PerformanceOptions options;
options.jsonl_file = host_selected_private_log_file; // omit for memory only
auto performance = std::make_shared<cymcp::PerformanceMonitor>(options);
commands->set_performance_monitor(performance);
// Or set CallContext.performance, McpServerOptions.performance,
// McpClientOptions.performance or ChatConnection.performance.
auto totals = performance->snapshot();
auto recent_records = performance->drain();
```

Records contain timestamp, stage/name, status, elapsed_ms, input_bytes, output_bytes and cache_hit.
They omit arguments, response contents, error explanations, principals, credentials and request IDs.
Stages distinguish operation, mcp.server, mcp.client and conversation. Conversation output_bytes
counts final UTF-8 text; protocol/operation sizes count serialized JSON bytes. These are not token counts.
Byte measurement walks JSON without allocating its serialized copy and is performed only when enabled.

Snapshots retain aggregate call/error/cache counts, total bytes, mean/max latency and histogram
p50_upper_ms/p95_upper_ms (bucket upper bounds, not exact percentiles). Labels and recent rows
are bounded; overflowing labels aggregate into other. The asynchronous file queue is bounded;
dropped_file_records and file_errors expose file delivery failures. Rows drained by an application
remain included in aggregate statistics. A supplied log destination is a new private output file;
the host owns rotation and retention. Instrumentation does not cache/retry operations or alter rights.

## Subscribe to changes {#notifications}

```cpp
#include <cymcp/changes.hpp>
#include <cymcp/mcp.hpp>

auto changes = std::make_shared<cymcp::ChangeFeed>();
commands->set_change_feed(changes);
resources->set_change_feed(changes);
cymcp::McpServerOptions server_options;
server_options.resources = resources;
server_options.changes = changes;
cymcp::McpServer server(*commands, "My app", server_options);
// Client: after connect(), explicitly opt into its owned notification reader.
client.start_notifications({{"toolsListChanged", true},
    {"resourcesListChanged", true},
    {"resourceSubscriptions", cymcp::Json::array({"app://state"})}});
```

Registry add/replace/remove publishes tool-list changes. Registered resource mutations publish
resource-list and URI changes. For external host changes, publish `resource` with its URI on the
same journal, or replace the immutable snapshot. Notifications never expose resource contents.
Resource URIs are authorized on subscription; subsequent deletion/revocation still invalidates
previously authorized URIs, and actual reads always apply current policy. Private generated results
stay outside public catalogues. A bounded journal reports missed events so subscriptions invalidate
broadly instead of silently retaining stale data.

The server implements MCP 2026 subscriptions/listen, first acknowledgment, per-subscription IDs,
requested-type filtering and cancellation. HTTP uses a streamed SSE POST; LocalHttpServer supplies
the channel automatically. Custom transports must provide CallContext.notifications (null means
heartbeat). Stdio uses a separate subscriber worker pool, so one ordinary worker remains usable.
Defaults accept two active streams and 64 URIs per stream; configure max_subscriptions and
max_resource_subscriptions on McpServerOptions to change these bounds. HTTP streams occupy
HTTP workers: provide more workers than active streams (default is four). Shutdown cancels streams.

The client owns a reader only after start_notifications. Incoming changes invalidate its caches
before the next access, including changes received while a previous request was in flight.
Use drain_notifications for host/UI integration and notification_status for the accepted filter,
running flag and terminal error code. No host callbacks run on reader threads. stop_notifications
cancels/joins the reader; it never retries a command. A failed stream invalidates caches and
reports failure; reconnection is explicit. Legacy 2025 peers retain TTL/clear_cache behavior and
return Unsupported for this new subscription API. Unenabled servers advertise no subscription support.

## Bound API context while retaining full history {#context}

```cpp
cymcp::ContextOptions context;
context.max_bytes = 128 * 1024; // serialized messages, not a model token-window claim
context.recent_turns = 8;
conversation.configure_context(context);
```

Configure an idle Conversation before use or restore. Default max_bytes=0 disables projection.
The sent view retains at most recent_turns complete user turns within its byte budget. System
instructions and the active user turn are pinned. Tool calls/results are never split across turns.
An assistant reference marker explains omitted history. Full history, transcripts and checkpoints
remain unchanged; the host can expose bounded history-reading commands if the AI needs older details.
This feature bounds the sent view, not the complete host transcript's RAM usage.

Optional ContextOptions.summarize is a host callback receiving previous_summary, newly omitted
messages and max_bytes. It runs only when necessary, never launches an implicit AI request and
must return bounded UTF-8 text. Its summary is assistant reference material, not a replacement for
system instructions. Edits, deletions and restore reset the incremental summary. Context options
and callbacks are host configuration, not serialized checkpoint data.

Oversized pinned instructions/current input fail ContextTooLarge before generation. If a live tool
turn exceeds the budget, its already executed calls/results are retained and never replayed; the
next user turn can recover with a new projection. Provider loops enforce this message budget
between tool rounds. Tool schemas and provider framing add overhead outside the message budget.
Native CLI agents keep their own context behavior; configuring their SDK context window explicitly
returns Unsupported. ChatController applies ChatConnection.context only to host-history APIs.

## Chat Lab configuration {#chat-lab}

```json
{
  "context": {"max_bytes": 131072, "recent_turns": 8},
  "performance": {"enabled": true},
  "app_commands": {
    "notifications": true,
    "storage": {"directory": "./private-cache", "disk_bytes": 1073741824}
  }
}
```

Merge selected fields into an existing private provider configuration. Each is independent.
Performance rows appear in Logs; optional performance.file chooses a separate private JSONL file.
Notification mode also exposes app://chat-lab/counter, updated by both SDK and MCP counter writes.
Existing configurations and the original two-command interface continue to work.
