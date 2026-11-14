#pragma once
#include "http.hpp"
#include "process.hpp"
namespace cymcp {
struct Session {
    std::string id, model;
};
struct ChatResult {
    std::string text, finish_reason;
    Json usage = Json::object();
};
struct ProviderCapabilities {
    bool streaming = false, resume = false, tools = false, model_catalogue = false,
         multimodal = false;
};
class Provider {
  public:
    virtual ~Provider() = default;
    virtual ProviderCapabilities capabilities() const = 0;
    virtual void connect(const CallContext& = {}) = 0;
    virtual Json list_models(const CallContext& = {}) {
        throw Error("Unsupported", "This transport does not expose a model catalogue");
    }
    virtual Session start_session(std::string model, std::string resume_id = {},
                                  const CallContext& = {}, std::string system_prompt = {}) = 0;
    // messages are typed JSON chat messages; tool calls/results may be appended during a turn.
    virtual ChatResult send(Session&, Json& messages, const CallContext&) = 0;
    virtual void disconnect() noexcept = 0;
};
struct HttpChatOptions {
    std::string base_url = "http://127.0.0.1:9931/v1", api_key;
    std::shared_ptr<const Registry> tools;
    std::size_t max_tool_rounds = 8;
    std::chrono::milliseconds timeout{120000};
};
class HttpChatProvider final : public Provider {
    HttpChatOptions options_;

  public:
    explicit HttpChatProvider(HttpChatOptions);
    ProviderCapabilities capabilities() const override;
    void connect(const CallContext& = {}) override;
    Json list_models(const CallContext& = {}) override;
    Session start_session(std::string, std::string = {}, const CallContext& = {},
                          std::string = {}) override;
    ChatResult send(Session&, Json&, const CallContext&) override;
    void disconnect() noexcept override {}
};
// Callback returns a provider-native result. No raw requests are logged by the SDK.
using ProviderRequestHandler =
    std::function<Json(const std::string&, const Json&, const CallContext&)>;
struct CodexOptions {
    std::chrono::milliseconds connect_timeout{30000}, turn_timeout{600000};
    ProcessOptions process;
    std::string approval_policy = "never", sandbox = "read-only";
    std::shared_ptr<const Registry> tools;
    Policy approvals;
    ProviderRequestHandler request_handler;
};
class CodexProvider final : public Provider {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit CodexProvider(CodexOptions);
    ~CodexProvider();
    ProviderCapabilities capabilities() const override;
    void connect(const CallContext& = {}) override;
    Json list_models(const CallContext& = {}) override;
    Session start_session(std::string, std::string = {}, const CallContext& = {},
                          std::string = {}) override;
    ChatResult send(Session&, Json&, const CallContext&) override;
    void disconnect() noexcept override;
};
struct ClaudeOptions {
    std::chrono::milliseconds connect_timeout{15000}, turn_timeout{600000};
    ProcessOptions process;
    Json mcp_servers = Json::object();
    std::vector<std::string> allowed_tools; // empty means no built-in tools
    bool allow_all_actions = false;         // maps explicitly to the provider's bypass mode
};
class ClaudeProvider final : public Provider {
    ClaudeOptions options_;
    std::mutex mutex_;

  public:
    explicit ClaudeProvider(ClaudeOptions);
    ProviderCapabilities capabilities() const override;
    void connect(const CallContext& = {}) override;
    Session start_session(std::string, std::string = {}, const CallContext& = {},
                          std::string = {}) override;
    ChatResult send(Session&, Json&, const CallContext&) override;
    void disconnect() noexcept override {}
};
// API protocols have different authentication, request and streaming formats.
enum class ApiProtocol { OpenAIResponses, AnthropicMessages, Ollama, OpenRouter };
struct ApiOptions {
    ApiProtocol protocol = ApiProtocol::OpenAIResponses;
    std::string base_url, api_key; // empty URL selects the protocol's official endpoint
    std::shared_ptr<const Registry> tools;
    std::size_t max_tool_rounds = 8;
    int max_output_tokens = 4096;
    std::chrono::milliseconds timeout{120000};
};
class ApiProvider final : public Provider {
    ApiOptions options_;

  public:
    explicit ApiProvider(ApiOptions);
    ProviderCapabilities capabilities() const override;
    void connect(const CallContext& = {}) override;
    Json list_models(const CallContext& = {}) override;
    Session start_session(std::string, std::string = {}, const CallContext& = {},
                          std::string = {}) override;
    ChatResult send(Session&, Json&, const CallContext&) override;
    void disconnect() noexcept override {}
};
enum class AgentCliProtocol { OpenCode, Antigravity, CommandCode, CodeBuddy };
struct AgentCliOptions {
    AgentCliProtocol protocol = AgentCliProtocol::OpenCode;
    ProcessOptions process;
    std::chrono::milliseconds connect_timeout{15000}, turn_timeout{600000};
    bool allow_all_actions = false;
    // OpenCode v1 config, or Antigravity/CodeBuddy --settings; policy flags take precedence.
    Json native_settings = Json::object();
    std::filesystem::path mcp_config_file;  // optional CodeBuddy MCP file supplied by the host
    std::vector<std::string> allowed_tools; // CodeBuddy built-ins; empty disables them by default
};
class AgentCliProvider final : public Provider {
    AgentCliOptions options_;
    std::mutex mutex_;
    std::mutex cancellation_mutex_;
    Cancellation active_;
    bool connected_ = false;
    CallContext begin(const CallContext&, std::chrono::milliseconds);

  public:
    explicit AgentCliProvider(AgentCliOptions);
    ProviderCapabilities capabilities() const override;
    void connect(const CallContext& = {}) override;
    Json list_models(const CallContext& = {}) override;
    Session start_session(std::string, std::string = {}, const CallContext& = {},
                          std::string = {}) override;
    ChatResult send(Session&, Json&, const CallContext&) override;
    void disconnect() noexcept override;
};
struct AcpOptions {
    // Complete command including the agent's ACP subcommand/flag; no shell is used.
    ProcessOptions process;
    std::chrono::milliseconds connect_timeout{15000}, turn_timeout{600000};
    Json mcp_servers = Json::array(); // ACP stdio/HTTP server objects, not Claude's keyed map
    Policy approvals; // denies agent permission requests until explicitly configured
    std::string mode; // optional exact native mode/config value
    // Optional independent non-generating command, including any wrapper/script prefix.
    ProcessOptions models_process;
};
class AcpProvider final : public Provider {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit AcpProvider(AcpOptions);
    ~AcpProvider();
    ProviderCapabilities capabilities() const override;
    void connect(const CallContext& = {}) override;
    Json list_models(const CallContext& = {}) override;
    Session start_session(std::string, std::string = {}, const CallContext& = {},
                          std::string = {}) override;
    ChatResult send(Session&, Json&, const CallContext&) override;
    void disconnect() noexcept override;
};
struct ProviderDescriptor {
    std::string id, name, transport, authentication;
    // Available features; instance capabilities reflect the supplied configuration.
    ProviderCapabilities capabilities;
};
struct ProviderConfig {
    ProcessOptions process;
    Json settings = Json::object();
    std::string api_key; // supplied by the host's vault/environment; never catalogued
    std::shared_ptr<const Registry> tools;
    Policy approvals;
    ProviderRequestHandler request_handler;
};
using ProviderFactory = std::function<std::shared_ptr<Provider>(const ProviderConfig&)>;
class ProviderRegistry {
    mutable std::mutex mutex_;
    std::map<std::string, std::pair<ProviderDescriptor, ProviderFactory>> entries_;

  public:
    explicit ProviderRegistry(bool builtins = true);
    void add(ProviderDescriptor, ProviderFactory);
    Json describe() const;
    std::shared_ptr<Provider> create(const std::string&, const ProviderConfig& = {}) const;
};
class Conversation {
    struct State;
    std::shared_ptr<State> state_;
    CallContext reserve(const CallContext&);
    ChatResult send_reserved(Json, const CallContext&);

  public:
    Conversation(std::shared_ptr<Provider>, std::string model, std::string system_prompt = {});
    void connect(std::string resume_id = {}, const CallContext& = {});
    ChatResult send(Json user_content, const CallContext& = {});
    std::future<ChatResult> send_async(Executor&, Json user_content, CallContext = {});
    void cancel() noexcept;
    Json history() const;
    Session session() const;
};
} // namespace cymcp
