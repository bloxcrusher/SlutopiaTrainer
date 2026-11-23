#include "SaveManager.hpp"
#include <ShlObj.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>

namespace trainer {

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace

SaveManager::SaveManager(std::filesystem::path saveDirectory) : saveDir_(std::move(saveDirectory)) {
    Discover(saveDir_);
}

std::filesystem::path SaveManager::DefaultSaveDirectory() {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(hr) || !raw) return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path / L"AppData" / L"LocalLow" / L"MobiStar Games" / L"Slutopia";
}

std::optional<std::size_t> SaveManager::FindSequence(const std::vector<std::uint8_t>& buffer, std::string_view needle) {
    if (needle.empty() || buffer.size() < needle.size()) return std::nullopt;
    const auto it = std::search(buffer.begin(), buffer.end(), needle.begin(), needle.end());
    if (it != buffer.end()) {
        return static_cast<std::size_t>(std::distance(buffer.begin(), it));
    }
    return std::nullopt;
}

std::string SaveManager::FormatUtcTime(const std::filesystem::file_time_type& ftime) {
    try {
        const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        const std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
        std::tm tm{};
        gmtime_s(&tm, &cftime);
        char buf[64]{};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%SZ", &tm);
        return std::string(buf);
    } catch (...) {
        return "Unknown";
    }
}

bool SaveManager::Discover(const std::filesystem::path& customDir) {
    saveDir_ = !customDir.empty() ? customDir : DefaultSaveDirectory();
    info_ = {};

    std::error_code ec;
    if (!std::filesystem::exists(saveDir_, ec) || !std::filesystem::is_directory(saveDir_, ec)) {
        return false;
    }

    // Look for a file whose size is > 10KB, not ending in .log or .bak, and starting with BinaryFormatter magic
    for (const auto& entry : std::filesystem::directory_iterator(saveDir_, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".log" || ext == ".bak") continue;

        const auto size = entry.file_size(ec);
        if (size < 1024) continue;

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) continue;

        char header[16]{};
        file.read(header, sizeof(header));
        if (file.gcount() >= 9) {
            // Check BinaryFormatter serialization magic: 0x00 0x01 0x00 0x00 0x00 0xFF 0xFF 0xFF 0xFF
            const unsigned char magic[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
            if (std::memcmp(header, magic, sizeof(magic)) == 0) {
                info_.path = entry.path();
                info_.playerId = entry.path().filename().string();
                info_.fileSize = size;
                info_.lastModifiedUtc = FormatUtcTime(entry.last_write_time(ec));
                const auto bakPath = entry.path().string() + ".bak";
                info_.hasBackup = std::filesystem::exists(bakPath, ec);
                return true;
            }
        }
    }

    return false;
}

std::vector<SaveParameter> SaveManager::ParseParameters(const std::vector<std::uint8_t>& buffer) {
    std::vector<SaveParameter> params;
    if (buffer.size() < 64) return params;

    struct CurrencyTarget {
        const char* key;
        const char* displayName;
        const char* note;
    };

    static constexpr CurrencyTarget targets[] = {
        {"currency_Soft[0];", "Soft Currency (Coins)", "Stored in local save wallet; merged on cloud sync"},
        {"currency_Hard[0];", "Hard Currency (Diamonds)", "Stored in local save wallet; merged on cloud sync"},
        {"currency_GirlCrystal[0];", "Girl Crystals", "Stored in local save wallet; character ascension"},
        {"currency_Star[0];", "Stars", "Stored in local save wallet; star shop unlock"},
        {"currency_StandardTickets[0];", "Standard Tickets", "Stored in local save wallet; gacha pulls"},
        {"currency_Contracts[0];", "Contracts", "Stored in local save wallet; contracts"},
        {"currency_BP[0];", "BattlePass Points", "Stored in local save wallet; BP progression"},
        {"currency_Jewel[0];", "Jewels", "Stored in local save wallet; exchange items"},
        {"currency_Smiles[0];", "Smiles", "Stored in local save wallet; dynamic order tokens"}
    };

    for (const auto& t : targets) {
        const auto keyPos = FindSequence(buffer, t.key);
        if (!keyPos) continue;

        // Search backwards up to 100 bytes for ClassId reference \x22\x00\x00\x00 or root struct
        bool found = false;
        std::size_t countOffset = 0;
        std::int32_t val = 0;

        const std::size_t start = *keyPos;
        const std::size_t minPos = start > 120 ? start - 120 : 0;

        for (std::size_t b = start - 1; b >= minPos; --b) {
            if (b + 8 <= buffer.size() &&
                buffer[b] == 0x22 && buffer[b + 1] == 0x00 && buffer[b + 2] == 0x00 && buffer[b + 3] == 0x00) {
                countOffset = b + 4;
                std::memcpy(&val, &buffer[countOffset], sizeof(val));
                found = true;
                break;
            }
        }

        // Special handling for initial definition if not referencing 0x22
        if (!found && std::string_view(t.key).find("currency_Hard") != std::string_view::npos) {
            for (std::size_t b = start - 1; b >= minPos; --b) {
                // Initial definition has record header 0x01, followed by 4-byte id, followed by 4-byte count
                // and then 0x05, 0xF9, 0xF8, 0xFF, 0xFF (Identificator struct marker)
                if (b + 9 <= buffer.size() &&
                    buffer[b + 4] == 0x05 && buffer[b + 5] == 0xF9 &&
                    buffer[b + 6] == 0xF8 && buffer[b + 7] == 0xFF && buffer[b + 8] == 0xFF) {
                    countOffset = b;
                    std::memcpy(&val, &buffer[countOffset], sizeof(val));
                    found = true;
                    break;
                }
            }
        }

        if (found && countOffset + sizeof(val) <= buffer.size()) {
            SaveParameter param;
            param.id = std::string(t.key);
            param.displayName = t.displayName;
            param.category = "RESOURCES";
            param.value = val;
            param.offset = countOffset;
            param.isServerAuthoritative = false;
            param.note = t.note;
            params.push_back(std::move(param));
        }
    }

    // Player level: PlayerLevelerMemento.rewardedLevel
    const auto levelPos = FindSequence(buffer, "PlayerLeveler");
    if (levelPos && *levelPos >= 11) {
        const std::size_t levelOffset = *levelPos - 11;
        std::int32_t levelVal = 0;
        std::memcpy(&levelVal, &buffer[levelOffset], sizeof(levelVal));
        if (levelVal >= 0 && levelVal <= 10000) {
            SaveParameter param;
            param.id = "player_level";
            param.displayName = "Player Level";
            param.category = "PROGRESSION";
            param.value = levelVal;
            param.offset = levelOffset;
            param.isServerAuthoritative = false;
            param.note = "Local rewarded level counter; server claim verifies total exp";
            params.push_back(std::move(param));
        }
    }

    return params;
}

bool SaveManager::Load(std::string& error) {
    error.clear();
    if (info_.path.empty() && !Discover(saveDir_)) {
        error = "No valid Slutopia save file detected";
        info_.valid = false;
        return false;
    }

    std::ifstream file(info_.path, std::ios::binary);
    if (!file.is_open()) {
        error = "Failed to open save file for reading: " + info_.path.string();
        info_.valid = false;
        return false;
    }

    std::vector<std::uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    info_.parameters = ParseParameters(buffer);
    info_.valid = !info_.parameters.empty();

    std::error_code ec;
    info_.fileSize = std::filesystem::file_size(info_.path, ec);
    info_.lastModifiedUtc = FormatUtcTime(std::filesystem::last_write_time(info_.path, ec));
    info_.hasBackup = std::filesystem::exists(info_.path.string() + ".bak", ec);

    if (!info_.valid) {
        error = "Save file was opened, but no known parameter signatures matched";
        return false;
    }

    return true;
}

bool SaveManager::ModifyParameter(const std::string& id, std::int32_t newValue, std::string& error) {
    error.clear();
    if (!info_.valid || info_.path.empty()) {
        error = "No active save file loaded";
        return false;
    }

    // Locate parameter in current list
    auto it = std::find_if(info_.parameters.begin(), info_.parameters.end(), [&](const SaveParameter& p) {
        return p.id == id;
    });
    if (it == info_.parameters.end()) {
        error = "Parameter ID not found: " + id;
        return false;
    }

    std::error_code ec;
    const auto bakPath = info_.path.string() + ".bak";
    if (!std::filesystem::exists(bakPath, ec)) {
        std::filesystem::copy_file(info_.path, bakPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Failed to create save backup before modification: " + ec.message();
            return false;
        }
    }

    // Read full file
    std::ifstream inFile(info_.path, std::ios::binary);
    if (!inFile.is_open()) {
        error = "Cannot open save file for modification";
        return false;
    }
    std::vector<std::uint8_t> buffer((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    if (it->offset + sizeof(newValue) > buffer.size()) {
        error = "Parameter offset out of file range";
        return false;
    }

    std::memcpy(&buffer[it->offset], &newValue, sizeof(newValue));

    // Atomic write via temp file
    const auto tempPath = info_.path.string() + ".tmp";
    {
        std::ofstream outFile(tempPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            error = "Failed to write temp save file";
            return false;
        }
        outFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        outFile.flush();
    }

    std::filesystem::rename(tempPath, info_.path, ec);
    if (ec) {
        // Fallback: overwrite directly
        std::ofstream directFile(info_.path, std::ios::binary | std::ios::trunc);
        if (!directFile.is_open()) {
            error = "Failed to replace save file: " + ec.message();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
        directFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        directFile.close();
        std::filesystem::remove(tempPath, ec);
    }

    // Reload parameters to verify
    return Load(error);
}

bool SaveManager::RestoreBackup(std::string& error) {
    error.clear();
    if (info_.path.empty()) {
        error = "No save file path configured";
        return false;
    }

    const auto bakPath = info_.path.string() + ".bak";
    std::error_code ec;
    if (!std::filesystem::exists(bakPath, ec)) {
        error = "No backup file exists (.bak not found)";
        return false;
    }

    std::filesystem::copy_file(bakPath, info_.path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Failed to restore backup: " + ec.message();
        return false;
    }

    return Load(error);
}

} // namespace trainer
