#include <cymcp/core.hpp>
#include <regex>
#include <set>

namespace cymcp {
Error::Error(std::string value, std::string message)
    : std::runtime_error(std::move(message)), code(std::move(value)) {}
Error::Error(std::string value, std::string message, std::string native)
    : std::runtime_error(std::move(message)), code(std::move(value)), provider_code(std::move(native)) {}
std::string path_utf8(const std::filesystem::path& path) {
    auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
Json parse_json(const std::string& text, std::size_t max_bytes, int max_depth) {
    if (max_depth < 1 || max_depth > 256)
        throw Error("InvalidOptions", "JSON depth limit must be 1..256");
    if (text.size() > max_bytes)
        throw Error("MessageTooLarge", "JSON message exceeds byte limit");
    std::vector<std::set<std::string>> keys(static_cast<std::size_t>(max_depth) + 2);
    try {
        return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > max_depth)
                throw Error("InvalidJson", "JSON nesting exceeds limit");
            if (event == Json::parse_event_t::object_start)
                keys[static_cast<std::size_t>(depth) + 1].clear();
            if (event == Json::parse_event_t::key &&
                !keys[static_cast<std::size_t>(depth)].insert(value.get<std::string>()).second)
                throw Error("InvalidJson", "Duplicate JSON object key");
            return true;
        });
    } catch (const Json::exception&) {
        throw Error("InvalidJson", "Malformed JSON");
    }
}
void Cancellation::check() const {
    if (cancelled())
        throw Error("Cancelled", "Operation cancelled");
    if (Clock::now() >= deadline)
        throw Error("Timeout", "Operation deadline exceeded");
}
void CallContext::emit(std::string type, Json data) const {
    cancellation.check();
    if (events)
        events({std::move(type), std::move(data)});
}
Policy::Policy() : Policy([](const auto&, const auto&, const auto&) { return Decision::deny; }) {}
Policy::Policy(PolicyCallback callback) : callback_(std::move(callback)) {
    if (!callback_)
        throw Error("InvalidPolicy", "Policy callback is required");
}
Policy Policy::allow_all() {
    return Policy([](const auto&, const auto&, const auto&) { return Decision::allow; });
}
Policy Policy::deny_all() {
    return {};
}
bool Policy::allows(const CallContext& c, const std::string& n, const Json& a) const {
    return callback_(c, n, a) == Decision::allow;
}
Registry::Registry(Policy policy) : policy_(std::move(policy)) {}
Json dispatch_app(const Registry& registry, const Json& request, const CallContext& context) {
    if (!request.is_object() || !request.contains("method") || !request["method"].is_string())
        throw Error("InvalidRequest", "Object with method required");
    auto method = request["method"].get<std::string>();
    if (method == "describe")
        return {{"sdkVersion", version}, {"operations", registry.describe()}};
    if (method == "invoke")
        return registry.invoke(request.at("name").get<std::string>(),
                               request.value("arguments", Json::object()), context);
    throw Error("UnknownMethod", "Unknown application method");
}
void Registry::add(Operation op) {
    if (op.name.empty() || op.name.size() > 128 ||
        !std::regex_match(op.name, std::regex("[A-Za-z0-9_.-]+")) || !op.handler)
        throw Error("InvalidOperation", "Operation needs a portable name and handler");
    check_schema(op.input_schema);
    check_schema(op.output_schema);
    if (!op.input_schema.is_object() || !op.output_schema.is_object())
        throw Error("InvalidSchema", "Operation schemas must be objects");
    if (op.input_schema.value("type", Json()) != "object" ||
        (!op.output_schema.empty() && op.output_schema.value("type", Json()) != "object"))
        throw Error("InvalidSchema",
                    "Operation parameters and declared structured outputs must be object schemas");
    std::lock_guard lock(mutex_);
    const auto name = op.name;
    if (!operations_.emplace(name, std::make_shared<const Operation>(std::move(op))).second)
        throw Error("DuplicateOperation", "Operation already registered");
}
Json Registry::describe() const {
    std::lock_guard lock(mutex_);
    Json result = Json::array();
    for (const auto& [name, op] : operations_) {
        Json value = {{"name", name},
                      {"description", op->description},
                      {"inputSchema", op->input_schema},
                      {"annotations",
                       {{"readOnlyHint", op->read_only}, {"destructiveHint", !op->read_only}}}};
        if (!op->output_schema.empty())
            value["outputSchema"] = op->output_schema;
        result.push_back(std::move(value));
    }
    return result;
}
Json Registry::invoke(const std::string& name, const Json& args, const CallContext& context) const {
    context.cancellation.check();
    std::shared_ptr<const Operation> op;
    {
        std::lock_guard lock(mutex_);
        auto it = operations_.find(name);
        if (it == operations_.end())
            throw Error("UnknownOperation", "Unknown operation");
        op = it->second;
    }
    if (!policy_.allows(context, name, args))
        throw Error("AccessDenied", "Operation denied by host policy");
    validate(args, op->input_schema);
    auto result = op->handler(args, context);
    context.cancellation.check();
    try {
        validate(result, op->output_schema);
    } catch (const Error&) {
        throw Error("InvalidOutput", "Handler output does not match its schema");
    }
    return result;
}
Executor::Executor(std::size_t workers, std::size_t capacity) : capacity_(capacity) {
    if (!workers || workers > 64 || !capacity)
        throw Error("InvalidOptions", "Invalid executor limits");
    try {
        for (std::size_t i = 0; i < workers; ++i)
            threads_.emplace_back([this] {
                while (true) {
                    std::function<void()> work;
                    {
                        std::unique_lock lock(mutex_);
                        wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                        if (stopping_ && queue_.empty())
                            return;
                        work = std::move(queue_.front());
                        queue_.pop_front();
                    }
                    try {
                        work();
                    } catch (...) { /* work owns its error channel */
                    }
                }
            });
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (auto& t : threads_)
            t.join();
        throw;
    }
}
Executor::~Executor() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& t : threads_)
        t.join();
}
void Executor::post(std::function<void()> work) {
    std::lock_guard lock(mutex_);
    if (stopping_)
        throw Error("Closed", "Executor stopped");
    if (queue_.size() >= capacity_)
        throw Error("Busy", "Executor queue full");
    queue_.push_back(std::move(work));
    wake_.notify_one();
}
EventQueue::EventQueue(std::size_t capacity, std::size_t bytes)
    : capacity_(capacity), byte_limit_(bytes) {}
void EventQueue::push(const Event& e) {
    auto size = e.type.size() + e.data.dump().size();
    std::lock_guard lock(mutex_);
    if (queue_.size() >= capacity_ || size > byte_limit_ - std::min(bytes_, byte_limit_))
        throw Error("Backpressure", "Event queue full");
    queue_.push_back(e);
    bytes_ += size;
}
std::vector<Event> EventQueue::drain(std::size_t count) {
    std::lock_guard lock(mutex_);
    std::vector<Event> result;
    while (!queue_.empty() && result.size() < count) {
        bytes_ -= queue_.front().type.size() + queue_.front().data.dump().size();
        result.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
    return result;
}
} // namespace cymcp
