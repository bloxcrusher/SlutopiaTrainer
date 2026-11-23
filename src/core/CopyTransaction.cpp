#include "CopyTransaction.hpp"
#include <array>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace trainer::testing {
namespace {
struct Stage {
    std::filesystem::path path;
    ~Stage() { if (!path.empty()) { std::error_code ec; std::filesystem::remove_all(path,ec); } }
};
#ifdef _WIN32
struct File {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
#else
struct File {
    int value = -1;
    ~File() { if (value != -1) close(value); }
};
#endif
}
bool PublishNewCopy(const std::filesystem::path& destination, const std::vector<std::uint8_t>& bytes,
    std::string& error, CopyFault fault) {
    error.clear();
    try {
        if (bytes.empty() || bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("Copy buffer exceeds bounds");
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(destination, ec);
        if (std::filesystem::exists(status) || std::filesystem::is_symlink(status)) throw std::runtime_error("Destination already exists; replacement is forbidden");
        if (ec && ec != std::errc::no_such_file_or_directory) throw std::runtime_error("Cannot inspect destination");
        auto parent = destination.parent_path();
        if (parent.empty()) parent = std::filesystem::current_path();
        parent = std::filesystem::canonical(parent);
        const auto target = parent / destination.filename();
        Stage stage;
        std::random_device random;
        for (int attempt=0;attempt<16;++attempt) {
            const auto candidate = parent / (".nrbf-stage-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) { stage.path = candidate; break; }
        }
        if (stage.path.empty()) throw std::runtime_error("Cannot create exclusive staging directory");
#ifndef _WIN32
        std::filesystem::permissions(stage.path,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
#endif
        const auto temp = stage.path / "output";
        File file;
#ifdef _WIN32
        file.value = CreateFileW(temp.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Exclusive staging file creation failed");
        DWORD written{};
        const auto size = fault == CopyFault::AfterPartialWrite ? bytes.size()/2 : bytes.size();
        if (!WriteFile(file.value,bytes.data(),static_cast<DWORD>(size),&written,nullptr) || written != size)
            throw std::runtime_error("Staging write failed");
        if (fault == CopyFault::AfterPartialWrite) throw std::runtime_error("Injected interrupted write");
        if (!FlushFileBuffers(file.value)) throw std::runtime_error("Staging flush failed");
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN)) throw std::runtime_error("Staging seek failed");
        std::vector<std::uint8_t> back(bytes.size()); DWORD received{};
        if (!ReadFile(file.value,back.data(),static_cast<DWORD>(back.size()),&received,nullptr) || received != back.size() || back != bytes)
            throw std::runtime_error("Staging handle readback failed");
        const auto handle = file.value; file.value = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle)) throw std::runtime_error("Staging close failed");
#else
        file.value = open(temp.c_str(),O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW,0600);
        if (file.value < 0) throw std::runtime_error("Exclusive staging file creation failed");
        const auto size = fault == CopyFault::AfterPartialWrite ? bytes.size()/2 : bytes.size();
        for (std::size_t offset=0;offset<size;) {
            const auto n = write(file.value,bytes.data()+offset,size-offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Staging write failed");
            offset += static_cast<std::size_t>(n);
        }
        if (fault == CopyFault::AfterPartialWrite) throw std::runtime_error("Injected interrupted write");
        if (fsync(file.value) != 0) throw std::runtime_error("Staging flush failed");
        std::vector<std::uint8_t> back(bytes.size());
        for (std::size_t offset=0;offset<back.size();) {
            const auto n = pread(file.value,back.data()+offset,back.size()-offset,static_cast<off_t>(offset));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Staging readback failed");
            offset += static_cast<std::size_t>(n);
        }
        if (back != bytes) throw std::runtime_error("Staging readback mismatch");
        const auto descriptor = file.value; file.value = -1;
        if (close(descriptor) != 0) throw std::runtime_error("Staging close failed");
#endif
        if (fault == CopyFault::BeforePublish) throw std::runtime_error("Injected pre-publication failure");
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("No-replace publication failed");
#else
        // link is atomic and never overwrites an existing target (including a raced-in symlink).
        if (link(temp.c_str(),target.c_str()) != 0) throw std::runtime_error("No-replace publication failed");
#endif
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
}
