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
    bool isServerAuthoritative = true;
    std::string schemaVersion;
    std::string note;
};

struct SaveFileInfo {
    std::filesystem::path path;
    std::string playerId;
    std::uint64_t fileSize = 0;
    std::int64_t updatedAt = 0;
    std::size_t mementoCount = 0;
    std::size_t nrbfRecordCount = 0;
    std::string lastModifiedUtc;
    bool hasBackup = false;
    bool valid = false;
    bool candidate = false;
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

    // Structural candidate inspection, not verified game/build compatibility. Returns empty on failure.
    static std::vector<SaveParameter> ParseParameters(const std::vector<std::uint8_t>& bytes);

    // Integrity-checked patcher operating only to a NEW output file; existing destinations are never replaced.
    static bool PatchCopyFileVerified(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& destCopyPath,
        const std::string& paramId,
        std::int32_t expectedOldValue,
        std::int32_t newValue,
        std::string& error);

private:
    std::filesystem::path saveDir_;
    SaveFileInfo info_;
    std::string discoveryError_;
};

} // namespace trainer
