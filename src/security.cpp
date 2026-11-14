#include <array>
#include <cymcp/core.hpp>
#include <fstream>
#include <picosha2.h>
#ifdef _WIN32
#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace cymcp {
std::string random_token(std::size_t size) {
    if (size < 16 || size > 1024)
        throw Error("InvalidOptions", "Token length must be 16..1024 bytes");
    std::vector<unsigned char> bytes(size);
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(size),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw Error("SystemError", "Secure randomness unavailable");
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        throw Error("SystemError", "Secure randomness unavailable");
    std::size_t at = 0;
    while (at < size) {
        auto n = read(fd, bytes.data() + at, size - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            throw Error("SystemError", "Secure random read failed");
        }
        at += static_cast<std::size_t>(n);
    }
    close(fd);
#endif
    std::string result;
    result.reserve(size * 2);
    constexpr char hex[] = "0123456789abcdef";
    for (auto b : bytes) {
        result += hex[b >> 4];
        result += hex[b & 15];
    }
    return result;
}
bool constant_time_equal(const std::string& a, const std::string& b) noexcept {
    if (a.size() != b.size())
        return false;
    volatile unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference =
            static_cast<unsigned char>(difference | static_cast<unsigned char>(a[i] ^ b[i]));
    return difference == 0;
}
std::string sha256(const std::string& s) {
    return picosha2::hash256_hex_string(s);
}
std::string sha256_file(const std::filesystem::path& path, const Cancellation& cancel) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Error("FileError", "Cannot open file for verification");
    picosha2::hash256_one_by_one hash;
    std::array<char, 65536> buffer{};
    while (in) {
        cancel.check();
        in.read(buffer.data(), buffer.size());
        hash.process(buffer.begin(), buffer.begin() + in.gcount());
    }
    if (!in.eof())
        throw Error("FileError", "File read failed");
    hash.finish();
    return picosha2::get_hash_hex_string(hash);
}
void private_directory(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
        throw Error("UnsafePath", "Private directory cannot be a symlink");
#ifdef _WIN32
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw Error("SystemError", "Cannot inspect directory owner");
    struct TokenCleanup {
        HANDLE h;
        ~TokenCleanup() {
            CloseHandle(h);
        }
    } cleanup{token};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> storage(size);
    if (!GetTokenInformation(token, TokenUser, storage.data(), size, &size))
        throw Error("SystemError", "Cannot inspect directory owner");
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid))
        throw Error("SystemError", "Cannot resolve directory owner");
    std::wstring descriptor = L"D:P(A;OICI;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR security = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
                                                              &security, nullptr))
        throw Error("SystemError", "Cannot construct private directory ACL");
    struct SecurityCleanup {
        PSECURITY_DESCRIPTOR p;
        ~SecurityCleanup() {
            LocalFree(p);
        }
    } security_cleanup{security};
    PACL dacl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    GetSecurityDescriptorDacl(security, &present, &dacl, &defaulted);
    auto name = path.wstring();
    if (SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, dacl, nullptr) != ERROR_SUCCESS)
        throw Error("SystemError", "Cannot protect private directory");
#else
    std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
#endif
}
std::string platform_id() {
#ifdef _WIN32
    std::string os = "windows";
#elif defined(__APPLE__)
    std::string os = "macos";
#elif defined(__linux__)
    std::string os = "linux";
#else
    std::string os = "unsupported";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
    return os + "-arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return os + "-x64";
#else
    return os + "-unknown";
#endif
}
} // namespace cymcp
