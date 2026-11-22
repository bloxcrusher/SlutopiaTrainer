#pragma once
#include "WinHandle.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace trainer {
struct ProcessInfo { std::uint32_t pid = 0; std::wstring executable; };
struct ModuleInfo { std::wstring name, path; std::uintptr_t base = 0; std::uint32_t size = 0; };
[[nodiscard]] std::vector<ProcessInfo> EnumerateProcesses(std::string* error = nullptr);
[[nodiscard]] std::optional<ProcessInfo> FindProcess(std::wstring_view executable, std::string* error = nullptr);
[[nodiscard]] std::wstring QueryImagePath(HANDLE process, std::string* error = nullptr);
[[nodiscard]] std::wstring QueryImagePath(std::uint32_t pid);
[[nodiscard]] std::string QueryArchitecture(HANDLE process, std::string* error = nullptr);
[[nodiscard]] std::vector<ModuleInfo> EnumerateModules(std::uint32_t pid, std::string* error = nullptr);
}
