#pragma once
#include <cymcp/mcp.hpp>

namespace chat_example {
// The example's entire mutable domain: no filesystem, terminal or OS commands.
class Commands {
    std::atomic_bool writable_{false};
    std::atomic_int value_{0}, reads_{0}, writes_{0}, denied_{0};
    std::shared_ptr<cymcp::Registry> registry_;
    cymcp::McpServer mcp_;
    std::unique_ptr<cymcp::LocalHttpServer> server_;
    std::string token_, endpoint_;

  public:
    Commands()
        : registry_(std::make_shared<cymcp::Registry>(
              cymcp::Policy([this](const auto&, const std::string& name, const auto&) {
                  if (name == "get_counter" || writable_.load())
                      return cymcp::Decision::allow;
                  ++denied_;
                  return cymcp::Decision::deny;
              }))),
          mcp_(*registry_, "CyMCPSDK Chat Lab") {
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
        auto server = std::make_unique<cymcp::LocalHttpServer>(
            token_, [this](const cymcp::Json& request, const cymcp::CallContext& context) {
                const auto method = request.value("method", std::string());
                if (method == "invoke" || method == "describe")
                    return cymcp::dispatch_app(*registry_, request, context);
                return mcp_.handle(request, context);
            });
        auto port = server->start();
        endpoint_ = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
        server_ = std::move(server);
    }
    void stop() noexcept {
        if (server_)
            server_->stop();
        server_.reset();
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
