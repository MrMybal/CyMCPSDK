#include <cymcp/local.hpp>
#include <fstream>
#include <regex>
#include <set>
namespace cymcp {
namespace {
bool segment(const std::string& s) {
    return !s.empty() && s.size() <= 96 && s != "." && s != ".." && s.back() != '.' &&
           std::regex_match(s, std::regex("[A-Za-z0-9_.-]+"));
}
void validate_package(const Package& p, const DependencyOptions& opts) {
    if (!segment(p.id) || !segment(p.version) || !segment(p.platform) || p.artifacts.empty())
        throw Error("InvalidManifest", "Invalid package identity or empty artifact list");
    if (p.platform != platform_id())
        throw Error("UnsupportedPlatform", "Package targets a different platform");
    std::set<std::string> paths;
    std::uint64_t total = 0;
    for (const auto& a : p.artifacts) {
        auto relative = std::filesystem::path(a.relative_path);
        if (a.relative_path.empty() || relative.is_absolute() || relative.has_root_path() ||
            a.relative_path.find('\\') != std::string::npos)
            throw Error("InvalidManifest", "Artifact path must be portable and relative");
        for (const auto& component : relative) {
            auto s = component.string();
            if (!segment(s))
                throw Error("InvalidManifest", "Invalid path component");
            auto base = s.substr(0, s.find('.'));
            std::transform(base.begin(), base.end(), base.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (std::regex_match(base, std::regex("CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]")))
                throw Error("InvalidManifest", "Reserved path component");
        }
        std::string key = a.relative_path;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!paths.insert(key).second || key == "receipt.json")
            throw Error("InvalidManifest", "Duplicate or reserved artifact path");
        if (a.sha256.size() != 64 ||
            a.sha256.find_first_not_of("0123456789abcdef") != std::string::npos || !a.size ||
            a.size > opts.max_package_bytes - total)
            throw Error("InvalidManifest", "Invalid artifact hash or size budget");
        total += a.size;
        auto url = parse_url(a.url);
        if (url.scheme != "https" && !(opts.allow_loopback_http_for_tests && url.loopback))
            throw Error("InvalidManifest", "Artifacts require HTTPS");
    }
    if (!p.installer.empty()) {
        auto found = std::find_if(p.artifacts.begin(), p.artifacts.end(), [&](const Artifact& a) {
            return a.relative_path == p.installer && a.executable;
        });
        if (found == p.artifacts.end())
            throw Error("InvalidManifest", "Installer must be a verified executable artifact");
    }
}
std::filesystem::path target(const Package& p, const DependencyOptions& opts) {
    return std::filesystem::absolute(opts.cache_directory) /
           (p.id + "-" + p.version + "-" + p.platform);
}
void safe_child(const std::filesystem::path& root, const std::filesystem::path& child) {
    auto base = std::filesystem::weakly_canonical(root),
         value = std::filesystem::weakly_canonical(child);
    auto a = base.begin(), b = value.begin();
    for (; a != base.end() && b != value.end(); ++a, ++b)
        if (*a != *b)
            throw Error("UnsafePath", "Path escapes managed cache");
    if (a != base.end() || b == value.end())
        throw Error("UnsafePath", "Path escapes managed cache");
    auto relative = child.lexically_relative(root);
    auto cursor = root;
    for (const auto& part : relative) {
        cursor /= part;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(cursor)))
            throw Error("UnsafePath", "Symlinks are forbidden in managed installations");
    }
}
Json receipt(const Package& p) {
    Json files = Json::array();
    for (const auto& a : p.artifacts)
        files.push_back({{"path", a.relative_path}, {"sha256", a.sha256}, {"size", a.size}});
    return {{"format", 1},
            {"id", p.id},
            {"version", p.version},
            {"platform", p.platform},
            {"files", files}};
}
struct DirectoryLock {
    std::filesystem::path path;
    explicit DirectoryLock(std::filesystem::path p) : path(std::move(p)) {
        if (!std::filesystem::create_directory(path))
            throw Error("Busy", "Package installation is locked; recover a stale lock explicitly "
                                "after confirming no installer is running");
    }
    ~DirectoryLock() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};
} // namespace
DependencyManager::DependencyManager(DependencyOptions options) : options_(std::move(options)) {
    if (options_.cache_directory.empty())
        throw Error("InvalidOptions", "Managed cache directory required");
    options_.cache_directory = std::filesystem::absolute(options_.cache_directory);
}
bool DependencyManager::verify(const Package& p, const CallContext& context) const {
    validate_package(p, options_);
    auto directory = target(p, options_);
    safe_child(options_.cache_directory, directory);
    if (!std::filesystem::is_directory(directory))
        return false;
    auto marker = directory / "receipt.json";
    safe_child(options_.cache_directory, marker);
    std::ifstream in(marker);
    if (!in || std::filesystem::file_size(marker) > 1024 * 1024)
        return false;
    Json stored;
    try {
        in >> stored;
    } catch (...) {
        return false;
    }
    if (stored != receipt(p))
        return false;
    for (const auto& file : p.artifacts) {
        auto path = directory / file.relative_path;
        safe_child(options_.cache_directory, path);
        if (!std::filesystem::is_regular_file(path) ||
            std::filesystem::file_size(path) != file.size ||
            sha256_file(path, context.cancellation) != file.sha256)
            return false;
    }
    return true;
}
std::filesystem::path DependencyManager::install(const Package& p,
                                                 const CallContext& context) const {
    validate_package(p, options_);
    if (!options_.policy.allows(context, "dependencies.install", receipt(p)))
        throw Error("AccessDenied", "Dependency installation denied by host");
    private_directory(options_.cache_directory);
    auto destination = target(p, options_);
    safe_child(options_.cache_directory, destination);
    auto lockpath = destination;
    lockpath += ".lock";
    safe_child(options_.cache_directory, lockpath);
    DirectoryLock lock(lockpath);
    if (verify(p, context))
        return destination;
    if (std::filesystem::exists(destination))
        throw Error("CorruptInstallation",
                    "Existing installation failed verification; repair requires explicit removal");
    auto stage = options_.cache_directory / ("stage-" + random_token(16));
    private_directory(stage);
    safe_child(options_.cache_directory, stage);
    try {
        for (const auto& file : p.artifacts) {
            context.cancellation.check();
            auto path = stage / file.relative_path;
            safe_child(options_.cache_directory, path);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary);
            if (!output)
                throw Error("FileError", "Cannot create staged artifact");
            std::string url = file.url;
            bool downloaded = false;
            for (int redirect = 0; redirect < 6; ++redirect) {
                std::uint64_t written = 0;
                output.seekp(0);
                HttpRequest request;
                request.url = url;
                request.timeout = std::chrono::minutes(30);
                request.max_response_bytes = static_cast<std::size_t>(file.size);
                auto response = HttpClient().perform(
                    request, context.cancellation, [&](const char* data, std::size_t size) {
                        if (size > file.size - written)
                            throw Error("IntegrityError", "Download exceeds declared size");
                        output.write(data, static_cast<std::streamsize>(size));
                        if (!output)
                            throw Error("FileError", "Artifact write failed");
                        written += size;
                        context.emit("DownloadProgress", {{"package", p.id},
                                                          {"file", file.relative_path},
                                                          {"received", written},
                                                          {"total", file.size}});
                        return true;
                    });
                if (response.status >= 300 && response.status < 400) {
                    auto it = response.headers.find("location");
                    if (it == response.headers.end())
                        throw Error("DownloadError", "Redirect missing location");
                    auto next = it->second;
                    auto parsed = parse_url(next);
                    auto previous = parse_url(url);
                    if (parsed.scheme != "https" ||
                        (parsed.host != previous.host &&
                         std::find(p.redirect_hosts.begin(), p.redirect_hosts.end(), parsed.host) ==
                             p.redirect_hosts.end()))
                        throw Error("DownloadError",
                                    "Redirect host is not in the trusted package catalogue");
                    url = std::move(next);
                    output.close();
                    output.open(path, std::ios::binary | std::ios::trunc);
                    continue;
                }
                if (response.status != 200 || written != file.size)
                    throw Error("DownloadError", "Incomplete artifact download");
                downloaded = true;
                break;
            }
            output.close();
            if (!downloaded || sha256_file(path, context.cancellation) != file.sha256)
                throw Error("IntegrityError", "Artifact SHA-256 mismatch");
#ifndef _WIN32
            if (file.executable)
                std::filesystem::permissions(path, std::filesystem::perms::owner_exec,
                                             std::filesystem::perm_options::add);
#endif
            context.emit("ArtifactVerified", {{"package", p.id}, {"file", file.relative_path}});
        }
        {
            std::ofstream marker(stage / "receipt.json");
            marker << receipt(p).dump(2);
            if (!marker)
                throw Error("FileError", "Cannot write installation receipt");
        }
        context.cancellation.check();
        std::filesystem::rename(stage, destination);
        return destination;
    } catch (...) {
        safe_child(options_.cache_directory, stage);
        std::filesystem::remove_all(stage);
        throw;
    }
}
int DependencyManager::run_installer(const Package& p, const CallContext& context,
                                     ElevationHandler elevated) const {
    validate_package(p, options_);
    if (p.installer.empty())
        throw Error("InvalidManifest", "Package has no installer");
    if (!options_.policy.allows(context, "dependencies.runInstaller", receipt(p)))
        throw Error("AccessDenied", "Installer denied by host");
    if (!verify(p, context))
        throw Error("IntegrityError", "Verify installation before running installer");
    ProcessOptions opts;
    opts.executable = target(p, options_) / p.installer;
    opts.arguments = p.installer_arguments;
    opts.cwd = target(p, options_);
    int code = 0;
    if (p.requires_elevation) {
        if (!elevated)
            throw Error("NeedsElevation", "Host must provide the platform elevation flow");
        code = elevated(opts, context);
    } else {
        Process installer(opts);
        installer.close_input();
        ProcessLine line;
        while (installer.next(line, context.cancellation)) {
        }
        code = installer.wait(context.cancellation);
    }
    if (code != 0 && code != 3010)
        throw Error("InstallFailed", "Installer returned exit code " + std::to_string(code));
    context.emit(code == 3010 ? "RestartRequired" : "DependencyInstalled", {{"package", p.id}});
    return code;
}
void DependencyManager::uninstall(const Package& p, const CallContext& context) const {
    validate_package(p, options_);
    if (!options_.policy.allows(context, "dependencies.uninstall", receipt(p)))
        throw Error("AccessDenied", "Removal denied by host");
    auto directory = target(p, options_);
    safe_child(options_.cache_directory, directory);
    if (!std::filesystem::exists(directory))
        return;
    auto lockpath = directory;
    lockpath += ".lock";
    DirectoryLock lock(lockpath);
    auto marker = directory / "receipt.json";
    safe_child(options_.cache_directory, marker);
    std::ifstream in(marker);
    Json stored;
    if (!in || std::filesystem::file_size(marker) > 1024 * 1024)
        throw Error("UnsafePath", "Installation has no managed receipt");
    in >> stored;
    in.close();
    if (stored != receipt(p))
        throw Error("UnsafePath", "Installation receipt does not match manifest");
    context.cancellation.check();
    std::filesystem::remove_all(directory);
}
} // namespace cymcp
