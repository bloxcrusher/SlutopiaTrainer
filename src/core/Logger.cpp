#include "Logger.hpp"
#include <chrono>
#include <fstream>
namespace trainer {
Logger::Logger(std::filesystem::path file) : file_(std::move(file)) {}
void Logger::Write(std::string message) {
    std::lock_guard lock(mutex_);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string line = std::to_string(seconds) + " | " + message;
    entries_.push_back(line);
    if (entries_.size() > 200) entries_.pop_front();
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (std::filesystem::file_size(file_, ec) > 1024 * 1024 && !ec) {
        auto old = file_; old += ".old";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(file_, old, ec);
    }
    std::ofstream out(file_, std::ios::app);
    if (out) out << line << '\n';
}
std::vector<std::string> Logger::Entries() const {
    std::lock_guard lock(mutex_); return {entries_.begin(), entries_.end()};
}
}
