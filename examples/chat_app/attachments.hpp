#pragma once
// Windows example adapters only: the SDK and CyUI widgets remain portable.
#include <charconv>
#include <commdlg.h>
#include <cymcp/chat_ui.hpp>
#pragma push_macro("pascal")
#undef pascal
#include <cyui/modules/DocumentViews.h>
#pragma pop_macro("pascal")
#include <d3d11.h>
#include <fstream>
#include <wincodec.h>
#include <wincrypt.h>
#include <windows.h>
#include <wrl/client.h>

namespace chat_example {
inline std::vector<unsigned char> attachment_bytes(const cymcp::ChatAttachment& attachment) {
    if (!attachment.content)
        throw cymcp::Error("AttachmentUnavailable", "Attachment is still loading");
    const auto& block = *attachment.content;
    const bool resource = block.value("type", std::string()) == "resource";
    const auto& payload = resource ? block.at("resource") : block;
    if (payload.contains("text")) {
        const auto& text = payload.at("text").get_ref<const std::string&>();
        return {text.begin(), text.end()};
    }
    const auto& data = payload.at(resource ? "blob" : "data").get_ref<const std::string&>();
    DWORD count = 0;
    if (!CryptStringToBinaryA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64,
                              nullptr, &count, nullptr, nullptr))
        throw cymcp::Error("InvalidContent", "Invalid attachment bytes");
    std::vector<unsigned char> bytes(count);
    if (!CryptStringToBinaryA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64,
                              bytes.data(), &count, nullptr, nullptr))
        throw cymcp::Error("InvalidContent", "Invalid attachment bytes");
    bytes.resize(count);
    return bytes;
}
class Attachments {
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Pixels {
        UINT width = 0, height = 0;
        std::vector<unsigned char> bytes;
    };
    struct Image {
        std::future<Pixels> loading;
        ComPtr<ID3D11ShaderResourceView> texture;
        UINT width = 0, height = 0;
        std::uint64_t used = 0;
    };
    HWND window_;
    ComPtr<ID3D11Device> device_;
    std::map<std::string, Image> images_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> retired_; // retained through the current Render
    std::future<void> saving_;
    std::optional<cymcp::ChatAttachment> selected_;
    cyui::Document document_;
    cyui::DocumentState document_state_;
    cyui::ImageState image_state_;
    bool show_ = false;
    cymcp::Executor worker_{1, 16}; // joins before captured data and textures are destroyed
    static const cymcp::ChatAttachment* locate(const cymcp::ChatViewState& state, std::uint64_t id,
                                               const cymcp::ChatMessage** owner = nullptr) {
        auto row = std::upper_bound(state.messages.begin(), state.messages.end(), id,
                                    [](auto key, const auto& message) { return key < message.id; });
        if (row == state.messages.begin())
            return nullptr;
        --row;
        for (const auto& item : row->attachments)
            if (item.id == id) {
                if (owner)
                    *owner = &*row;
                return &item;
            }
        return nullptr;
    }
    static Pixels decode(std::vector<unsigned char> bytes) {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        struct Apartment {
            HRESULT status;
            ~Apartment() {
                if (SUCCEEDED(status))
                    CoUninitialize();
            }
        } apartment{initialized};
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()))) ||
            FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                    WICDecodeMetadataCacheOnDemand, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)))
            return {};
        Pixels result;
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
            std::uint64_t(width) * height > 16 * 1024 * 1024)
            return {};
        const auto ratio = std::min(1., 1024. / std::max(width, height));
        result.width = std::max(1U, static_cast<UINT>(width * ratio));
        result.height = std::max(1U, static_cast<UINT>(height * ratio));
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame.Get(), result.width, result.height,
                                      WICBitmapInterpolationModeFant)) ||
            FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA,
                                         WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)))
            return {};
        result.bytes.resize(std::size_t(result.width) * result.height * 4);
        if (FAILED(converter->CopyPixels(nullptr, result.width * 4,
                                         static_cast<UINT>(result.bytes.size()),
                                         result.bytes.data())))
            return {};
        return result;
    }

  public:
    Attachments(HWND window, ID3D11Device* device) : window_(window), device_(device) {}
    bool ready(const std::string& source) const {
        auto found = images_.find(source);
        return found != images_.end() && found->second.texture;
    }
    static const cymcp::ChatAttachment* find(const cymcp::ChatViewState& state,
                                             const std::string& source) {
        constexpr std::string_view prefix = "cymcp-attachment:";
        if (source.rfind(prefix, 0) != 0)
            return nullptr;
        std::uint64_t id = 0;
        auto parsed =
            std::from_chars(source.data() + prefix.size(), source.data() + source.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size())
            return nullptr;
        return locate(state, id);
    }
    cyui::ImageData image(const cymcp::ChatAttachment& attachment) {
        if (!attachment.content || attachment.mime_type.rfind("image/", 0) != 0)
            return {};
        return cached_image(attachment.source,
                            [attachment] { return decode(attachment_bytes(attachment)); });
    }
    cyui::ImageData preview(const std::filesystem::path& path) {
        auto suffix = path.extension().wstring();
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](wchar_t c) {
            return c >= L'A' && c <= L'Z' ? wchar_t(c + L'a' - L'A') : c;
        });
        if (suffix != L".png" && suffix != L".jpg" && suffix != L".jpeg" && suffix != L".webp" &&
            suffix != L".gif")
            return {};
        return cached_image(cymcp::path_utf8(path), [path] {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (size <= 0 || size > 32 * 1024 * 1024)
                return Pixels{};
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
                return Pixels{};
            return decode(std::move(bytes));
        });
    }

  private:
    cyui::ImageData cached_image(const std::string& source, std::function<Pixels()> load) {
        const auto frame = static_cast<std::uint64_t>(ImGui::GetFrameCount());
        auto found = images_.find(source);
        if (found == images_.end()) {
            if (images_.size() >= 8) {
                auto oldest = std::min_element(
                    images_.begin(), images_.end(),
                    [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                if (oldest->second.used == frame)
                    return {};
                if (oldest->second.texture)
                    retired_.push_back(oldest->second.texture);
                images_.erase(oldest);
            }
            auto task = std::make_shared<std::packaged_task<Pixels()>>(std::move(load));
            try {
                worker_.post([task] { (*task)(); });
            } catch (const cymcp::Error&) {
                return {}; // Defer optional thumbnails while the decoder is busy.
            }
            auto& entry = images_[source];
            entry.loading = task->get_future();
            found = images_.find(source);
        }
        auto& entry = found->second;
        entry.used = frame;
        if (entry.loading.valid() &&
            entry.loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto pixels = entry.loading.get();
                if (!pixels.bytes.empty()) {
                    D3D11_TEXTURE2D_DESC desc{};
                    desc.Width = pixels.width;
                    desc.Height = pixels.height;
                    desc.MipLevels = desc.ArraySize = 1;
                    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    desc.SampleDesc.Count = 1;
                    desc.Usage = D3D11_USAGE_IMMUTABLE;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA data{pixels.bytes.data(), pixels.width * 4, 0};
                    ComPtr<ID3D11Texture2D> texture;
                    if (SUCCEEDED(device_->CreateTexture2D(&desc, &data, &texture)) &&
                        SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), nullptr,
                                                                    &entry.texture))) {
                        entry.width = pixels.width;
                        entry.height = pixels.height;
                    }
                }
            } catch (...) { /* A preview failure leaves the attachment card and Save usable. */
            }
        }
        return {reinterpret_cast<ImTextureID>(entry.texture.Get()), int(entry.width),
                int(entry.height)};
    }

  public:
    void save_to(const cymcp::ChatAttachment& attachment, std::filesystem::path destination) {
        if (saving_.valid())
            return;
        auto task = std::make_shared<std::packaged_task<void()>>(
            [attachment, destination = std::move(destination)] {
                auto bytes = attachment_bytes(attachment);
                std::ofstream file(destination, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
                file.close();
                if (!file)
                    throw cymcp::Error("FileError", "Could not save attachment");
            });
        auto result = task->get_future();
        worker_.post([task] { (*task)(); });
        saving_ = std::move(result);
    }
    void open(const cymcp::ChatAttachment& attachment, bool save) {
        if (!attachment.content)
            throw cymcp::Error("AttachmentUnavailable", "Attachment is still loading");
        if (save) {
            if (saving_.valid())
                return;
            wchar_t path[32768]{};
            // Embedded names are labels, never destination paths. The user chooses the file.
            const auto name = std::filesystem::u8path(attachment.name).filename().wstring();
            std::copy_n(name.data(), std::min(name.size(), std::size(path) - 1), path);
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = window_;
            dialog.lpstrFile = path;
            dialog.nMaxFile = static_cast<DWORD>(std::size(path));
            dialog.lpstrFilter =
                cyui::locale() == "fr" ? L"Tous les fichiers\0*.*\0\0" : L"All files\0*.*\0\0";
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
            if (!GetSaveFileNameW(&dialog))
                return;
            save_to(attachment, path);
        } else {
            selected_ = attachment;
            document_state_ = {};
            image_state_ = {};
            document_.setMarkdown(
                attachment.content && attachment.mime_type.rfind("text/", 0) == 0
                    ? attachment.content->at("resource").value("text", std::string())
                    : "");
            show_ = true;
        }
    }
    void draw(const cymcp::ChatViewState& state,
              const std::function<void(const std::string&)>& open_link) {
        retired_.clear();
        if (saving_.valid() &&
            saving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                saving_.get();
                cyui::notify(cyui::tr("Saved").c_str(), cyui::Tone::success);
            } catch (...) {
                cyui::notify(cyui::tr("Could not save attachment").c_str(), cyui::Tone::error);
            }
        }
        if (show_) {
            ImGui::OpenPopup("##attachment-preview");
            show_ = false;
        }
        if (!ImGui::BeginPopupModal("##attachment-preview", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (selected_) {
            cyui::heading(selected_->name.c_str(), 2);
            cyui::textDim("%s · %.1f KB", selected_->mime_type.c_str(),
                          double(selected_->bytes) / 1024);
            if (selected_->mime_type.rfind("image/", 0) == 0)
                cyui::imageViewport("##image-preview", image_state_, image(*selected_),
                                    cyui::dp(640, 360));
            else if (!document_.markdown().empty()) {
                cyui::DocumentServices services;
                const cymcp::ChatMessage* row = nullptr;
                locate(state, selected_->id, &row);
                const auto sibling = [row](const std::string& src) {
                    return row ? cymcp::find_chat_attachment(*row, src) : nullptr;
                };
                services.image = [&](const std::string& src) {
                    const auto* match = sibling(src);
                    return match ? image(*match) : cyui::ImageData{};
                };
                cyui::DocumentOptions options;
                options.services = &services;
                const auto result = cyui::documentView("##file-preview", document_state_, document_,
                                                       cyui::dp(640, 360), options);
                if (!result.link.empty()) {
                    if (const auto* related = sibling(result.link))
                        open(*related, false);
                    else if (open_link)
                        open_link(result.link);
                }
            } else
                ImGui::TextUnformatted(
                    cyui::tr("No preview for this file type. Save the attachment to use it.")
                        .c_str());
            ImGui::BeginDisabled(saving_.valid());
            if (cyui::button(cyui::trLabel("Save attachment").c_str())) {
                try {
                    open(*selected_, true);
                } catch (const std::exception&) {
                    cyui::notify(cyui::tr("Could not save attachment").c_str(), cyui::Tone::error);
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (cyui::button(cyui::trLabel("Close").c_str()))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
};
} // namespace chat_example
