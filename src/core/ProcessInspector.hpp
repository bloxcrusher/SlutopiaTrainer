#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trainer {

struct ProcessInfo {
    std::uint32_t pid = 0;
    std::wstring executable;
};

[[nodiscard]] std::vector<ProcessInfo> EnumerateProcesses();
[[nodiscard]] std::optional<ProcessInfo> FindProcess(std::wstring_view executable);
[[nodiscard]] std::wstring QueryImagePath(std::uint32_t pid);

} // namespace trainer
