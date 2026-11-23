#include "SaveManager.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#ifdef _WIN32
#include <ShlObj.h>
#endif

namespace trainer {
SaveManager::SaveManager(std::filesystem::path directory) { Discover(directory); }
std::filesystem::path SaveManager::DefaultSaveDirectory() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw))) return {};
    const std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    // Candidate location reported by the analysis branch, not a verified schema.
    return path / L"MobiStar Games" / L"Slutopia";
#else
    return {};
#endif
}
bool SaveManager::Discover(const std::filesystem::path& customDir) {
    const auto directory = customDir.empty() ? DefaultSaveDirectory() : customDir;
    saveDir_ = directory;
    info_ = {};
    discoveryError_.clear();
    try {
        if (directory.empty() || !std::filesystem::is_directory(directory)) {
            discoveryError_ = "Save candidate directory is unavailable"; return false;
        }
        std::size_t entries = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (++entries > 512) { info_ = {}; discoveryError_ = "Too many entries; narrow the save directory"; return false; }
            if (entry.is_symlink() || !entry.is_regular_file()) continue;
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".bak" || extension == ".tmp" || extension == ".log") continue;
            const auto size = entry.file_size();
            if (size < 17 || size > 16 * 1024 * 1024) continue;
            std::ifstream file(entry.path(), std::ios::binary);
            std::array<unsigned char,17> header{};
            file.read(reinterpret_cast<char*>(header.data()), header.size());
            constexpr std::array<unsigned char,17> signature{0,1,0,0,0,255,255,255,255,1,0,0,0,0,0,0,0};
            if (file.gcount() != 17 || header != signature) continue;
            if (!info_.path.empty()) {
                info_ = {}; discoveryError_ = "Multiple save candidates found; no active player was selected"; return false;
            }
            info_.path = entry.path();
            info_.fileSize = size;
            info_.candidate = true;
            auto backup = entry.path(); backup += ".bak";
            info_.hasBackup = std::filesystem::is_regular_file(backup);
        }
        if (!info_.candidate) discoveryError_ = "No NRBF-header save candidate found";
        return info_.candidate;
    } catch (const std::exception& ex) {
        info_ = {}; discoveryError_ = std::string("Save discovery failed: ") + ex.what(); return false;
    }
}
std::vector<SaveParameter> SaveManager::ParseParameters(const std::vector<std::uint8_t>&) {
    // NRBF object IDs and string proximity are not field offsets. Until a bounded,
    // version-matched schema reader is validated, no parameter is trusted or exposed.
    return {};
}
bool SaveManager::Load(std::string& error) {
    const auto directory = saveDir_;
    if (!Discover(directory)) { error = discoveryError_; return false; }
    error = "Save candidate found; schema/build and local ownership are unverified. Parameter access is disabled.";
    return false;
}
bool SaveManager::ModifyParameter(const std::string&, std::int32_t, std::string& error) {
    error = "Save edits disabled: no validated version-matched adapter or local ownership evidence";
    return false;
}
bool SaveManager::RestoreBackup(std::string& error) {
    error = "Restore disabled: backup identity and exclusive access have not been validated";
    return false;
}
} // namespace trainer
