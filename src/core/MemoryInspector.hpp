#pragma once
#include "WinHandle.hpp"
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>
namespace trainer {
class MemoryInspector {
public:
    // Reserved for validated adapters; UI provides no arbitrary-address editor. No write API.
    static std::optional<std::vector<std::byte>> Read(std::uint32_t pid, std::uintptr_t address,
        std::size_t size, std::string& error);
};
}
