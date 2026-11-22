#pragma once
#include "ProcessInspector.hpp"
#include "Logger.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace trainer {
struct ProcessStatus {
    std::string state = "Select the actual game executable in SETTINGS";
    ProcessInfo process;
    std::wstring image;
    std::string architecture = "Unknown", engine = "Unknown", error;
    std::vector<ModuleInfo> modules;
    bool connected = false;
};
class ProcessManager {
public:
    explicit ProcessManager(Logger& logger);
    ~ProcessManager();
    void Configure(std::wstring target, bool enabled);
    void Refresh();
    ProcessStatus Status() const;
private:
    void Run(std::stop_token stop);
    Logger& logger_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::wstring target_;
    bool enabled_ = false;
    unsigned generation_ = 0;
    ProcessStatus status_;
    std::jthread worker_;
};
}
