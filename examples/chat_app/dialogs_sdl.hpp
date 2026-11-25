#pragma once
#include <SDL3/SDL.h>
#include <cymcp/core.hpp>
#include <functional>
#include <mutex>

namespace chat_example {
// Marshal SDL's asynchronous native dialogs back to the application's UI thread.
// No extension filter: Linux provider executables commonly have no extension.
class FileDialogs {
    using Paths = std::vector<std::filesystem::path>;
    struct Result {
        std::mutex mutex;
        bool ready = false;
        Paths paths;
        std::string name, error;
    };
    std::shared_ptr<Result> pending_;
    std::function<void(const Paths&)> complete_;
    static void SDLCALL received(void* data, const char* const* files, int) {
        std::unique_ptr<std::shared_ptr<Result>> owner(static_cast<std::shared_ptr<Result>*>(data));
        auto& result = **owner;
        std::lock_guard lock(result.mutex);
        if (!files)
            result.error = SDL_GetError();
        else
            for (auto file = files; *file; ++file)
                result.paths.emplace_back(std::filesystem::u8path(*file));
        result.ready = true;
    }

  public:
    void choose(SDL_Window* window, bool save, bool multiple, const std::string& name,
                std::function<void(const Paths&)> callback) {
        if (pending_)
            return;
        pending_ = std::make_shared<Result>();
        pending_->name = name;
        complete_ = std::move(callback);
        auto* data = new std::shared_ptr<Result>(pending_);
        const auto* location = pending_->name.empty() ? nullptr : pending_->name.c_str();
        if (save)
            SDL_ShowSaveFileDialog(received, data, window, nullptr, 0, location);
        else
            SDL_ShowOpenFileDialog(received, data, window, nullptr, 0, location, multiple);
    }
    void poll() {
        if (!pending_)
            return;
        Paths paths;
        std::string error;
        {
            std::lock_guard lock(pending_->mutex);
            if (!pending_->ready)
                return;
            paths = std::move(pending_->paths);
            error = pending_->error;
        }
        pending_.reset();
        auto callback = std::move(complete_);
        complete_ = {};
        if (!error.empty())
            throw cymcp::Error("FileError", "Cannot open the native file dialog");
        if (callback)
            callback(paths);
    }
};
} // namespace chat_example
