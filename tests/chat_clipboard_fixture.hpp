#pragma once
#include "../examples/chat_app/clipboard.hpp"
#include "test.hpp"
#include <shlobj.h>

// HGLOBALs have the same layout as Win32 clipboard formats and never replace the user's clipboard.
struct ChatClipboardFixture {
    chat_example::ClipboardAttachments host;
    UINT format = 0;
    HGLOBAL data = nullptr;
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    unsigned attempts = 0;
    ~ChatClipboardFixture() {
        if (data)
            GlobalFree(data);
        if (SUCCEEDED(apartment))
            CoUninitialize();
    }
    void set(UINT type, const std::string& payload) {
        if (data)
            GlobalFree(data);
        format = type;
        data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, payload.size());
        check(data != nullptr, "Allocate isolated Win32 clipboard data");
        auto* memory = GlobalLock(data);
        check(memory != nullptr, "Lock isolated Win32 clipboard data");
        std::memcpy(memory, payload.data(), payload.size());
        GlobalUnlock(data);
    }
    void screenshot(bool v5 = false) {
        BITMAPV5HEADER info{};
        info.bV5Size = v5 ? sizeof(info) : sizeof(BITMAPINFOHEADER);
        info.bV5Width = 2;
        info.bV5Height = 1;
        info.bV5Planes = 1;
        info.bV5BitCount = 24;
        info.bV5SizeImage = 8;
        std::string dib(reinterpret_cast<const char*>(&info), info.bV5Size);
        dib.append("\xff\0\0\0\xff\0\0\0", 8);
        set(v5 ? CF_DIBV5 : CF_DIB, dib);
    }
    bool paste(std::string& replacement, std::vector<std::filesystem::path>& paths) {
        ++attempts;
        return host.paste_from([this](UINT f) { return f == format ? data : nullptr; }, replacement,
                               paths);
    }
    void check_formats(const std::filesystem::path& folder) {
        screenshot(true);
        std::string text;
        std::vector<std::filesystem::path> paths;
        check(paste(text, paths) && paths.size() == 1, "Win32 DIBV5 is imported as a screenshot");
        auto image = cymcp::attachment_from_file(paths.front());
        check(image.at("mimeType") == "image/png",
              "Clipboard screenshots become portable PNG attachments");
        std::ifstream file(paths.front(), std::ios::binary);
        std::string png{std::istreambuf_iterator<char>(file), {}};
        check(png.starts_with("\x89PNG\r\n\x1a\n"), "Win32 bitmap was encoded into a PNG");
        file.close();
        set(RegisterClipboardFormatW(L"PNG"), png + "allocation padding");
        check(paste(text, paths) && paths.size() == 1 && paths.front().extension() == ".png" &&
                  cymcp::attachment_from_file(paths.front()).at("data") == image.at("data"),
              "Registered native PNG preserves the image without clipboard allocation padding");
        const auto path = folder / L"copy é.md";
        DROPFILES drop{};
        drop.pFiles = sizeof(drop);
        drop.fWide = TRUE;
        std::wstring names = path.wstring();
        names.append(2, L'\0');
        std::string drop_data(reinterpret_cast<const char*>(&drop), sizeof(drop));
        drop_data.append(reinterpret_cast<const char*>(names.data()),
                         names.size() * sizeof(wchar_t));
        set(CF_HDROP, drop_data);
        check(paste(text, paths) && paths.size() == 1 && paths.front() == path &&
                  chat_example::ClipboardAttachments::dropped(static_cast<HDROP>(data)) == paths,
              "A Unicode file list is shared by clipboard paste and Explorer drop");
        set(RegisterClipboardFormatW(L"HTML Format"),
            "Version:1.0\r\n<html><!--StartFragment--><strong>rich</strong><!--EndFragment--></"
            "html>");
        check(paste(text, paths) && paths.empty() && text.find("**rich**") != std::string::npos &&
                  text.find("Version") == std::string::npos,
              "Rich clipboard fragment becomes Markdown");
        format = CF_UNICODETEXT;
        check(!paste(text, paths), "Plain Unicode clipboard text uses ordinary composer paste");
        host.discard_unused({});
        screenshot();
        attempts = 0;
    }
};
