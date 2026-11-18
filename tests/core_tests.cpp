#include "test.hpp"
using namespace cymcp;
int main() {
    try {
        const std::string refusal =
            "The 'gpt-6.2-sol' model is not supported when using Codex with a ChatGPT account.";
        check(redact_error_message(refusal) == refusal, "Ordinary provider explanations stay exact");
        const std::string hidden = "right-secret/value";
        for (const auto& diagnostic : std::vector<std::string>{
                 "Authorization: Bearer " + hidden, "Proxy-Authorization: Basic " + hidden,
                 "Authorization: token " + hidden, "https://user:" + hidden.substr(0, 12) + "@example.test",
                 "https://example.test/?access_token=" + hidden + "&format=json",
                 R"({"apiKey":"right-secret/value","message":"unavailable"})",
                 "password='right-secret/value; other words'", "Cookie: sid=" + hidden + "; extra=second-secret",
                 "-----BEGIN PRIVATE KEY-----\n" + hidden + "\n-----END PRIVATE KEY-----",
                 "eyJhbGciOiJIUzI1NiJ9.right-secret.signature", "sk-proj-right-secret"}) {
            const auto masked = redact_error_message(refusal + " " + diagnostic);
            check(masked.starts_with(refusal) && masked.find("[REDACTED]") != std::string::npos &&
                      masked.find("right-secret") == std::string::npos &&
                      masked.find("second-secret") == std::string::npos &&
                      redact_error_message(masked) == masked,
                  "Structured and conventional secrets are masked without losing the explanation");
        }
        check(redact_error_message("Unlabeled right-secret/value or right-secret", { "right-secret", hidden }) ==
                  "Unlabeled [REDACTED] or [REDACTED]",
              "Configured credentials are removed even without a field name");
        const auto bounded = redact_error_message(std::string(4091, 'x') + hidden + refusal, {hidden});
        check(bounded.size() <= 4096 && bounded.find("ri") == std::string::npos,
              "Truncating a diagnostic cannot disclose a credential prefix");
        const auto malformed = redact_error_message(std::string("line\r\nnext\t") + char(0xff));
        const auto multiline = redact_error_message("{\"api_key\":\r\n \"right-secret/value\"} " + refusal);
        check(multiline.ends_with(refusal) && multiline.find("right-secret") == std::string::npos &&
                  multiline.find_first_of("\r\n\t") == std::string::npos,
              "Pretty-printed credentials remain redacted in a single-line explanation");
        check(malformed.find_first_of("\r\n\t") == std::string::npos && Json::accept(Json(malformed).dump()),
              "Native diagnostics always remain single-line serializable UTF-8");
        check(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA empty vector");
        check(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA abc vector");
        check(parse_json("{\"a\":{\"b\":1},\"c\":{\"b\":2}}")["c"]["b"] == 2,
              "Bounded JSON parser");
        expect_error("InvalidJson", [] { parse_json("{\"a\":1,\"a\":2}"); });
        expect_error("InvalidJson",
                     [] { parse_json(std::string(70, '[') + "0" + std::string(70, ']')); });
        expect_error("MessageTooLarge", [] { parse_json("12345", 4); });
        Cancellation parent;
        auto child = parent.child();
        auto grandchild = child.child();
        parent.cancel();
        expect_error("Cancelled", [&] { grandchild.check(); });
        auto a = random_token(), b = random_token();
        check(a.size() == 64 && a != b, "Secure tokens");
        check(constant_time_equal(a, a) && !constant_time_equal(a, b), "Token comparison");
        Json schema = {{"type", "object"},
                       {"properties",
                        {{"n", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}}},
                         {"s", {{"type", "string"}, {"minLength", 2}}}}},
                       {"required", {"n"}},
                       {"additionalProperties", false}};
        check_schema(schema);
        validate({{"n", 3}, {"s", "valid"}}, schema);
        expect_error("InvalidParameters", [&] { validate({{"n", 0}}, schema); });
        expect_error("InvalidParameters", [&] { validate({{"n", 3}, {"extra", true}}, schema); });
        expect_error("InvalidParameters", [&] { validate({{"n", 3.5}}, schema); });
        expect_error("InvalidParameters", [&] { validate(Json::object(), schema); });
        expect_error("UnsupportedSchema", [] { check_schema({{"pattern", ".*"}}); });
        expect_error("UnsupportedSchema", [] { check_schema({{"$ref", "https://example.com"}}); });
        expect_error("InvalidParameters", [] {
            validate(Json::array({1, 1.0}), {{"type", "array"}, {"uniqueItems", true}});
        });
        validate(7, {{"oneOf", Json::array({{{"type", "number"}}, {{"type", "string"}}})}});
        expect_error("InvalidParameters", [] {
            validate(true, {{"oneOf", Json::array({{{"type", "number"}}, {{"type", "string"}}})}});
        });
        Operation op{"echo", "echo",
                     schema, {{"type", "object"}},
                     true,   [](const Json& j, const CallContext&) {return j; }};
        Registry denied;
        denied.add(op);
        expect_error("AccessDenied", [&] { denied.invoke("echo", {{"n", 4}}); });
        Registry allowed(Policy::allow_all());
        allowed.add(op);
        check(allowed.invoke("echo", {{"n", 4}})["n"] == 4, "AllowAll invokes");
        expect_error("DuplicateOperation", [&] { allowed.add(op); });
        Registry policy{Policy([](const CallContext& context, const std::string&, const Json&) {
            return context.principal == "trusted" ? Decision::allow : Decision::deny;
        })};
        policy.add(op);
        CallContext c;
        c.principal = "trusted";
        check(policy.invoke("echo", {{"n", 4}}, c)["n"] == 4, "Host policy");
        c.cancellation.cancel();
        expect_error("Cancelled", [&] { allowed.invoke("echo", {{"n", 4}}, c); });
        Cancellation expired;
        expired.deadline = Clock::now();
        expect_error("Timeout", [&] { expired.check(); });
        EventQueue queue(2, 1024);
        queue.push({"first", {}});
        queue.push({"second", {}});
        expect_error("Backpressure", [&] { queue.push({"third", {}}); });
        check(queue.drain().size() == 2, "Event queue drain");
        {
            Executor executor(2, 64);
            std::atomic<int> count = 0;
            std::promise<void> done;
            auto future = done.get_future();
            for (int i = 0; i < 32; ++i)
                executor.post([&] {
                    if (++count == 32)
                        done.set_value();
                });
            check(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                  "Worker pool progress");
        }
        std::cout << checks << " core checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
