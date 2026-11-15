#include <cymcp/http.hpp>
#include <httplib.h>
namespace cymcp {
int available_loopback_port() {
    httplib::Server probe;
    auto port = probe.bind_to_any_port("127.0.0.1");
    if (port < 1)
        throw Error("NetworkError", "Cannot allocate loopback port");
    probe.stop();
    return port;
}
struct LocalHttpServer::Impl {
    httplib::Server server;
    std::thread thread;
    std::string token;
    JsonEndpoint endpoint;
    Cancellation shutdown;
    Impl(std::string key, JsonEndpoint fn, std::size_t workers)
        : token(std::move(key)), endpoint(std::move(fn)) {
        if (token.size() < 32 || !endpoint || workers < 1 || workers > 64)
            throw Error("InvalidOptions",
                        "Server requires a strong token, endpoint and worker limit");
        server.new_task_queue = [workers] {
            return new httplib::ThreadPool(workers, workers, 128, 30);
        };
        server.set_payload_max_length(provider_message_limit);
        server.set_read_timeout(10, 0);
        server.set_write_timeout(10, 0);
        server.set_keep_alive_max_count(100);
        auto handler = [this](const httplib::Request& req, httplib::Response& res) {
            if (req.has_header("Origin")) {
                res.status = 403;
                return;
            }
            auto host = req.get_header_value("Host");
            if (host != "127.0.0.1" && host.rfind("127.0.0.1:", 0) != 0 && host != "localhost" &&
                host.rfind("localhost:", 0) != 0) {
                res.status = 403;
                return;
            }
            if (!constant_time_equal(req.get_header_value("Authorization"), "Bearer " + token)) {
                res.status = 401;
                res.set_header("WWW-Authenticate", "Bearer");
                return;
            }
            if (req.get_header_value("Content-Type").rfind("application/json", 0) != 0) {
                res.status = 415;
                return;
            }
            try {
                auto value = parse_json(req.body, provider_message_limit);
                CallContext context;
                context.principal = "authenticated-local";
                context.cancellation = shutdown.child();
                std::jthread watch_connection([&](std::stop_token stop) {
                    std::mutex mutex;
                    std::condition_variable_any wake;
                    std::unique_lock wait(mutex);
                    while (!stop.stop_requested()) {
                        if (req.is_connection_closed()) {
                            context.cancellation.cancel();
                            return;
                        }
                        wake.wait_for(wait, stop, std::chrono::milliseconds(20),
                                      [] { return false; });
                    }
                });
                if (req.path == "/mcp" && value.is_object() && value.contains("params") &&
                    value["params"].is_object()) {
                    auto metadata = value["params"].value("_meta", Json::object());
                    if (metadata.is_object() &&
                        metadata.contains("io.modelcontextprotocol/protocolVersion")) {
                        if (!metadata["io.modelcontextprotocol/protocolVersion"].is_string() ||
                            req.get_header_value("MCP-Protocol-Version") !=
                                metadata["io.modelcontextprotocol/protocolVersion"]
                                    .get<std::string>()) {
                            res.status = 400;
                            return;
                        }
                    }
                }
                auto reply = endpoint(value, context);
                if (reply.is_null()) {
                    res.status = 202;
                    return;
                }
                res.set_content(reply.dump(), "application/json");
            } catch (const Json::exception&) {
                res.status = 400;
                res.set_content(
                    Json(
                        {{"error", {{"code", "InvalidJson"}, {"message", "Invalid JSON request"}}}})
                        .dump(),
                    "application/json");
            } catch (const Error& e) {
                res.status = e.code == "AccessDenied" ? 403 : 400;
                res.set_content(Json({{"error", {{"code", e.code}, {"message", e.what()}}}}).dump(),
                                "application/json");
            } catch (...) {
                res.status = 500;
                res.set_content("{\"error\":{\"code\":\"InternalError\"}}", "application/json");
            }
        };
        server.Post("/mcp", handler);
        server.Post("/v1", handler);
        server.Get("/health", [](const auto&, auto& res) {
            res.set_content("{\"status\":\"ready\"}", "application/json");
        });
    }
};
LocalHttpServer::LocalHttpServer(std::string token, JsonEndpoint endpoint, std::size_t workers)
    : impl_(std::make_unique<Impl>(std::move(token), std::move(endpoint), workers)) {}
LocalHttpServer::~LocalHttpServer() {
    stop();
}
int LocalHttpServer::start(int port) {
    auto& p = *impl_;
    if (p.thread.joinable() || port < 0 || port > 65535)
        throw Error("InvalidState", "Server already started or invalid port");
    int actual = port;
    if (port == 0)
        actual = p.server.bind_to_any_port("127.0.0.1");
    else if (!p.server.bind_to_port("127.0.0.1", port))
        actual = -1;
    if (actual < 1)
        throw Error("NetworkError", "Cannot bind loopback server");
    p.thread = std::thread([&p] { p.server.listen_after_bind(); });
    p.server.wait_until_ready();
    return actual;
}
void LocalHttpServer::stop() noexcept {
    if (!impl_)
        return;
    impl_->shutdown.cancel();
    impl_->server.stop();
    if (impl_->thread.joinable())
        impl_->thread.join();
}
} // namespace cymcp
