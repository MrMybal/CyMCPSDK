#pragma once
#include "process.hpp"
#ifdef CYMCP_HAS_HTTP
#include "http.hpp"
#endif
#include <istream>
#include <ostream>
namespace cymcp {
class McpServer {
    const Registry& registry_;
    std::string name_;
    std::mutex mutex_;
    bool initialized_ = false;
    std::map<std::string, Cancellation> active_;

  public:
    explicit McpServer(const Registry&, std::string name = "CyMCPSDK");
    Json handle(const Json&, const CallContext& = {});
    void serve_stdio(std::istream&, std::ostream&, std::size_t workers = 4);
    void cancel_all();
};
class McpClient {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit McpClient(ProcessOptions);
#ifdef CYMCP_HAS_HTTP
    McpClient(std::string url, std::string token);
#endif
    ~McpClient();
    void connect(const Cancellation& = {});
    Json list_tools(const Cancellation& = {});
    Json call_tool(const std::string&, const Json&, const Cancellation& = {});
};
} // namespace cymcp
