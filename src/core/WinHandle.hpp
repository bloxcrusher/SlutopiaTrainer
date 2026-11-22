#pragma once
#include <Windows.h>
#include <utility>
#include <string>
#include <string_view>
namespace trainer {
class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~UniqueHandle() { Reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) { Reset(); value_ = std::exchange(other.value_, nullptr); }
        return *this;
    }
    void Reset(HANDLE value = nullptr) noexcept { if (Valid()) CloseHandle(value_); value_ = value; }
    [[nodiscard]] HANDLE Get() const noexcept { return value_; }
    [[nodiscard]] bool Valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};
inline std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}
inline std::wstring Wide(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}
inline std::string WinError(std::string_view action, DWORD code = GetLastError()) {
    return std::string(action) + " (Windows error " + std::to_string(code) + ")";
}
}
