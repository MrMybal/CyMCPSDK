#pragma once
#ifdef _WIN32
#include <cymcp/core.hpp>
#include <shlwapi.h>
#include <windows.h>

namespace chat_example {
inline std::wstring help_url(const std::filesystem::path& executable, bool french) {
    const auto manual = std::filesystem::absolute(executable).parent_path() / L"Documentation" /
                        L"Documentation.html";
    if (!std::filesystem::is_regular_file(manual))
        throw cymcp::Error("DocumentationMissing", "Keep Documentation beside the application");
    std::wstring url(3 * 32768 + 32, L'\0');
    DWORD size = static_cast<DWORD>(url.size());
    if (FAILED(UrlCreateFromPathW(manual.root_path().c_str(), url.data(), &size, 0)))
        throw cymcp::Error("InvalidOptions", "Cannot create documentation URL");
    url.resize(wcslen(url.c_str()));
    // UrlCreateFromPathW uses legacy byte escapes for some Unicode characters.
    // Keep its native file root and encode each path segment explicitly as UTF-8.
    for (const auto& part : manual.relative_path()) {
        const auto segment = part.wstring();
        std::wstring escaped(9 * segment.size() + 32, L'\0');
        size = static_cast<DWORD>(escaped.size());
        if (FAILED(UrlEscapeW(segment.c_str(), escaped.data(), &size,
                              URL_ESCAPE_AS_UTF8 | URL_ESCAPE_SEGMENT_ONLY | URL_ESCAPE_PERCENT)))
            throw cymcp::Error("InvalidOptions", "Cannot encode documentation URL");
        escaped.resize(wcslen(escaped.c_str()));
        if (url.back() != L'/')
            url += L'/';
        url += escaped;
    }
    url += french ? L"#/fr/chat-lab" : L"#/en/chat-lab";
    return url;
}
} // namespace chat_example

#else

#include <cymcp/core.hpp>
namespace chat_example {
inline std::string file_url(const std::filesystem::path& path) {
    const auto bytes = cymcp::path_utf8(std::filesystem::absolute(path));
    constexpr char hex[] = "0123456789ABCDEF";
    std::string url = "file://";
    for (unsigned char c : bytes) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' ||
            c == '_' || c == '.' || c == '~')
            url += char(c);
        else {
            url += '%';
            url += hex[c >> 4];
            url += hex[c & 15];
        }
    }
    return url;
}
inline std::string help_url(const std::filesystem::path& executable, bool french) {
    const auto manual = std::filesystem::absolute(executable).parent_path() /
                        "Documentation" / "Documentation.html";
    if (!std::filesystem::is_regular_file(manual))
        throw cymcp::Error("DocumentationMissing", "Keep Documentation beside the application");
    return file_url(manual) + (french ? "#/fr/chat-lab" : "#/en/chat-lab");
}
} // namespace chat_example
#endif
