#include "test.hpp"
#include <cymcp/local.hpp>
#include <fstream>
#include <httplib.h>
using namespace cymcp;

namespace {
struct Scratch {
    std::filesystem::path root =
        std::filesystem::current_path() / ("cuda-test-" + random_token(16));
    Scratch() {
        private_directory(root);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--verify-nvidia-download") {
            // Explicit integration exercise, excluded from CTest. Never loads the library.
            DependencyManager manager({std::filesystem::u8path(argv[2]), Policy::allow_all()});
            auto p = cuda_runtime_package();
            const auto installed = manager.install(p);
            check(manager.verify(p), "Official NVIDIA runtime package verifies");
            std::cout << path_utf8(installed / "runtime") << '\n';
            return 0;
        }
        if (argc != 3)
            return 2;
        Scratch scratch;
        std::string bytes;
        for (int n = 0; n < 16384; ++n)
            bytes.append("bin\0\r\n\xff", 7);
        const std::string license = "sample license";
        const auto source = scratch.root / "input";
        std::filesystem::create_directories(source / "vendor");
        std::ofstream(source / "vendor" / "library", std::ios::binary)
            .write(bytes.data(), bytes.size());
        std::ofstream(source / "vendor" / "LICENSE", std::ios::binary) << license;
        auto archive_path = scratch.root / "fixture.tar";
        ProcessOptions command;
        command.executable = std::filesystem::u8path(argv[1]);
        command.arguments = {"-E",    "tar", "cf", path_utf8(archive_path), "--format=gnutar",
                             "vendor"};
        command.cwd = source;
        Process pack(command);
        pack.close_input();
        check(pack.wait() == 0, "Fixture archive created");
        std::ifstream archive_file(archive_path, std::ios::binary);
        std::string archive{std::istreambuf_iterator<char>(archive_file), {}};
        httplib::Server server;
        std::atomic<int> downloads{0};
        server.Get("/package", [&](const auto&, auto& response) {
            ++downloads;
            response.set_content(archive, "application/octet-stream");
        });
        const int port = server.bind_to_any_port("127.0.0.1");
        check(port > 0, "Dependency fixture binds");
        std::thread serving([&] { server.listen_after_bind(); });
        struct Stop {
            httplib::Server& server;
            std::thread& worker;
            ~Stop() {
                server.stop();
                worker.join();
            }
        } stop{server, serving};
        server.wait_until_ready();
        Package p;
        p.id = "runtime-test";
        p.version = "1";
        p.platform = platform_id();
        const std::string library =
            p.platform == "windows-x64" ? "cudart64_12.dll" : "libcudart.so.12";
        p.artifacts.push_back(
            {"fixture.tar",
             "http://127.0.0.1:" + std::to_string(port) + "/package",
             sha256(archive),
             archive.size(),
             false,
             {{"vendor/library", "runtime/" + library, sha256(bytes), bytes.size()},
              {"vendor/LICENSE", "LICENSE", sha256(license), license.size()}}});
        const auto cache = scratch.root / "cache";
        DependencyManager manager({cache, Policy::allow_all(), true});
        DependencyManager denied({cache, Policy::deny_all(), true});
        expect_error("AccessDenied", [&] { denied.install(p); });
        check(downloads == 0, "Denied installation performs no download");
        auto bad = p;
        bad.artifacts[0].members[0].sha256 = sha256("other bytes");
        expect_error("IntegrityError", [&] { manager.install(bad); });
        check(std::filesystem::is_empty(cache),
              "Failed extraction rolls back the staging directory");
        auto undersized = p;
        undersized.artifacts[0].members[0].size = 9000;
        expect_error("IntegrityError", [&] { manager.install(undersized); });
        check(std::filesystem::is_empty(cache), "Over-budget binary extraction leaves no files");
        auto directory_member = p;
        directory_member.artifacts[0].members[0].member_path = "vendor";
        expect_error("ExtractionFailed", [&] { manager.install(directory_member); });
        check(std::filesystem::is_empty(cache), "A directory cannot be installed as a file");
        const auto installed = manager.install(p);
        check(manager.verify(p) && sha256_file(installed / "runtime" / library) == sha256(bytes),
              "Verified runtime is materialized under its loader filename");
        check(sha256_file(installed / "LICENSE") == sha256(license), "Vendor license preserved");
        const int fetched = downloads.load();
        check(manager.install(p) == installed && downloads == fetched, "Verified cache reused");
        if (p.platform == "windows-x64" || p.platform == "linux-x64") {
            LocalEngineOptions options;
            options.model = source / "example.gguf";
            std::ofstream(options.model) << "model fixture";
            options.private_work_directory = scratch.root / "engine";
            options.cuda_runtime_directory = installed / "runtime";
            options.startup_timeout = std::chrono::seconds(5);
            options.process.executable = std::filesystem::u8path(argv[2]);
            options.process.arguments = {"--engine-fixture", "--expect-runtime",
                                         path_utf8(options.cuda_runtime_directory)};
            options.process.environment[p.platform == "windows-x64" ? "Path" : "LD_LIBRARY_PATH"] =
                "host-extra";
            LocalEngine engine(options);
            engine.start();
            check(!engine.base_url().empty(),
                  "Owned engine receives the private runtime search path");
            engine.stop();
        }
        std::ofstream(installed / "runtime" / library, std::ios::binary) << "damaged";
        check(!manager.verify(p), "Changed extracted runtime fails verification");
        manager.uninstall(p);
        check(!std::filesystem::exists(installed), "Managed runtime removed");
        if (p.platform == "windows-x64" || p.platform == "linux-x64") {
            const auto live = cuda_runtime_package();
            check(live.platform == p.platform && live.artifacts.size() == 1 &&
                      live.artifacts[0].members[0].relative_path == "runtime/" + library &&
                      live.artifacts[0].members[1].relative_path == "LICENSE" &&
                      !live.requires_elevation && live.installer.empty(),
                  "Native CUDART recipe selects the platform runtime and license");
        } else {
            expect_error("UnsupportedPlatform", [] { cuda_runtime_package(); });
        }
        std::cout << checks << " CUDA package checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
