#pragma once
namespace cymcp::detail {
// Internal consumers may finish a bounded SSE response after its matching RPC
// frame, retaining the HTTP status/headers without waiting for the stream to close.
struct HttpStreamComplete {};
} // namespace cymcp::detail
