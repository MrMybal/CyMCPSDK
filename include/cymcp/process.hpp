#pragma once
#include "core.hpp"
namespace cymcp {
struct ProcessOptions {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path cwd;
    std::map<std::string, std::string> environment;
    std::size_t max_line_bytes = 1024 * 1024, max_queue_bytes = 4 * 1024 * 1024;
};
struct ProcessLine {
    bool stderr_stream = false;
    std::string text;
};
class Process {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Process(const ProcessOptions&);
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    void write(const std::string&, const Cancellation& = {});
    void close_input();
    // Returns false only after both stdout/stderr reach EOF. Cancellation works while silent.
    bool next(ProcessLine&, const Cancellation& = {});
    int wait(const Cancellation& = {});
    bool running();
    void terminate() noexcept;
};
std::filesystem::path find_executable(const std::string& name);
} // namespace cymcp
