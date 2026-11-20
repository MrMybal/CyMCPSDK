#pragma once
#include <cymcp/changes.hpp>
#include <cymcp/http.hpp>
#include <cymcp/mcp.hpp>
#include <cymcp/search.hpp>

namespace chat_example {
struct McpAccessOption {
    cymcp::LocalAccessProfile profile;
    std::string_view id;
    const char* label;
};
inline constexpr McpAccessOption mcp_access_options[] = {
    {cymcp::LocalAccessProfile::free, "free", "Free"},
    {cymcp::LocalAccessProfile::local_controlled, "local_controlled", "Local controlled"},
    {cymcp::LocalAccessProfile::token_only, "token_only", "Token only"},
    {cymcp::LocalAccessProfile::protected_access, "protected", "Protected"},
};
inline cymcp::LocalAccessSettings mcp_access_from_config(const cymcp::Json& config) {
    if (!config.contains("access_profile"))
        return {!config.value("mcp_without_token", false), !config.value("allow_changes", false)};
    auto id = config.at("access_profile").get<std::string>();
    for (const auto& option : mcp_access_options)
        if (option.id == id)
            return cymcp::local_access_settings(option.profile);
    throw cymcp::Error("InvalidOptions", "Unknown local MCP access profile");
}

// Counter commands by default. Workspace text search is enabled only for an explicit host root.
class Commands {
    static constexpr auto default_access_ =
        cymcp::local_access_settings(cymcp::LocalAccessProfile::free);
    std::atomic_bool writable_{!default_access_.control_operations};
    std::atomic_int value_{0}, reads_{0}, writes_{0}, denied_{0};
    std::shared_ptr<cymcp::Registry> registry_;
    std::unique_ptr<cymcp::McpServer> mcp_;
    std::shared_ptr<cymcp::ResourceRegistry> resources_;
    std::unique_ptr<cymcp::ToolGateway> gateway_;
    std::unique_ptr<cymcp::FileSearch> workspace_;
    bool deferred_ = false;
    std::unique_ptr<cymcp::LocalHttpServer> server_;
    std::string token_, endpoint_;
    bool require_token_ = default_access_.require_token;
    int port_ = 0;
    std::shared_ptr<cymcp::ChangeFeed> changes_;
    std::shared_ptr<cymcp::PerformanceMonitor> performance_;
    static cymcp::Resource counter_resource(int value) {
        auto text = std::make_shared<const std::string>(cymcp::Json({{"value", value}}).dump());
        cymcp::Resource r;
        r.uri = "app://chat-lab/counter";
        r.name = "Counter state";
        r.size = text->size();
        r.cache_ttl = std::chrono::seconds(30);
        r.read = [text](auto o, auto n, const auto& c) {
            c.cancellation.check();
            return text->substr(o, n);
        };
        return r;
    }

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
            "Chat Lab exposes get_counter and set_counter. The Free and Token only profiles "
            "allow counter changes. Optional workspace.grep and workspace.read expose only a "
            "host-selected "
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
                 if (changes_)
                     resources_->replace(counter_resource(value));
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
    void configure_services(cymcp::ResourceOptions options, bool notifications,
                            std::shared_ptr<cymcp::PerformanceMonitor> monitor = {}) {
        if (server_)
            throw cymcp::Error("Busy", "Configure services before starting MCP");
        const auto help = std::make_shared<const std::string>(
            resources_->read("app://chat-lab/commands")["contents"][0]["text"].get<std::string>());
        resources_ = std::make_shared<cymcp::ResourceRegistry>(cymcp::Policy::allow_all(),
                                                               std::move(options));
        cymcp::Resource r;
        r.uri = "app://chat-lab/commands";
        r.name = "Chat Lab commands";
        r.size = help->size();
        r.cache_ttl = std::chrono::seconds(30);
        r.read = [help](auto o, auto n, const auto& c) {
            c.cancellation.check();
            return help->substr(o, n);
        };
        resources_->add(std::move(r));
        performance_ = std::move(monitor);
        registry_->set_performance_monitor(performance_);
        if (notifications) {
            changes_ = std::make_shared<cymcp::ChangeFeed>();
            resources_->set_change_feed(changes_);
            registry_->set_change_feed(changes_);
            resources_->add(counter_resource(value_.load()));
        }
        gateway_ = std::make_unique<cymcp::ToolGateway>(registry_, cymcp::ToolGatewayOptions{},
                                                        resources_);
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
    void configure_mcp_access(cymcp::LocalAccessSettings access, int port) {
        if (server_ && (access.require_token != require_token_ || port != port_))
            throw cymcp::Error("Busy",
                               "Stop local MCP before changing its token requirement or port");
        if (port < 0 || port > 65535)
            throw cymcp::Error("InvalidOptions", "The local MCP port must be between 0 and 65535");
        require_token_ = access.require_token;
        allow_changes(!access.control_operations);
        port_ = port;
    }
    cymcp::LocalAccessSettings mcp_access() const {
        return {require_token_, !writable_.load()};
    }
    int mcp_port() const {
        return port_;
    }
    void start() {
        if (server_)
            return;
        cymcp::McpServerOptions options;
        options.resources = resources_;
        options.changes = changes_;
        options.performance = performance_;
        auto mcp = std::make_unique<cymcp::McpServer>(*ai_registry(), "CyMCPSDK Chat Lab", options);
        auto endpoint = [this, mcp = mcp.get()](const cymcp::Json& request,
                                                const cymcp::CallContext& context) {
            const auto method = request.value("method", std::string());
            if (method == "invoke" || method == "describe")
                return cymcp::dispatch_app(*registry_, request, context);
            return mcp->handle(request, context);
        };
        auto token = require_token_ ? cymcp::random_token() : std::string{};
        auto server = require_token_
                          ? std::make_unique<cymcp::LocalHttpServer>(token, endpoint)
                          : std::make_unique<cymcp::LocalHttpServer>(
                                cymcp::LocalHttpServer::UnauthenticatedLoopback{}, endpoint);
        auto port = server->start(port_);
        endpoint_ = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
        token_ = std::move(token);
        mcp_ = std::move(mcp);
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
