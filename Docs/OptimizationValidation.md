# On-demand tools, resources and search

Version 0.6.0 adds optional compact discovery while retaining the complete original operation
registry and permissions. See the bilingual [integration guide](../Documentation/Documentation.html#/en/optimization).

## Validation

The Windows MSVC Release build passes all 15 local CTest suites, including the actual Win32/DX11
Chat Lab application. Core-only and stdio-only builds each pass their two enabled suites.
Installed SDK and ChatUI consumers configure, build and run successfully.

Focused fixtures verify:

- Every command remains searchable, its complete schema can be fetched independently and its
  original handler executes once with the same validation, cancellation and policy.
- Immutable catalogue snapshots are reused and invalidated on add/replace/remove. Old pagination
  cursors are rejected after a catalogue revision changes.
- MCP works over HTTP and stdio; ordinary full discovery still aggregates all pages.
  Targeted schema lookup avoids fetching unrelated schemas when the peer advertises support.
- A result with more than 200,000 bytes is transferred as a resource handle/link and reconstructed
  byte for byte, including multibyte UTF-8 characters. Binary resources use base64.
- Private handles are isolated by principal, not listed, and subject to byte/count/TTL eviction.
  Oversized retained results stay complete inline without executing the handler twice.
- Read caches reuse positive-TTL results, refetch after expiration/clear and bypass zero-TTL data.
  Tool execution results are never cached or replayed.
- Native resource grep finds all 120 matching lines across tiny UTF-8 chunks and multiple pages.
- Optional filesystem grep finds all 120 lines through ripgrep cursor pages. Regex, literal,
  case-insensitive matching, globs, ignore-rule overrides, Git metadata exclusion, non-UTF-8
  accounting, cancellation, private cursors and stale cursor rejection are covered.
- Bounded workspace reads reconstruct the complete UTF-8 text. Traversal, absolute paths,
  changed revisions and malformed patterns are rejected.
- Five deterministic API transports (Chat Completions, Responses, Messages, Ollama and OpenRouter)
  complete search → describe → call → answer using only five bootstrap definitions. Each invokes
  the original command once. Explicit full-definition mode remains available.
- Chat Lab serves its own resources and accepts MCP reads/writes in ordinary and compact modes,
  with both denied and allowed mutations. The direct JSON SDK route continues using original names.
- The official Python MCP client accepts legacy stdio/HTTP discovery and ordinary tool calls,
  plus paged gateway tools, standard resource links and complete resource chunk reads.
  Python is a development-only interop check, not a runtime dependency.

For the synthetic 100-command fixture (long descriptions), serialized definitions measure
**89,291 bytes full versus 1,904 bytes compact** before selected schemas are requested.
This measures initial catalogue traffic, not model latency or a guarantee for every application.

## Run locally

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The resources suite needs only Core. The MCP optimization suite requires HTTP/MCP; its optional
filesystem checks use a separately installed ripgrep in PATH and report a skip if absent. To run
optional official-client interop in a development environment with the Python MCP SDK:

```sh
python tests/mcp_official.py build/cymcp_example --gateway-server build/cymcp_mcp_optimization_tests
```

On Windows add `.exe` to executable paths. Chat Lab validation is built with the optional chat app.

## Boundaries

All performance-related limits are host-configurable. Large original JSON results still allocate
in the handler; use host-backed snapshot resources for genuinely unbounded datasets. The result
store is temporary, and expired handles never cause an automatic retry of a mutation.

The text adapters do not expose binary workspace contents. Native resource grep uses literal
matching with ASCII case folding; the optional ripgrep backend supplies regex and file selection.
Filesystem root checks and no-follow behavior do not replace host OS isolation for a concurrently
modified, untrusted tree. Assign distinct principals when private result isolation is required.

This feature was validated on Windows locally. Linux/macOS validation of these new paths remains
pending. Native MCP subscriptions/list-change emission are not advertised; the client uses TTL,
explicit invalidation and any change notifications it receives from other peers.
