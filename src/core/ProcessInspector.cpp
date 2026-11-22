#include "ProcessInspector.hpp"
#include <TlHelp32.h>
#include <algorithm>
#include <cwctype>
namespace trainer {
std::vector<ProcessInfo> EnumerateProcesses(std::string* error) {
    if (error) error->clear();
    std::vector<ProcessInfo> result;
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.Valid()) { if (error) *error = WinError("Process snapshot failed"); return result; }
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.Get(), &entry)) {
        if (GetLastError() != ERROR_NO_MORE_FILES && error) *error = WinError("Process32First failed");
        return result;
    }
    do { result.push_back({entry.th32ProcessID, entry.szExeFile}); }
    while (Process32NextW(snapshot.Get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES && error) *error = WinError("Process32Next failed");
    return result;
}
std::optional<ProcessInfo> FindProcess(std::wstring_view executable, std::string* error) {
    for (const auto& p : EnumerateProcesses(error))
        if (CompareStringOrdinal(p.executable.c_str(), -1, std::wstring(executable).c_str(), -1, TRUE) == CSTR_EQUAL) return p;
    return std::nullopt;
}
std::wstring QueryImagePath(HANDLE process, std::string* error) {
    std::wstring path(32768, L'\0'); DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        if (error) *error = WinError("Image path unavailable"); return {};
    }
    path.resize(length); return path;
}
std::wstring QueryImagePath(std::uint32_t pid) {
    UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    return process.Valid() ? QueryImagePath(process.Get()) : std::wstring{};
}
std::string QueryArchitecture(HANDLE process, std::string* error) {
    USHORT machine{}, native{};
    if (!IsWow64Process2(process, &machine, &native)) {
        if (error) *error = WinError("IsWow64Process2 failed"); return "Unknown";
    }
    switch (machine == IMAGE_FILE_MACHINE_UNKNOWN ? native : machine) {
    case IMAGE_FILE_MACHINE_AMD64: return "x64";
    case IMAGE_FILE_MACHINE_I386: return "x86";
    case IMAGE_FILE_MACHINE_ARM64: return "ARM64";
    default: return "Unknown machine " + std::to_string(machine);
    }
}
std::vector<ModuleInfo> EnumerateModules(std::uint32_t pid, std::string* error) {
    std::vector<ModuleInfo> result;
    UniqueHandle snapshot;
    for (int retry = 0; retry < 3; ++retry) {
        snapshot.Reset(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (snapshot.Valid() || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (!snapshot.Valid()) { if (error) *error = WinError("Module snapshot failed"); return result; }
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.Get(), &entry)) { if (error) *error = WinError("Module32First failed"); return result; }
    do { result.push_back({entry.szModule, entry.szExePath, reinterpret_cast<std::uintptr_t>(entry.modBaseAddr), entry.modBaseSize}); }
    while (Module32NextW(snapshot.Get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES && error) *error = WinError("Module32Next failed");
    return result;
}
}
