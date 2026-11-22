#include "ProcessInspector.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>
#include <utility>

namespace trainer {
namespace {
class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] bool Valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_;
};

std::wstring Lower(std::wstring_view text) {
    std::wstring result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}
} // namespace

std::vector<ProcessInfo> EnumerateProcesses() {
    std::vector<ProcessInfo> processes;
    const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.Valid())
        return processes;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.Get(), &entry))
        return processes;

    do {
        processes.push_back({entry.th32ProcessID, entry.szExeFile});
    } while (Process32NextW(snapshot.Get(), &entry));

    return processes;
}

std::optional<ProcessInfo> FindProcess(std::wstring_view executable) {
    if (executable.empty())
        return std::nullopt;
    const auto needle = Lower(executable);
    for (const auto& process : EnumerateProcesses()) {
        if (Lower(process.executable) == needle)
            return process;
    }
    return std::nullopt;
}

std::wstring QueryImagePath(std::uint32_t pid) {
    const UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.Valid())
        return {};

    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.Get(), 0, path.data(), &length))
        return {};

    path.resize(length);
    return path;
}

} // namespace trainer
