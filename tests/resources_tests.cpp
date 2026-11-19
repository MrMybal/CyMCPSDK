#include "test.hpp"
#include <cymcp/resources.hpp>
#include <set>
using namespace cymcp;
using namespace std::chrono_literals;
namespace {
Resource text_resource(std::string uri, std::string text, std::atomic_int* reads = nullptr) {
    auto data = std::make_shared<const std::string>(std::move(text));
    Resource resource;
    resource.uri = std::move(uri);
    resource.name = "Text snapshot";
    resource.size = data->size();
    resource.read = [data, reads](auto offset, auto length, const auto& context) {
        context.cancellation.check();
        if (reads)
            ++*reads;
        return data->substr(offset, length);
    };
    return resource;
}
std::string full(ResourceRegistry& registry, std::string uri, const CallContext& context = {}) {
    auto first = registry.read(uri, context);
    if (first.at("contents")[0].at("mimeType") == "application/json") {
        const auto parsed = parse_json(first.at("contents")[0].at("text").get<std::string>());
        if (parsed.is_object() && parsed.contains("firstUri"))
            uri = parsed.at("firstUri");
    }
    std::string output;
    for (int n = 0; n < 100000; ++n) {
        auto part = registry.read(uri, context);
        output += part.at("contents")[0].at("text").get<std::string>();
        const auto& page = part.at("_meta").at("com.cymcp/chunk");
        if (page.at("eof").get<bool>())
            return output;
        uri = page.at("nextUri");
    }
    throw std::runtime_error("Resource pagination did not terminate");
}
Operation operation(std::string name, std::atomic_int& calls) {
    return {std::move(name),
            "Searchable application operation " + std::string(600, 'd'),
            {{"type", "object"},
             {"properties", {{"value", {{"type", "integer"}}}}},
             {"required", {"value"}},
             {"additionalProperties", false}},
            {{"type", "object"}},
            false,
            [&calls](const Json& args, const CallContext&) {
                ++calls;
                return args;
            }};
}
} // namespace
int main() {
    try {
        ResourceOptions options;
        options.chunk_bytes = 13;
        ResourceRegistry registry(Policy::allow_all(), options);
        std::atomic_int reads{0};
        std::string utf8;
        for (int n = 0; n < 80; ++n)
            utf8 += "é🙂中文 ligne\n";
        registry.add(text_resource("app://document", utf8, &reads));
        check(registry.list()["resources"].size() == 1 && reads == 0,
              "Resource discovery does not load contents");
        auto manifest = registry.read("app://document");
        check(reads == 0 && manifest.dump().size() < 1024,
              "Large resource reads return a small manifest without loading the body");
        check(full(registry, "app://document") == utf8 && reads > 0,
              "UTF-8 resource chunks reconstruct every byte across character boundaries");
        auto old = parse_json(manifest["contents"][0]["text"]).at("firstUri").get<std::string>();
        registry.replace(text_resource("app://document", "new"));
        expect_error("ResourceNotFound", [&] { registry.read(old); });
        check(full(registry, "app://document") == "new",
              "Replacement invalidates old snapshot handles");
        Resource binary;
        binary.uri = "app://binary";
        binary.name = "Binary";
        binary.binary = true;
        binary.mime_type = "application/octet-stream";
        binary.size = 3;
        binary.read = [](auto offset, auto length, const auto&) {
            return std::string("\0\xff\2", 3).substr(offset, length);
        };
        registry.add(binary);
        check(registry.read("app://binary")["contents"][0]["blob"] == "AP8C",
              "Binary resources use standard base64 contents");
        ResourceRegistry denied;
        denied.add(text_resource("app://secret", "secret"));
        expect_error("AccessDenied", [&] { denied.read("app://secret"); });
        Cancellation stopped;
        stopped.cancel();
        CallContext cancelled;
        cancelled.cancellation = stopped;
        expect_error("Cancelled", [&] { registry.read("app://document", cancelled); });
        auto malformed = text_resource("app://bad", "text");
        malformed.read = [](auto, auto, const auto&) { return std::string(); };
        registry.add(malformed);
        expect_error("ResourceChanged", [&] { registry.read("app://bad"); });
        registry.remove("app://bad");

        ResourceOptions retention;
        retention.max_stored_bytes = 80;
        retention.max_stored_results = 2;
        retention.result_ttl = 30ms;
        ResourceRegistry retained(Policy::allow_all(), retention);
        CallContext alice;
        alice.principal = "alice";
        CallContext bob;
        bob.principal = "bob";
        const auto handle =
            retained.store(std::string(40, 'a'), "text/plain", alice).at("uri").get<std::string>();
        expect_error("ResourceNotFound", [&] { retained.read(handle, bob); });
        check(retained.list()["resources"].empty(),
              "Private result handles never leak into the global resource list");
        retained.store(std::string(40, 'b'), "text/plain", alice);
        retained.store(std::string(40, 'c'), "text/plain", alice);
        check(retained.stored_bytes() == 80, "Retained result memory and entry counts are bounded");
        expect_error("ResourceNotFound", [&] { retained.read(handle, alice); });
        std::this_thread::sleep_for(40ms);
        check(retained.stored_bytes() == 0, "Expired result storage is reclaimed");

        auto target = std::make_shared<Registry>(Policy::allow_all());
        std::atomic_int calls{0};
        for (int n = 0; n < 100; ++n)
            target->add(operation("app.command_" + std::to_string(n), calls));
        auto snapshot = target->catalogue();
        check(snapshot == target->catalogue(), "Registry caches immutable descriptions");
        ToolGatewayOptions gateway_options;
        gateway_options.page_size = 7;
        gateway_options.inline_bytes = 128;
        gateway_options.resources.chunk_bytes = 32;
        ToolGateway gateway(target, gateway_options);
        auto tools = gateway.tools();
        check(tools->describe().size() == 5, "Compact discovery keeps five small bootstrap tools");
        check(tools->describe().dump().size() * 5 < target->describe().dump().size(),
              "Compact definitions reduce wire bytes for large application catalogues");
        std::cout << "Tool catalogue bytes: full=" << target->describe().dump().size()
                  << ", compact=" << tools->describe().dump().size() << '\n';
        std::set<std::string> names;
        std::string cursor;
        Json page;
        do {
            Json args = Json::object();
            if (!cursor.empty())
                args["cursor"] = cursor;
            page = tools->invoke("cy_tools_search", args);
            for (const auto& item : page["tools"]) {
                check(!item.contains("inputSchema"), "Search results omit heavy schemas");
                names.insert(item.at("name"));
            }
            cursor = page.value("nextCursor", std::string());
        } while (!cursor.empty());
        check(names.size() == 100 && calls == 0,
              "Every application operation remains discoverable without executing it");
        auto descriptor = tools->invoke("cy_tools_describe", {{"name", "app.command_17"}});
        check(descriptor.at("inputSchema").at("required")[0] == "value",
              "One full schema is loaded on demand");
        check(tools->invoke("cy_tools_call", {{"name", "app.command_17"},
                                              {"arguments", {{"value", 42}}}})["value"] == 42 &&
                  calls == 1,
              "Lazy invocation reaches the exact original operation once");
        expect_error("InvalidParameters", [&] {
            tools->invoke("cy_tools_call",
                          {{"name", "app.command_17"}, {"arguments", {{"value", "bad"}}}});
        });
        auto first = tools->invoke("cy_tools_search", Json::object());
        target->replace(operation("app.command_17", calls));
        expect_error("InvalidParameters", [&] {
            tools->invoke("cy_tools_search", {{"cursor", first.at("nextCursor")}});
        });
        target->remove("app.command_17");
        check(snapshot->size() == 100 && target->catalogue()->size() == 99,
              "Registry changes invalidate cached catalogues while preserving older snapshots");
        auto restricted = std::make_shared<Registry>();
        restricted->add(operation("write", calls));
        ToolGateway guard(restricted);
        expect_error("AccessDenied", [&] {
            guard.tools()->invoke("cy_tools_call",
                                  {{"name", "write"}, {"arguments", {{"value", 1}}}});
        });
        target->add({"large",
                     "Large output",
                     {{"type", "object"}},
                     {{"type", "object"}},
                     true,
                     [&](const auto&, const auto&) {
                         ++calls;
                         return Json{{"text", utf8 + utf8}};
                     }});
        auto large = tools->invoke("cy_tools_call", {{"name", "large"}});
        auto uri = large.at("resource").at("uri").get<std::string>();
        check(large.dump().size() < 1024 &&
                  parse_json(full(*gateway.resources(), uri))["text"] == utf8 + utf8,
              "Large tool output remains complete behind a small resource handle");
        auto tiny = gateway_options;
        tiny.resources.max_stored_bytes = 64;
        ToolGateway fallback(target, tiny);
        const auto before = calls.load();
        check(fallback.tools()->invoke("cy_tools_call", {{"name", "large"}})["text"] ==
                      utf8 + utf8 &&
                  calls == before + 1,
              "An oversized retained result stays complete inline without executing twice");
        check(compact_tools(target)->describe().size() == 5,
              "Large provider tool sets select compact discovery automatically");
        auto small = std::make_shared<Registry>(Policy::allow_all());
        small->add(operation("small", calls));
        check(compact_tools(small) == small, "Small existing provider registries remain unchanged");
        std::string lines;
        for (int n = 0; n < 120; ++n)
            lines += "line " + std::to_string(n) + " Needle é🙂\n";
        registry.add(text_resource("app://grep", lines));
        std::size_t matches = 0;
        cursor.clear();
        do {
            auto found = registry.grep("needle", true, cursor);
            matches += found["matches"].size();
            for (const auto& hit : found["matches"])
                check(registry.read(hit.at("readUri"))["contents"].size() == 1,
                      "Grep matches link to readable source ranges");
            cursor = found.value("nextCursor", std::string());
        } while (!cursor.empty());
        check(matches == 120,
              "Native resource grep streams paged text and returns each matching line once");
        std::cout << checks << " resource and on-demand tool checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
