#include "test.hpp"
#include <cymcp/changes.hpp>
#include <cymcp/performance.hpp>
#include <cymcp/resources.hpp>
#include <fstream>
using namespace cymcp;
using namespace std::chrono_literals;
int main() {
    try {
        const auto folder =
            std::filesystem::temp_directory_path() / ("cymcp-optional-" + random_token(16));
        std::filesystem::create_directory(folder);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{folder};
        ResourceOptions o;
        o.chunk_bytes = 32768;
        o.max_stored_bytes = 64;
        o.disk_directory = folder;
        o.max_disk_bytes = 2 * 1024 * 1024;
        o.spool_threshold = 64;
        o.result_ttl = 2s;
        {
            ResourceRegistry resources(Policy::allow_all(), o);
            const std::string block = std::string(65531, 'x') + "é🙂\n";
            auto handle = resources.store_stream(
                [&](auto& out, const auto&) {
                    for (int n = 0; n < 16; ++n)
                        out.write(block.data(), block.size());
                },
                "text/plain");
            check(resources.storage_stats()["memory_bytes"] == 0 &&
                      resources.storage_stats()["disk_bytes"] == block.size() * 16,
                  "Large producer spools to disk outside the memory-result budget");
            auto manifest = parse_json(resources.read(handle["uri"])["contents"][0]["text"]);
            std::string uri = manifest.at("firstUri");
            std::size_t offset = 0, pages = 0;
            do {
                auto part = resources.read(uri);
                auto text = part["contents"][0]["text"].get<std::string>();
                check(text.size() <= o.chunk_bytes, "Disk reads remain bounded");
                for (char c : text) {
                    if (c != block[offset % block.size()])
                        throw std::runtime_error("Disk content changed");
                    ++offset;
                }
                ++pages;
                uri = part["_meta"]["com.cymcp/chunk"].value("nextUri", std::string());
            } while (!uri.empty());
            check(offset == block.size() * 16 && pages > 16,
                  "Disk chunks preserve every UTF-8 byte");
            CallContext stranger;
            stranger.principal = "other";
            expect_error("ResourceNotFound", [&] { resources.read(handle["uri"], stranger); });
            expect_error("ProducerFailed", [&] {
                resources.store_stream(
                    [](auto& out, const auto&) {
                        out << std::string(10000, 's');
                        throw Error("ProducerFailed", "private-body");
                    },
                    "text/plain");
            });
            expect_error("ResourceTooLarge", [&] {
                resources.store_stream(
                    [&](auto& out, const auto&) { out << std::string(o.max_disk_bytes + 1, 'x'); },
                    "text/plain");
            });
            CallContext cancelled;
            expect_error("Cancelled", [&] {
                resources.store_stream(
                    [&](auto& out, const auto&) {
                        out << std::string(1000, 'x');
                        cancelled.cancellation.cancel();
                        out << "more";
                    },
                    "text/plain", cancelled);
            });
            auto binary = resources.store_stream(
                [](auto& out, const auto&) {
                    for (int n = 0; n < 1000; ++n)
                        out.write("\0\xff\2", 3);
                },
                "application/octet-stream", {}, true);
            std::string expected;
            for (int n = 0; n < 1000; ++n)
                expected += "AP8C";
            check(resources.read(binary["uri"])["contents"][0]["blob"] == expected,
                  "Disk producers support complete binary resources using base64");
            std::this_thread::sleep_for(2100ms);
            check(resources.storage_stats()["disk_bytes"] == 0, "Disk retention expires");
            std::size_t files = 0;
            for (const auto& p : std::filesystem::recursive_directory_iterator(folder))
                files += p.is_regular_file();
            check(files == 0, "Failed, cancelled and expired producers leave no result files");
        }
        check(std::filesystem::is_empty(folder),
              "Owned private spool directory is cleaned on destruction");
        {
            auto independent = o;
            independent.max_stored_bytes = 1024 * 1024;
            independent.spool_threshold = 1024 * 1024;
            independent.max_disk_bytes = 64;
            ResourceRegistry resources(Policy::allow_all(), independent);
            resources.store(std::string(1000, 'r'), "text/plain");
            check(resources.storage_stats()["memory_bytes"] == 1000 &&
                      resources.storage_stats()["disk_bytes"] == 0,
                  "A small disk quota does not restrict a separate valid RAM retention budget");
        }
        {
            auto large_options = o;
            large_options.result_ttl = 60s;
            large_options.max_disk_bytes = 96 * 1024 * 1024;
            ResourceRegistry large(Policy::allow_all(), large_options);
            const std::string block(65536, 'v');
            auto handle = large.store_stream(
                [&](auto& output, const auto&) {
                    for (int n = 0; n < 1280; ++n)
                        output.write(block.data(), block.size());
                },
                "text/plain");
            constexpr std::size_t bytes = 80 * 1024 * 1024;
            check(handle["bytes"] == bytes && large.storage_stats()["memory_bytes"] == 0,
                  "An 80 MiB producer exceeds the former RAM limit while retaining zero result "
                  "bytes in RAM");
            auto manifest = parse_json(large.read(handle["uri"])["contents"][0]["text"]);
            std::string first_uri = manifest["firstUri"];
            auto last_uri =
                first_uri.substr(0, first_uri.find_last_of('/') + 1) + std::to_string(bytes - 17);
            check(large.read(first_uri)["contents"][0]["text"].get<std::string>().size() ==
                          large_options.chunk_bytes &&
                      large.read(last_uri)["contents"][0]["text"] == std::string(17, 'v'),
                  "Large disk resources support bounded first and final range reads");
        }
        {
            o.result_ttl = 60s;
            ResourceRegistry resources(Policy::allow_all(), o);
            std::promise<void> written, finish;
            auto released = finish.get_future().share();
            auto first = std::async(std::launch::async, [&] {
                return resources.store_stream(
                    [&](auto& out, const auto&) {
                        out << std::string(1500000, 'x');
                        written.set_value();
                        released.wait();
                    },
                    "text/plain");
            });
            struct ReleaseProducer {
                std::promise<void>& p;
                ~ReleaseProducer() {
                    try {
                        p.set_value();
                    } catch (...) {
                    }
                }
            } release_producer{finish};
            written.get_future().get();
            expect_error("ResourceTooLarge", [&] {
                resources.store_stream(
                    [](auto& out, const auto&) { out << std::string(1500000, 'y'); }, "text/plain");
            });
            finish.set_value();
            first.get();
            check(resources.storage_stats()["disk_bytes"] == 1500000,
                  "Disk quota also accounts for unfinished concurrent producers");
        }
        PerformanceOptions perf;
        perf.records = 3;
        perf.labels = 2;
        perf.jsonl_file = folder / "performance.jsonl";
        auto monitor = std::make_shared<PerformanceMonitor>(perf);
        Registry registry(Policy::allow_all());
        registry.set_performance_monitor(monitor);
        registry.add({"read",
                      "Read",
                      {{"type", "object"}},
                      {{"type", "object"}},
                      true,
                      [](const auto& a, const auto&) {
                          std::this_thread::sleep_for(6ms);
                          return a;
                      }});
        registry.add({"fail",
                      "Fail",
                      {{"type", "object"}},
                      Json::object(),
                      false,
                      [](const auto&, const auto&) -> Json {
                          throw Error("ExpectedFailure", "fixture-secret");
                      }});
        registry.invoke("read", {{"body", "fixture-secret"}});
        expect_error("ExpectedFailure", [&] { registry.invoke("fail", Json::object()); });
        auto rows = monitor->drain();
        check(rows.size() == 2 && rows[0]["elapsed_ms"].get<double>() >= 5,
              "Performance records operation time and terminal failures");
        check(rows.dump().find("fixture-secret") == std::string::npos &&
                  monitor->snapshot()["operations"].size() == 2,
              "Performance metadata contains no argument, result or error body");
        for (int n = 0; n < 5; ++n)
            registry.invoke("read", Json::object());
        check(monitor->drain().size() == 3 &&
                  monitor->snapshot()["operations"].size() <= perf.labels,
              "Performance row and aggregate label limits remain bounded");
        registry.set_performance_monitor(nullptr);
        monitor.reset();
        std::ifstream log(perf.jsonl_file);
        std::string contents((std::istreambuf_iterator<char>(log)), {});
        check(contents.find("ExpectedFailure") != std::string::npos &&
                  contents.find("fixture-secret") == std::string::npos,
              "Private asynchronous performance log flushes metadata only");
        auto feed = std::make_shared<ChangeFeed>(2);
        registry.set_change_feed(feed);
        registry.remove("fail");
        registry.replace({"read",
                          "Changed",
                          {{"type", "object"}},
                          Json::object(),
                          true,
                          [](const auto&, const auto&) { return Json::object(); }});
        registry.remove("read");
        auto batch = feed->read(0);
        check(batch.missed && batch.sequence == 3 && batch.changes.size() == 2,
              "Bounded notification journal signals missed changes");
        Cancellation stop;
        stop.cancel();
        expect_error("Cancelled", [&] { feed->read(3, stop, 1s); });
        std::cout << checks << " optional core checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
