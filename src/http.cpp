#include <algorithm>
#include <cctype>
#include <cymcp/http.hpp>
#include <curl/curl.h>
#include <limits>
namespace cymcp {
Url parse_url(const std::string& input) {
    if (input.size() > 8192 ||
        std::any_of(input.begin(), input.end(),
                    [](unsigned char c) { return c <= 32 || c == 127; }) ||
        input.find('\\') != std::string::npos || input.find('#') != std::string::npos)
        throw Error("InvalidUrl", "Invalid URL");
    auto colon = input.find("://");
    if (colon == std::string::npos)
        throw Error("InvalidUrl", "URL must include a scheme");
    Url u;
    u.scheme = input.substr(0, colon);
    if (u.scheme != "http" && u.scheme != "https")
        throw Error("InvalidUrl", "Only HTTP(S) URLs supported");
    auto end = input.find_first_of("/?", colon + 3);
    std::string authority =
        input.substr(colon + 3, end == std::string::npos ? end : end - colon - 3);
    if (authority.empty() || authority.find('@') != std::string::npos)
        throw Error("InvalidUrl", "URL credentials are forbidden");
    u.port = u.scheme == "https" ? 443 : 80;
    std::string port;
    if (authority[0] == '[') {
        auto b = authority.find(']');
        if (b == std::string::npos)
            throw Error("InvalidUrl", "Malformed IPv6 host");
        u.host = authority.substr(1, b - 1);
        if (b + 1 < authority.size()) {
            if (authority[b + 1] != ':')
                throw Error("InvalidUrl", "Malformed authority");
            port = authority.substr(b + 2);
        }
    } else {
        auto b = authority.find(':');
        u.host = authority.substr(0, b);
        if (b != std::string::npos)
            port = authority.substr(b + 1);
    }
    if (u.host.empty())
        throw Error("InvalidUrl", "Host is required");
    if (authority.back() == ':' ||
        u.host.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-:") !=
            std::string::npos)
        throw Error("InvalidUrl", "Invalid host or port");
    if (!port.empty()) {
        if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
            throw Error("InvalidUrl", "Invalid port");
        u.port = std::stoi(port);
        if (!u.port || u.port > 65535)
            throw Error("InvalidUrl", "Invalid port");
    }
    std::transform(u.host.begin(), u.host.end(), u.host.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    u.loopback = u.host == "localhost" || u.host == "127.0.0.1" || u.host == "::1";
    if (u.scheme == "http" && !u.loopback)
        throw Error("InsecureTransport", "Remote endpoints require HTTPS");
    u.target = end == std::string::npos ? "/" : input.substr(end);
    if (u.target[0] == '?')
        u.target = "/" + u.target;
    return u;
}
namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
void validate_request(const HttpRequest& r) {
    if (r.method != "GET" && r.method != "POST" && r.method != "DELETE" && r.method != "HEAD")
        throw Error("InvalidOptions", "Unsupported HTTP method");
    if (r.timeout.count() < 0 || r.idle_timeout.count() < 0 ||
        r.timeout > std::chrono::hours(24 * 365) || r.idle_timeout > std::chrono::hours(24) ||
        !r.max_response_bytes)
        throw Error("InvalidOptions", "Invalid HTTP limits");
    for (const auto& [key, value] : r.headers)
        if (key.empty() ||
            key.find_first_not_of(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") !=
                std::string::npos ||
            value.find_first_of("\r\n") != std::string::npos ||
            value.find('\0') != std::string::npos)
            throw Error("InvalidOptions", "Invalid HTTP header");
}
} // namespace
HttpResponse HttpClient::perform(const HttpRequest& request, const Cancellation& input,
                                 ChunkSink sink) const {
    validate_request(request);
    auto url = parse_url(request.url);
    Cancellation cancel = input;
    if (request.timeout.count() > 0)
        cancel.deadline = std::min(cancel.deadline, Clock::now() + request.timeout);
    cancel.check();
    HttpResponse result;
    std::size_t received = 0;
    // Any received byte restarts idle detection.
    std::atomic<std::int64_t> last_activity{Clock::now().time_since_epoch().count()};
    auto accept = [&](const char* data, std::size_t size) {
        last_activity.store(Clock::now().time_since_epoch().count());
        cancel.check();
        if (size > request.max_response_bytes - std::min(received, request.max_response_bytes))
            throw Error("ResponseTooLarge", "HTTP response exceeds limit");
        received += size;
        if (sink && request.error_body_limit && (result.status < 200 || result.status >= 300)) {
            if (received > request.error_body_limit) {
                result.body.clear();
                return false;
            }
            result.body.append(data, size);
        } else if (sink) {
            if (!sink(data, size))
                throw Error("Cancelled", "Response consumer stopped");
        } else
            result.body.append(data, size);
        return true;
    };
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK)
        throw Error("NetworkError", "Cannot initialize TLS transport");
    CURL* handle = curl_easy_init();
    if (!handle)
        throw Error("NetworkError", "Cannot initialize HTTP request");
    struct Cleanup {
        CURL* handle;
        curl_slist* headers = nullptr;
        ~Cleanup() {
            curl_slist_free_all(headers);
            curl_easy_cleanup(handle);
        }
    } cleanup{handle};
    struct State {
        decltype(accept)* receive;
        Cancellation cancel;
        HttpResponse* response;
        CURL* handle;
        std::atomic<std::int64_t>& activity;
        curl_off_t uploaded = 0;
        std::exception_ptr error;
        std::size_t headers = 0;
        bool error_body_discarded = false;
    } state{&accept, cancel, &result, handle, last_activity, 0, {}, 0, false};
    curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
    const auto user_agent = std::string("CyMCPSDK/") + version;
    curl_easy_setopt(handle, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    // The polling loop enforces longer deadlines when libcurl's long is only 32 bits.
    const long timeout = request.timeout.count() > std::numeric_limits<long>::max()
                             ? 0L
                             : static_cast<long>(request.timeout.count());
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    if (url.loopback)
        curl_easy_setopt(handle, CURLOPT_NOPROXY, "*");
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (request.method == "HEAD")
        curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    if (!request.body.empty() || request.method == "POST") {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
    }
    for (const auto& [key, value] : request.headers)
        cleanup.headers = curl_slist_append(cleanup.headers, (key + ": " + value).c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, cleanup.headers);
    curl_easy_setopt(
        handle, CURLOPT_WRITEFUNCTION,
        +[](char* data, std::size_t size, std::size_t count, void* raw) -> std::size_t {
            auto& s = *static_cast<State*>(raw);
            try {
                if (!s.response->status) {
                    long status = 0;
                    if (curl_easy_getinfo(s.handle, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK ||
                        !status)
                        throw Error("NetworkError", "Cannot read HTTP response status");
                    s.response->status = static_cast<int>(status);
                }
                if (!(*s.receive)(data, size * count)) {
                    s.error_body_discarded = true;
                    return 0;
                }
                return size * count;
            } catch (...) {
                s.error = std::current_exception();
                return 0;
            }
        });
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(
        handle, CURLOPT_HEADERFUNCTION,
        +[](char* data, std::size_t size, std::size_t count, void* raw) -> std::size_t {
            auto& s = *static_cast<State*>(raw);
            auto n = size * count;
            s.activity.store(Clock::now().time_since_epoch().count());
            s.headers += n;
            if (s.headers > 65536)
                return 0;
            std::string line(data, n);
            auto p = line.find(':');
            if (p != std::string::npos) {
                auto value = line.substr(p + 1);
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                    value.erase(value.begin());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                    value.pop_back();
                s.response->headers[lower(line.substr(0, p))] = value;
            }
            return n;
        });
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(
        handle, CURLOPT_XFERINFOFUNCTION,
        +[](void* raw, curl_off_t, curl_off_t, curl_off_t, curl_off_t uploaded) -> int {
            auto& s = *static_cast<State*>(raw);
            if (uploaded != s.uploaded) {
                s.uploaded = uploaded;
                s.activity.store(Clock::now().time_since_epoch().count());
            }
            return s.cancel.cancelled() || Clock::now() >= s.cancel.deadline ? 1 : 0;
        });
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state);
    // curl_easy_perform may leave the progress callback idle for an entire second.
    // Poll to the caller's millisecond deadline even when no bytes arrive.
    struct Transfer {
        CURLM* multi;
        CURL* easy;
        ~Transfer() {
            if (multi) {
                curl_multi_remove_handle(multi, easy);
                curl_multi_cleanup(multi);
            }
        }
    } transfer{curl_multi_init(), handle};
    if (!transfer.multi || curl_multi_add_handle(transfer.multi, handle) != CURLM_OK)
        throw Error("NetworkError", "Cannot initialize HTTP transfer");
    int running = 0;
    do {
        cancel.check();
        auto now = Clock::now();
        auto deadline = cancel.deadline;
        if (request.idle_timeout.count() > 0) {
            auto last = Clock::time_point(Clock::duration(last_activity.load()));
            deadline = std::min(deadline, last + request.idle_timeout);
            if (now >= deadline)
                throw Error("Timeout", "HTTP connection stayed silent beyond its idle timeout");
        }
        if (curl_multi_perform(transfer.multi, &running) != CURLM_OK)
            throw Error("NetworkError", "HTTP transfer failed");
        if (running) {
            auto wait = std::chrono::milliseconds(40);
            if (deadline != Clock::time_point::max())
                wait = std::clamp(
                    std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()),
                    std::chrono::milliseconds(0), wait);
#if LIBCURL_VERSION_NUM >= 0x074200
            auto status = curl_multi_poll(transfer.multi, nullptr, 0,
                                          static_cast<int>(wait.count()), nullptr);
#else
            int ready = 0;
            auto status =
                curl_multi_wait(transfer.multi, nullptr, 0, static_cast<int>(wait.count()), &ready);
            if (!ready && status == CURLM_OK)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
            if (status != CURLM_OK)
                throw Error("NetworkError", "Cannot poll HTTP transfer");
        }
    } while (running);
    if (state.error)
        std::rethrow_exception(state.error);
    cancel.check();
    int queued = 0;
    const auto* message = curl_multi_info_read(transfer.multi, &queued);
    if (!message || message->msg != CURLMSG_DONE)
        throw Error("NetworkError", "Missing HTTP transfer result");
    const auto code = message->data.result;
    if (code == CURLE_OPERATION_TIMEDOUT)
        throw Error("Timeout", "HTTP request timed out");
    if (code != CURLE_OK && !(state.error_body_discarded && code == CURLE_WRITE_ERROR))
        throw Error("NetworkError",
                    "HTTP request failed: " + std::string(curl_easy_strerror(code)));
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    result.status = static_cast<int>(status);
    cancel.check();
    return result;
}
} // namespace cymcp
