#pragma once
#include <cymcp/mcp.hpp>
#include <cymcp/search.hpp>

namespace chat_example {
// Counter commands by default. Workspace text search is enabled only for an explicit host root.
class Commands {
    std::atomic_bool writable_{false};
    std::atomic_int value_{0}, reads_{0}, writes_{0}, denied_{0};
    std::shared_ptr<cymcp::Registry> registry_;
    std::unique_ptr<cymcp::McpServer> mcp_;
    std::shared_ptr<cymcp::ResourceRegistry> resources_;
    std::unique_ptr<cymcp::ToolGateway> gateway_;
    std::unique_ptr<cymcp::FileSearch> workspace_;
    bool deferred_ = false;
    std::unique_ptr<cymcp::LocalHttpServer> server_;
    std::string token_, endpoint_;

  public:
    Commands()
        : registry_(std::make_shared<cymcp::Registry>(
              cymcp::Policy([this](const auto&, const std::string& name, const auto&) {
                  if (name == "get_counter" || name == "workspace.grep" ||
                      name == "workspace.read" || writable_.load())
                      return cymcp::Decision::allow;
                  ++denied_;
                  return cymcp::Decision::deny;
              }))) {
        resources_ = std::make_shared<cymcp::ResourceRegistry>(cymcp::Policy::allow_all());
        const auto help = std::make_shared<const std::string>(
            "Chat Lab exposes get_counter and set_counter. Counter writes require Allow counter "
            "changes. Optional workspace.grep and workspace.read expose only a host-selected "
            "folder, read only.");
        cymcp::Resource resource;
        resource.uri = "app://chat-lab/commands";
        resource.name = "Chat Lab commands";
        resource.size = help->size();
        resource.cache_ttl = std::chrono::seconds(30);
        resource.read = [help](auto offset, auto length, const auto& context) {
            context.cancellation.check();
            return help->substr(offset, length);
        };
        resources_->add(std::move(resource));
        const cymcp::Json result_schema = {{"type", "object"},
                                           {"properties", {{"value", {{"type", "integer"}}}}},
                                           {"required", {"value"}},
                                           {"additionalProperties", false}};
        registry_->add({"get_counter",
                        "Read the Chat Lab demonstration counter.",
                        {{"type", "object"}, {"additionalProperties", false}},
                        result_schema,
                        true,
                        [this](const auto&, const auto&) {
                            ++reads_;
                            return cymcp::Json{{"value", value_.load()}};
                        }});
        registry_->add(
            {"set_counter",
             "Set the Chat Lab demonstration counter to an integer.",
             {{"type", "object"},
              {"properties",
               {{"value", {{"type", "integer"}, {"minimum", -1000000}, {"maximum", 1000000}}}}},
              {"required", {"value"}},
              {"additionalProperties", false}},
             result_schema,
             false,
             [this](const auto& args, const auto&) {
                 const int value = args.at("value").template get<int>();
                 value_ = value;
                 ++writes_;
                 return cymcp::Json{{"value", value}};
             }});
        gateway_ = std::make_unique<cymcp::ToolGateway>(registry_, cymcp::ToolGatewayOptions{},
                                                        resources_);
    }
    ~Commands() {
        stop();
    }
    void allow_changes(bool allow) {
        writable_ = allow;
    }
    bool changes_allowed() const {
        return writable_.load();
    }
    std::shared_ptr<const cymcp::Registry> registry() const {
        return registry_;
    }
    std::shared_ptr<const cymcp::Registry> ai_registry() const {
        return deferred_ ? gateway_->tools() : registry_;
    }
    void deferred_tools(bool enabled) {
        if (server_)
            throw cymcp::Error("Busy", "Stop the MCP server before changing tool exposure");
        deferred_ = enabled;
    }
    void configure_workspace(std::filesystem::path root, std::filesystem::path ripgrep = {}) {
        if (server_ || workspace_)
            throw cymcp::Error("InvalidState",
                               "Configure the workspace before starting the server");
        cymcp::FileSearchOptions options;
        options.root = std::move(root);
        options.ripgrep = std::move(ripgrep);
        workspace_ = std::make_unique<cymcp::FileSearch>(std::move(options));
        workspace_->add_to(*registry_);
    }
    cymcp::Json snapshot() const {
        return {{"value", value_.load()},
                {"reads", reads_.load()},
                {"writes", writes_.load()},
                {"denied", denied_.load()},
                {"writable", writable_.load()}};
    }
    void start() {
        if (server_)
            return;
        token_ = cymcp::random_token();
        cymcp::McpServerOptions options;
        options.resources = resources_;
        mcp_ = std::make_unique<cymcp::McpServer>(*ai_registry(), "CyMCPSDK Chat Lab", options);
        auto server = std::make_unique<cymcp::LocalHttpServer>(
            token_, [this](const cymcp::Json& request, const cymcp::CallContext& context) {
                const auto method = request.value("method", std::string());
                if (method == "invoke" || method == "describe")
                    return cymcp::dispatch_app(*registry_, request, context);
                return mcp_->handle(request, context);
            });
        auto port = server->start();
        endpoint_ = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
        server_ = std::move(server);
    }
    void stop() noexcept {
        if (server_)
            server_->stop();
        server_.reset();
        mcp_.reset();
        token_.clear();
        endpoint_.clear();
    }
    const std::string& endpoint() const {
        return endpoint_;
    }
    const std::string& token() const {
        return token_;
    }
};
} // namespace chat_example
