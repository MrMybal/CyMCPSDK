#pragma once
#include "process.hpp"
#include "resources.hpp"
#ifdef CYMCP_HAS_HTTP
#include "http.hpp"
#endif
#include <istream>
#include <optional>
#include <ostream>
namespace cymcp {
struct McpServerOptions {
    std::size_t page_size = 64;
    std::chrono::milliseconds cache_ttl{30000};
    std::shared_ptr<ResourceRegistry> resources;
};
struct McpClientOptions {
    std::size_t cache_entries = 256, cache_bytes = 16 * 1024 * 1024;
    std::chrono::milliseconds max_cache_ttl{30000};
};
struct McpCacheStats {
    std::uint64_t hits = 0, misses = 0;
    std::size_t entries = 0, bytes = 0;
};
class McpServer {
    const Registry& registry_;
    std::string name_;
    std::mutex mutex_;
    bool initialized_ = false;
    McpServerOptions options_;
    std::shared_ptr<const Json> catalogue_;
    std::string catalogue_generation_;
    using RequestKey = std::pair<std::pair<std::string, std::string>, std::string>;
    std::map<RequestKey, Cancellation> active_;
    struct ActiveCall;
    std::shared_ptr<ActiveCall> reserve(const Json&, const CallContext&, Json& error);
    Json dispatch(const Json&, const CallContext&);
    Json tool_page(const Json&);

  public:
    explicit McpServer(const Registry&, std::string name = "CyMCPSDK", McpServerOptions = {});
    Json handle(const Json&, const CallContext& = {});
    void serve_stdio(std::istream&, std::ostream&, std::size_t workers = 4);
    void cancel_all();
};
class McpClient {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit McpClient(ProcessOptions, McpClientOptions = {});
#ifdef CYMCP_HAS_HTTP
    McpClient(std::string url, std::string token, McpClientOptions = {});
#endif
    ~McpClient();
    void connect(const Cancellation& = {});
    Json list_tools(const Cancellation& = {});
    Json list_tools_page(std::optional<std::string> cursor = std::nullopt,
                         const Cancellation& = {});
    Json get_tool(const std::string&, const Cancellation& = {});
    Json list_resources(const Cancellation& = {});
    Json read_resource(const std::string& uri, const Cancellation& = {});
    void clear_cache();
    McpCacheStats cache_stats() const;
    Json call_tool(const std::string&, const Json&, const Cancellation& = {});
};
} // namespace cymcp
