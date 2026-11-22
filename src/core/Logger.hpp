#pragma once
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
namespace trainer {
class Logger {
public:
    explicit Logger(std::filesystem::path file);
    void Write(std::string message);
    [[nodiscard]] std::vector<std::string> Entries() const;
private:
    std::filesystem::path file_;
    mutable std::mutex mutex_;
    std::deque<std::string> entries_;
};
}
