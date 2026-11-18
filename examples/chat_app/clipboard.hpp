#pragma once
#include <cymcp/process.hpp>
#pragma push_macro("pascal")
#undef pascal
#include <cyui/modules/DocumentIO.h>
#pragma pop_macro("pascal")

#include <windows.h>

#include <cstring>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace chat_example {
class ClipboardAttachments {
    using Paths = std::vector<std::filesystem::path>;
    static constexpr std::size_t max_image = 32 * 1024 * 1024;
    std::map<std::filesystem::path, std::unique_ptr<cymcp::PrivateFile>> files_;
    static std::string bytes(HANDLE handle, std::size_t limit) {
        const auto size = handle ? GlobalSize(handle) : 0;
        if (!size)
            return {};
        if (size > limit)
            throw cymcp::Error("ResponseTooLarge", "Clipboard attachment exceeds its size limit");
        const auto* data = static_cast<const char*>(GlobalLock(handle));
        if (!data)
            return {};
        struct Unlock {
            HANDLE handle;
            ~Unlock() {
                GlobalUnlock(handle);
            }
        } unlock{handle};
        return {data, size};
    }
    static std::string dib_to_png(const std::string& dib) {
        using Microsoft::WRL::ComPtr;
        BITMAPINFOHEADER info{};
        if (dib.size() < sizeof(info))
            return {};
        std::memcpy(&info, dib.data(), sizeof(info));
        if (info.biSize < sizeof(info) || info.biSize > dib.size() || info.biWidth <= 0 ||
            !info.biHeight || info.biPlanes != 1 ||
            std::int64_t(info.biWidth) * std::abs(std::int64_t(info.biHeight)) > 16 * 1024 * 1024)
            return {};
        const auto colors =
            info.biClrUsed ? info.biClrUsed : (info.biBitCount <= 8 ? 1U << info.biBitCount : 0U);
        const auto masks =
            info.biSize == sizeof(info) && info.biCompression == BI_BITFIELDS ? 3U : 0U;
        const auto offset =
            std::uint64_t(info.biSize) + (std::uint64_t(colors) + masks) * sizeof(DWORD);
        if (offset >= dib.size())
            return {};
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfSize = static_cast<DWORD>(sizeof(header) + dib.size());
        header.bfOffBits = static_cast<DWORD>(sizeof(header) + offset);
        std::string bitmap(reinterpret_cast<const char*>(&header), sizeof(header));
        bitmap += dib;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> source;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IStream> output;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> target;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&source)) ||
            FAILED(source->InitializeFromMemory(reinterpret_cast<BYTE*>(bitmap.data()),
                                                header.bfSize)) ||
            FAILED(factory->CreateDecoderFromStream(source.Get(), nullptr,
                                                    WICDecodeMetadataCacheOnDemand, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)) ||
            FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &output)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(output.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&target, nullptr)) ||
            FAILED(target->Initialize(nullptr)) ||
            FAILED(target->WriteSource(frame.Get(), nullptr)) || FAILED(target->Commit()) ||
            FAILED(encoder->Commit()))
            return {};
        STATSTG stat{};
        if (FAILED(output->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > max_image)
            throw cymcp::Error("ResponseTooLarge", "Clipboard image exceeds its size limit");
        std::string png(static_cast<std::size_t>(stat.cbSize.QuadPart), '\0');
        ULONG read = 0;
        if (FAILED(output->Seek({}, STREAM_SEEK_SET, nullptr)) ||
            FAILED(output->Read(png.data(), static_cast<ULONG>(png.size()), &read)) ||
            read != png.size())
            return {};
        return png;
    }
    // GlobalSize may include allocator padding; stop at PNG's final chunk before embedding.
    static std::size_t png_length(std::string_view data) {
        if (!data.starts_with("\x89PNG\r\n\x1a\n"))
            return 0;
        for (std::size_t offset = 8; data.size() - offset >= 12;) {
            std::uint32_t size = 0;
            for (const unsigned char byte : data.substr(offset, 4))
                size = (size << 8) | byte;
            if (size > data.size() - offset - 12)
                return 0;
            if (data.substr(offset + 4, 4) == "IEND")
                return size ? 0 : offset + 12;
            offset += size + 12;
        }
        return 0;
    }

  public:
    static Paths dropped(HDROP drop) {
        Paths paths;
        if (drop) {
            const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            if (count > 256)
                throw cymcp::Error("InvalidContent", "At most 256 attachments");
            for (UINT n = 0; n < count; ++n) {
                std::wstring path(DragQueryFileW(drop, n, nullptr, 0) + 1, L'\0');
                const auto length =
                    DragQueryFileW(drop, n, path.data(), static_cast<UINT>(path.size()));
                if (length)
                    paths.emplace_back(path.substr(0, length));
            }
        }
        return paths;
    }
    bool paste_from(const std::function<HANDLE(UINT)>& get, std::string& replacement,
                    Paths& paths) {
        paths = dropped(static_cast<HDROP>(get(CF_HDROP)));
        if (!paths.empty())
            return true;
        std::string image, name;
        for (auto format : {L"PNG", L"image/png", L"image/jpeg", L"JFIF"}) {
            auto data = bytes(get(RegisterClipboardFormatW(format)), max_image);
            if (auto length = png_length(data)) {
                data.resize(length);
                name = "clipboard.png";
            } else if (data.starts_with("\xff\xd8\xff")) {
                const auto end = data.rfind("\xff\xd9");
                if (end == data.npos)
                    continue;
                data.resize(end + 2);
                name = "clipboard.jpg";
            } else
                continue;
            image = std::move(data);
            break;
        }
        if (image.empty())
            for (auto format : {CF_DIBV5, CF_DIB}) {
                auto dib = bytes(get(format), 64 * 1024 * 1024);
                if (!dib.empty() && !(image = dib_to_png(dib)).empty()) {
                    name = "clipboard.png";
                    break;
                }
            }
        if (!image.empty()) {
            auto file = std::make_unique<cymcp::PrivateFile>(name, image);
            auto path = file->path();
            files_.emplace(path, std::move(file));
            paths.push_back(std::move(path));
            return true;
        }
        auto html = bytes(get(RegisterClipboardFormatW(L"HTML Format")), 16 * 1024 * 1024);
        if (html.empty())
            html = bytes(get(RegisterClipboardFormatW(L"text/html")), 16 * 1024 * 1024);
        if (html.empty())
            return false; // The composer handles ordinary Unicode text, including Markdown.
        if (const auto nul = html.find('\0'); nul != html.npos)
            html.resize(nul);
        constexpr std::string_view marker = "<!--StartFragment-->";
        const auto begin = html.find(marker), end = html.find("<!--EndFragment-->");
        if (begin != html.npos && end != html.npos && end >= begin + marker.size())
            html = html.substr(begin + marker.size(), end - begin - marker.size());
        else if (html.starts_with("Version:") && html.find('<') != html.npos)
            html.erase(0, html.find('<'));
        replacement = cyui::markdownFromHtml(html);
        return !replacement.empty();
    }
    bool paste(HWND window, std::string& text, Paths& paths) {
        if (!OpenClipboard(window))
            return false;
        struct Close {
            ~Close() {
                CloseClipboard();
            }
        } close;
        return paste_from([](UINT format) { return GetClipboardData(format); }, text, paths);
    }
    void discard_unused(const Paths& pending) {
        std::erase_if(files_, [&](const auto& file) {
            return std::find(pending.begin(), pending.end(), file.first) == pending.end();
        });
    }
};
} // namespace chat_example
