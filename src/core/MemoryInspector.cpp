#include "MemoryInspector.hpp"
#include <limits>
namespace trainer {
std::optional<std::vector<std::byte>> MemoryInspector::Read(std::uint32_t pid, std::uintptr_t address,
    std::size_t size, std::string& error) {
    error.clear();
    if (size == 0 || size > 1024 * 1024 || address > std::numeric_limits<std::uintptr_t>::max() - size) {
        error = "Invalid or oversized memory range"; return std::nullopt;
    }
    UniqueHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process.Valid()) { error = WinError("Read-only OpenProcess failed"); return std::nullopt; }
    if (WaitForSingleObject(process.Get(), 0) != WAIT_TIMEOUT) { error = "Process is unavailable"; return std::nullopt; }
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQueryEx(process.Get(), reinterpret_cast<const void*>(address), &info, sizeof(info))) {
        error = WinError("VirtualQueryEx failed"); return std::nullopt;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const DWORD protection = info.Protect & 0xff;
    const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    if (info.State != MEM_COMMIT || !readable || (info.Protect & PAGE_GUARD) || address < base ||
        address - base > info.RegionSize || size > info.RegionSize - (address - base)) {
        error = "Range is not a committed readable region"; return std::nullopt;
    }
    std::vector<std::byte> bytes(size); SIZE_T read = 0;
    if (!ReadProcessMemory(process.Get(), reinterpret_cast<const void*>(address), bytes.data(), size, &read) || read != size) {
        error = WinError("ReadProcessMemory failed or returned a partial range"); return std::nullopt;
    }
    return bytes;
}
}
