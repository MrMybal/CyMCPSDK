#pragma once
#include <ctime>
#include <cymcp/chat_ui.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace chat_example {
inline const char* diagnostic_help(const std::string& code) {
    if (code == "AuthenticationRequired")
        return "Check the provider account or API credentials in Providers, then connect again.";
    if (code == "UnknownModel")
        return "Choose a model from the provider catalogue, then connect.";
    if (code == "ExecutableMissing")
        return "The CLI executable is missing. Check its installation in Providers.";
    if (code == "ProcessError" || code == "ProcessClosed" || code == "ProcessCrash")
        return "The native process could not run. Check the executable and working directory in "
               "Providers.";
    if (code == "NetworkError" || code == "Timeout")
        return "The service did not respond. Check its address and whether it is running.";
    if (code == "InvalidOptions")
        return "A connection setting is invalid. Review the selected provider settings.";
    if (code == "Cancelled")
        return "The operation was stopped. Reconnect if the provider closed its session.";
    if (code == "AccessDenied")
        return "The current permission policy rejected this action.";
    if (code == "ProviderError")
        return "The provider rejected the request. Check the account, model and native provider "
               "status.";
    if (code == "Unsupported")
        return "This provider does not support the requested operation.";
    return "Check the operation and error code in Logs and review the provider settings.";
}
inline std::string log_time(std::int64_t millis) {
    const auto seconds = static_cast<std::time_t>(millis / 1000);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    std::ostringstream text;
    text << std::put_time(&local, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
         << millis % 1000;
    return text.str();
}
inline std::string log_json(const cymcp::ChatDiagnostic& row) {
    cymcp::Json entry = {{"timestamp_ms", row.timestamp_ms}, {"time", log_time(row.timestamp_ms)},
                         {"level", row.level}, {"provider", row.provider_id},
                         {"operation", row.operation}, {"code", row.code},
                         {"message", row.message}, {"elapsed_ms", row.elapsed_ms}};
    if (!row.provider_code.empty())
        entry["provider_code"] = row.provider_code;
    return entry.dump();
}
class Journal {
    struct File {
        std::filesystem::path path;
        cymcp::PrivateOutputFile stream{nullptr, &std::fclose};
        std::size_t bytes = 0;
        std::atomic_bool failed{false};
        std::atomic_bool full{false};
    };
    std::shared_ptr<File> file_;
    std::uint64_t next_id_ = 1;
    std::deque<cymcp::ChatDiagnostic> rows_;
    cymcp::Executor writer_{1, 512}; // joined before the file or memory rows are destroyed
  public:
    void open_file(std::filesystem::path path) {
        file_ = std::make_shared<File>();
        file_->path = std::move(path);
    }
    void add(std::string level, std::string provider, std::string operation, std::string code,
             std::string message, double elapsed = 0, std::string provider_code = {}) {
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        cymcp::ChatDiagnostic row{next_id_++,
                                  millis,
                                  std::move(level),
                                  std::move(provider),
                                  std::move(operation),
                                  std::move(code),
                                  std::move(message),
                                  elapsed,
                                  std::move(provider_code)};
        if (rows_.size() == 2000)
            rows_.pop_front();
        rows_.push_back(row);
        if (!file_)
            return;
        auto line = log_json(row) + "\n";
        try {
            writer_.post([file = file_, line = std::move(line)] {
                try {
                    if (file->failed || file->full)
                        return;
                    if (line.size() > 8 * 1024 * 1024 - file->bytes) {
                        file->full = true;
                        return;
                    }
                    if (!file->stream)
                        file->stream = cymcp::open_private_output_file(file->path);
                    if (std::fwrite(line.data(), 1, line.size(), file->stream.get()) !=
                            line.size() ||
                        std::fflush(file->stream.get()) != 0)
                        file->failed = true;
                    else
                        file->bytes += line.size();
                } catch (...) {
                    file->failed = true;
                }
            });
        } catch (...) {
            file_->failed = true;
        }
    }
    const auto& rows() const noexcept {
        return rows_;
    }
    void clear() {
        rows_.clear();
    }
    bool file_failed() const {
        return file_ && file_->failed;
    }
    bool file_full() const {
        return file_ && file_->full;
    }
    static std::string copy_text(const std::vector<const cymcp::ChatDiagnostic*>& rows, bool raw) {
        std::ostringstream text;
        if (!raw)
            text << "CyMCPSDK " << cymcp::version << " diagnostics\n";
        for (const auto* entry : rows) {
            const auto& row = *entry;
            if (raw) {
                text << log_json(row) << '\n';
                continue;
            }
            text << log_time(row.timestamp_ms) << ' ' << row.level << " [" << row.provider_id
                 << "] " << row.operation << " / " << row.code;
            if (!row.provider_code.empty())
                text << " (" << row.provider_code << ')';
            text << ": " << row.message;
            if (row.elapsed_ms > 0)
                text << " (" << static_cast<long long>(row.elapsed_ms) << " ms)";
            text << '\n';
        }
        return text.str();
    }
};
} // namespace chat_example
