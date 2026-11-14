#include <cymcp/core.hpp>
#include <iostream>
int main() {
    using namespace cymcp;
    Registry registry(Policy::allow_all());
    registry.add({"add",
                  "Add integers",
                  {{"type", "object"},
                   {"properties", {{"n", {{"type", "integer"}}}}},
                   {"required", {"n"}},
                   {"additionalProperties", false}},
                  {{"type", "object"}},
                  true,
                  [](const Json& args, const CallContext&) {
                      return Json{{"result", args.at("n").get<int>() + 1}};
                  }});
    Json args = {{"n", 41}};
    CallContext context;
    constexpr int count = 100000;
    std::uint64_t total = 0;
    for (int i = 0; i < 1000; ++i)
        registry.invoke("add", args, context);
    auto started = Clock::now();
    for (int i = 0; i < count; ++i)
        total += registry.invoke("add", args, context).at("result").get<unsigned>();
    auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - started).count();
    std::cout
        << Json({{"iterations", count},
                 {"microsecondsPerInvoke", elapsed / count},
                 {"checksum", total},
                 {"scope",
                  "native registry, policy, input/output validation; no network or inference"}})
               .dump(2)
        << '\n';
}
