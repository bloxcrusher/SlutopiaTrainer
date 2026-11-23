#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace trainer::testing {
// This API publishes NEW fixture outputs only; no existing destination is replaced.
// Fault injection is for deterministic transaction tests, never exposed by the GUI.
enum class CopyFault { None, AfterPartialWrite, BeforePublish };
bool PublishNewCopy(const std::filesystem::path& destination, const std::vector<std::uint8_t>& bytes,
    std::string& error, CopyFault fault = CopyFault::None);
}
