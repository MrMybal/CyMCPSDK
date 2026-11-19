# Optional runtime facilities

CyMCPSDK 0.7.0 adds independent opt-in disk resources, performance monitoring, change subscriptions
and host-managed API context projection. No extra provider, model, graphics or external runtime is
required. See the bilingual [runtime options guide](../Documentation/Documentation.html#/en/runtime-options).

Local Windows MSVC checks cover:

- Progressive production and complete UTF-8 reconstruction of a disk-backed result larger than
  the configured RAM budget; binary/base64 contents; principal isolation.
- Byte quotas including unfinished concurrent producers, explicit overflow, producer failure,
  cancellation, TTL expiry and deletion of owned files/directories.
- Optional operation, MCP client/server and conversation measurements. Metadata-only private
  JSONL is flushed on orderly shutdown; argument/result/error text is absent.
- Bounded change journals, missed-event indication, automatic registry/resource events,
  MCP 2026 stream acknowledgment, subscription IDs, filters, cancellation and restart.
- HTTP SSE and stdio notifications invalidating positive-TTL caches before expiry, including
  resource deletion. Stdio serves commands with one ordinary worker while a stream is active.
- Full host history with a bounded provider view, pinned systems/current input, intact tool
  groups and incremental host summaries. No summary callback runs before older turns are omitted.
- Editing and checkpoint restore, input rejection without history loss, and active tool-turn
  overflow retaining an already executed mutation/result without retrying it.
- Chat Lab's existing SDK/MCP allow/deny flows with optional performance and notifications enabled.

The standard local Windows build has 18 suites. Minimal Core-only and stdio-only builds include
three Core suites. The optional official Python MCP client checks legacy stdio/HTTP interoperability
and standard resource links/chunks. New notifications use the explicitly negotiated MCP 2026
protocol; legacy peers continue using TTL/explicit refresh, without advertised subscription support.

Full transcripts still occupy host memory. Context budgets measure serialized message bytes,
excluding provider framing/tool definitions, rather than claiming an exact model token count.
Disk files may remain after abnormal termination. Histogram percentile fields are upper bounds;
bounded file queues expose dropped/error counts. Linux/macOS runtime validation of these new paths
remains pending; all checks run locally without GitHub Actions.
