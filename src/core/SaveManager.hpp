#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace trainer {

struct SaveParameter {
    std::string id;
    std::string displayName;
    std::string category;
    std::int32_t value = 0;
    std::size_t offset = 0;
    bool isServerAuthoritative = false;
    std::string note;
};

struct SaveFileInfo {
    std::filesystem::path path;
    std::string playerId;
    std::uint64_t fileSize = 0;
    std::string lastModifiedUtc;
    bool hasBackup = false;
    bool valid = false;
    std::vector<SaveParameter> parameters;
};

class SaveManager {
public:
    SaveManager() = default;
    explicit SaveManager(std::filesystem::path saveDirectory);

    static std::filesystem::path DefaultSaveDirectory();

    bool Discover(const std::filesystem::path& customDir = {});
    bool Load(std::string& error);
    bool ModifyParameter(const std::string& id, std::int32_t newValue, std::string& error);
    bool RestoreBackup(std::string& error);

    [[nodiscard]] const SaveFileInfo& Info() const { return info_; }
    [[nodiscard]] bool HasBackup() const { return info_.hasBackup; }
    [[nodiscard]] bool IsValid() const { return info_.valid; }
    [[nodiscard]] const std::filesystem::path& SaveDirectory() const { return saveDir_; }

    // Pure parsing on an in-memory buffer (useful for unit testing without disk dependencies)
    static std::vector<SaveParameter> ParseParameters(const std::vector<std::uint8_t>& bytes);

private:
    std::filesystem::path saveDir_;
    SaveFileInfo info_;

    static std::optional<std::size_t> FindSequence(const std::vector<std::uint8_t>& buffer, std::string_view needle);
    static std::string FormatUtcTime(const std::filesystem::file_time_type& ftime);
};

} // namespace trainer
