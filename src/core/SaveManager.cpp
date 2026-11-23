#include "SaveManager.hpp"
#include "NrbfReader.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#ifdef _WIN32
#include <ShlObj.h>
#include <windows.h>
#endif

namespace trainer {
namespace {

constexpr std::uint64_t kMaxSaveFileBytes = 16 * 1024 * 1024;

struct CurrencyDescriptor {
    const char* uniqueKey;
    const char* displayName;
    bool serverMerged;
    const char* note;
};

constexpr std::array kKnownCurrencies{
    CurrencyDescriptor{"currency_Soft[0];", "Soft Currency (Coins)", true, "Overwritten by LocalServerSaveMerger (soft_money)"},
    CurrencyDescriptor{"currency_Hard[0];", "Hard Currency (Diamonds)", true, "Overwritten by LocalServerSaveMerger (hard_money)"},
    CurrencyDescriptor{"currency_GirlCrystal[0];", "Girl Crystals", true, "Overwritten by LocalServerSaveMerger (girl_crystal)"},
    CurrencyDescriptor{"currency_Star[0];", "Stars", true, "Overwritten by LocalServerSaveMerger (stars)"},
    CurrencyDescriptor{"currency_StandardTickets[0];", "Standard Tickets", true, "Overwritten by LocalServerSaveMerger (standard_tickets)"},
    CurrencyDescriptor{"currency_EventTickets[0];", "Event Tickets", true, "Overwritten by LocalServerSaveMerger (event_tickets)"},
    CurrencyDescriptor{"currency_Contracts[0];", "Contracts", true, "Overwritten by LocalServerSaveMerger (contracts)"},
    CurrencyDescriptor{"currency_BP[0];", "BattlePass Points", true, "Overwritten by LocalServerSaveMerger (bp_points)"},
    CurrencyDescriptor{"currency_Jewel[0];", "Jewels", true, "Overwritten by LocalServerSaveMerger (jewels)"},
    CurrencyDescriptor{"currency_Smiles[0];", "Smiles", true, "Overwritten by LocalServerSaveMerger (smiles)"},
    CurrencyDescriptor{"currency_AuctionTicket[0];", "Auction Tickets", true, "Overwritten by LocalServerSaveMerger (auction_ticket)"},
    CurrencyDescriptor{"currency_Present1[0];", "Present 1", true, "Overwritten by LocalServerSaveMerger (present_1)"},
    CurrencyDescriptor{"currency_Present2[0];", "Present 2", true, "Overwritten by LocalServerSaveMerger (present_2)"},
    CurrencyDescriptor{"currency_Present3[0];", "Present 3", true, "Overwritten by LocalServerSaveMerger (present_3)"},
    CurrencyDescriptor{"currency_Present4[0];", "Present 4", true, "Overwritten by LocalServerSaveMerger (present_4)"},
    CurrencyDescriptor{"currency_StoryEventResource[0];", "Story Event Resource", true, "Overwritten by LocalServerSaveMerger (storyevent_resource)"},
    CurrencyDescriptor{"currency_StoryEventUpgrade[0];", "Story Event Upgrade", true, "Overwritten by LocalServerSaveMerger (storyevent_upgrade)"}
};

struct ExtractedSave {
    bool ok = false;
    std::string error;
    std::int64_t updatedAt = 0;
    std::size_t mementoCount = 0;
    std::size_t recordCount = 0;
    std::vector<SaveParameter> parameters;
};

ExtractedSave ExtractSavedData(const std::vector<std::uint8_t>& bytes) {
    ExtractedSave out;
    const auto parsed = nrbf::Parse(bytes);
    if (!parsed.ok) {
        out.error = "NRBF parse failed: " + parsed.error;
        return out;
    }

    const auto& doc = parsed.document;
    out.recordCount = doc.recordCount;

    const auto rootIt = doc.objects.find(doc.rootId);
    if (rootIt == doc.objects.end()) {
        out.error = "NRBF root object not found";
        return out;
    }
    const auto& root = rootIt->second;
    if (root.className != "Saves.SavedData") {
        out.error = "Unexpected NRBF root class: " + root.className;
        return out;
    }

    if (const auto* upd = root.FindField("UpdatedAt")) {
        if (const auto* v = std::get_if<std::int64_t>(&upd->value)) out.updatedAt = *v;
    }

    const auto* dataField = root.FindField("Data");
    const auto* listObj = dataField ? doc.ResolveObject(*dataField) : nullptr;
    if (!listObj) {
        out.error = "Saves.SavedData.Data list is missing or null";
        return out;
    }

    const auto* sizeField = listObj->FindField("_size");
    const auto* itemsField = listObj->FindField("_items");
    const auto* itemsArr = itemsField ? doc.ResolveArray(*itemsField) : nullptr;
    if (!sizeField || !itemsArr) {
        out.error = "Saves.SavedData.Data is missing _size or _items";
        return out;
    }

    const auto* rawSize = std::get_if<std::int64_t>(&sizeField->value);
    if (!rawSize || *rawSize < 0 || static_cast<std::size_t>(*rawSize) > itemsArr->elements.size()) {
        out.error = "Saves.SavedData.Data _size is out of array bounds";
        return out;
    }

    const auto count = static_cast<std::size_t>(*rawSize);
    out.mementoCount = count;

    for (std::size_t i = 0; i < count; ++i) {
        const auto* ref = std::get_if<nrbf::ObjectRef>(&itemsArr->elements[i]);
        if (!ref) continue;
        const auto objIt = doc.objects.find(ref->id);
        if (objIt == doc.objects.end()) continue;
        const auto& obj = objIt->second;

        if (obj.className == "StripClub.Model.Data.SimpleCurrency+CurrencyMemento") {
            const auto* cntField = obj.FindField("Count");
            const auto* keyField = obj.FindField("Memento+<UniqueKey>k__BackingField");
            const auto* verField = obj.FindField("Memento+<Version>k__BackingField");
            if (!cntField || !keyField || cntField->byteSize != sizeof(std::int32_t)) continue;

            const auto* cntVal = std::get_if<std::int64_t>(&cntField->value);
            const auto keyStr = doc.ResolveString(*keyField);
            if (!cntVal || !keyStr) continue;
            if (*cntVal < std::numeric_limits<std::int32_t>::min() ||
                *cntVal > std::numeric_limits<std::int32_t>::max()) {
                continue;
            }

            SaveParameter p;
            p.id = *keyStr;
            p.displayName = *keyStr;
            p.category = "RESOURCES";
            p.value = static_cast<std::int32_t>(*cntVal);
            p.offset = cntField->byteOffset;
            p.isServerAuthoritative = true;
            p.schemaVersion = verField ? doc.ResolveString(*verField).value_or("") : "";
            p.note = "Wallet currency (server-merged via LocalServerSaveMerger)";

            for (const auto& desc : kKnownCurrencies) {
                if (*keyStr == desc.uniqueKey) {
                    p.displayName = desc.displayName;
                    p.isServerAuthoritative = desc.serverMerged;
                    p.note = desc.note;
                    break;
                }
            }
            out.parameters.push_back(std::move(p));
        } else if (obj.className == "_Scripts.PlayerLeveling.PlayerLevelerMemento") {
            const auto* lvlField = obj.FindField("rewardedLevel");
            const auto* keyField = obj.FindField("Memento+<UniqueKey>k__BackingField");
            const auto* verField = obj.FindField("Memento+<Version>k__BackingField");
            if (!lvlField || !keyField || lvlField->byteSize != sizeof(std::int32_t)) continue;

            const auto* lvlVal = std::get_if<std::int64_t>(&lvlField->value);
            const auto keyStr = doc.ResolveString(*keyField);
            if (!lvlVal || !keyStr || *keyStr != "PlayerLeveler") continue;
            if (*lvlVal < std::numeric_limits<std::int32_t>::min() ||
                *lvlVal > std::numeric_limits<std::int32_t>::max()) {
                continue;
            }

            SaveParameter p;
            p.id = "PlayerLeveler.rewardedLevel";
            p.displayName = "Player Rewarded Level";
            p.category = "PROGRESSION";
            p.value = static_cast<std::int32_t>(*lvlVal);
            p.offset = lvlField->byteOffset;
            p.isServerAuthoritative = true;
            p.schemaVersion = verField ? doc.ResolveString(*verField).value_or("") : "";
            p.note = "Overwritten by LocalServerSaveMerger.MergePlayerLevel";
            out.parameters.push_back(std::move(p));
        }
    }

    out.ok = true;
    return out;
}

bool ReadFileBounded(const std::filesystem::path& path, std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        error = "Save file is missing or not a regular file";
        return false;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 17 || size > kMaxSaveFileBytes) {
        error = "Save file size is out of allowed bounds (17 B .. 16 MiB)";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Failed to open save file for reading";
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file || static_cast<std::size_t>(file.gcount()) != out.size()) {
        out.clear();
        error = "Failed to read complete save file";
        return false;
    }
    return true;
}

} // namespace

SaveManager::SaveManager(std::filesystem::path directory) { Discover(directory); }

std::filesystem::path SaveManager::DefaultSaveDirectory() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &raw))) return {};
    const std::filesystem::path path(raw);
    CoTaskMemFree(raw);
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
            discoveryError_ = "Save candidate directory is unavailable";
            return false;
        }
        std::size_t entries = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (++entries > 512) {
                info_ = {};
                discoveryError_ = "Too many entries; narrow the save directory";
                return false;
            }
            if (entry.is_symlink() || !entry.is_regular_file()) continue;
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".bak" || extension == ".tmp" || extension == ".log") continue;
            const auto size = entry.file_size();
            if (size < 17 || size > kMaxSaveFileBytes) continue;
            std::ifstream file(entry.path(), std::ios::binary);
            std::array<unsigned char, 17> header{};
            file.read(reinterpret_cast<char*>(header.data()), header.size());
            constexpr std::array<unsigned char, 17> signature{0, 1, 0, 0, 0, 255, 255, 255, 255, 1, 0, 0, 0, 0, 0, 0, 0};
            if (file.gcount() != 17 || header != signature) continue;
            if (!info_.path.empty()) {
                info_ = {};
                discoveryError_ = "Multiple save candidates found; no active player was selected";
                return false;
            }
            info_.path = entry.path();
            info_.fileSize = size;
            info_.candidate = true;
            auto backup = entry.path();
            backup += ".bak";
            info_.hasBackup = std::filesystem::is_regular_file(backup);
        }
        if (!info_.candidate) discoveryError_ = "No NRBF-header save candidate found";
        return info_.candidate;
    } catch (const std::exception& ex) {
        info_ = {};
        discoveryError_ = std::string("Save discovery failed: ") + ex.what();
        return false;
    }
}

std::vector<SaveParameter> SaveManager::ParseParameters(const std::vector<std::uint8_t>& bytes) {
    auto extracted = ExtractSavedData(bytes);
    if (!extracted.ok) return {};
    return std::move(extracted.parameters);
}

bool SaveManager::Load(std::string& error) {
    error.clear();
    const auto directory = saveDir_;
    if (!Discover(directory)) {
        error = discoveryError_;
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadFileBounded(info_.path, bytes, error)) {
        info_.valid = false;
        info_.parameters.clear();
        return false;
    }

    auto extracted = ExtractSavedData(bytes);
    if (!extracted.ok) {
        info_.valid = false;
        info_.parameters.clear();
        error = "Save candidate found, but NRBF schema validation failed: " + extracted.error;
        return false;
    }

    info_.updatedAt = extracted.updatedAt;
    info_.mementoCount = extracted.mementoCount;
    info_.nrbfRecordCount = extracted.recordCount;
    info_.parameters = std::move(extracted.parameters);
    info_.valid = true;
    return true;
}

bool SaveManager::ModifyParameter(const std::string&, std::int32_t, std::string& error) {
    error = "Direct save edits disabled: parsed wallet/level fields are server-merged; use PatchCopyFileVerified on test copies only";
    return false;
}

bool SaveManager::RestoreBackup(std::string& error) {
    error = "Restore disabled: backup identity and exclusive access have not been validated";
    return false;
}

bool SaveManager::PatchCopyFileVerified(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destCopyPath,
    const std::string& paramId,
    std::int32_t expectedOldValue,
    std::int32_t newValue,
    std::string& error) {
    error.clear();
    if (sourcePath.empty() || destCopyPath.empty()) {
        error = "Source and destination paths must be non-empty";
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(destCopyPath, ec) && std::filesystem::equivalent(sourcePath, destCopyPath, ec)) {
        error = "Refusing in-place modification: destination must be a separate test copy";
        return false;
    }
    if (sourcePath.lexically_normal() == destCopyPath.lexically_normal()) {
        error = "Refusing in-place modification: source and destination paths are identical";
        return false;
    }

    std::vector<std::uint8_t> original;
    if (!ReadFileBounded(sourcePath, original, error)) return false;

    const auto extracted = ExtractSavedData(original);
    if (!extracted.ok) {
        error = extracted.error;
        return false;
    }

    const SaveParameter* target = nullptr;
    for (const auto& p : extracted.parameters) {
        if (p.id == paramId) {
            if (target != nullptr) {
                error = "Ambiguous parameter ID in save graph: " + paramId;
                return false;
            }
            target = &p;
        }
    }
    if (!target) {
        error = "Parameter ID not found in validated NRBF graph: " + paramId;
        return false;
    }

    nrbf::PatchRequest req;
    req.byteOffset = target->offset;
    req.expectedOldValue = expectedOldValue;
    req.newValue = newValue;
    req.minAllowed = 0;
    req.maxAllowed = 1000000000;

    const auto patched = nrbf::PatchInt32Verified(original, req, error);
    if (!patched) return false;

    auto tempPath = destCopyPath;
    tempPath += ".tmp";

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "Failed to create temporary output file";
            return false;
        }
        out.write(reinterpret_cast<const char*>(patched->data()), static_cast<std::streamsize>(patched->size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            error = "Failed while writing or flushing temporary output file";
            return false;
        }
        out.close();
        if (out.fail()) {
            std::filesystem::remove(tempPath, ec);
            error = "Failed to close temporary output file cleanly";
            return false;
        }
    }

    // Re-read and re-parse the temporary file from disk before publishing.
    std::vector<std::uint8_t> readBack;
    if (!ReadFileBounded(tempPath, readBack, error) || readBack != *patched) {
        std::filesystem::remove(tempPath, ec);
        if (error.empty()) error = "Disk readback mismatch on temporary output file";
        return false;
    }
    const auto verifyExtracted = ExtractSavedData(readBack);
    if (!verifyExtracted.ok) {
        std::filesystem::remove(tempPath, ec);
        error = "Post-write disk validation failed: " + verifyExtracted.error;
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExW(tempPath.c_str(), destCopyPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD lastErr = GetLastError();
        std::filesystem::remove(tempPath, ec);
        error = "Atomic MoveFileExW failed with Win32 error " + std::to_string(lastErr);
        return false;
    }
#else
    std::filesystem::rename(tempPath, destCopyPath, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        error = "Atomic rename failed: " + ec.message();
        return false;
    }
#endif

    return true;
}

} // namespace trainer
