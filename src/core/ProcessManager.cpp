#include "ProcessManager.hpp"
#include "GameDetector.hpp"
#include <chrono>
namespace trainer {
ProcessManager::ProcessManager(Logger& logger) : logger_(logger), worker_([this](std::stop_token stop) { Run(stop); }) {}
ProcessManager::~ProcessManager() { worker_.request_stop(); wake_.notify_all(); worker_.join(); }
void ProcessManager::Configure(std::wstring target, bool enabled) {
    { std::lock_guard lock(mutex_); target_ = std::move(target); enabled_ = enabled; ++generation_; status_ = {}; }
    wake_.notify_all();
}
void ProcessManager::Refresh() {
    { std::lock_guard lock(mutex_); ++generation_; }
    wake_.notify_all();
}
ProcessStatus ProcessManager::Status() const { std::lock_guard lock(mutex_); return status_; }
void ProcessManager::Run(std::stop_token stop) {
    UniqueHandle process;
    unsigned observed = ~0u;
    ProcessStatus next;
    std::string lastError;
    while (!stop.stop_requested()) {
        std::wstring target; bool enabled; unsigned generation;
        { std::lock_guard lock(mutex_); target = target_; enabled = enabled_; generation = generation_; }
        if (generation != observed) { process.Reset(); next = {}; observed = generation; }
        next.error.clear();
        bool exited = false;
        if (process.Valid()) {
            const auto wait = WaitForSingleObject(process.Get(), 0);
            if (wait != WAIT_TIMEOUT) {
                const auto waitError = wait == WAIT_FAILED ? WinError("Process availability check failed") : std::string{};
                process.Reset(); next = {}; next.error = waitError; next.state = "Process exited; waiting to reconnect"; exited = true;
                logger_.Write("Process disconnected; cached modules cleared");
            }
        }
        if (!enabled || target.empty()) { process.Reset(); next = {}; next.state = target.empty() ? "Executable not configured" : "Disconnected"; }
        else if (!process.Valid() && !exited) {
            next = {}; next.state = "Waiting for configured executable";
            const auto match = FindProcess(target, &next.error);
            if (match) {
                next.process = *match;
                process.Reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, match->pid));
                if (!process.Valid()) { next.state = "Process found; access unavailable"; next.error = WinError("OpenProcess failed"); }
                else {
                    next.image = QueryImagePath(process.Get(), &next.error);
                    next.architecture = QueryArchitecture(process.Get(), &next.error);
                    next.modules = EnumerateModules(match->pid, &next.error);
                    // Holding the original handle and rechecking exit avoids publishing a reused PID's modules.
                    if (WaitForSingleObject(process.Get(), 0) == WAIT_TIMEOUT) {
                        next.engine = GameDetector::EngineEvidence(next.modules); next.connected = true;
                        next.state = "Connected for diagnostics (game identity unverified)";
                        logger_.Write("Connected to PID " + std::to_string(match->pid));
                    } else { process.Reset(); next = {}; next.state = "Process exited during inspection"; }
                }
            }
        }
        if (!next.error.empty() && next.error != lastError) logger_.Write(next.error);
        lastError = next.error;
        std::unique_lock lock(mutex_);
        if (generation == generation_) status_ = next;
        wake_.wait_for(lock, stop, std::chrono::milliseconds(750), [&] { return generation != generation_; });
    }
}
}
