#include "core/ConfigManager.hpp"
#include "core/FeatureManager.hpp"
#include "core/Logger.hpp"
#include "core/NrbfReader.hpp"
#include "core/SaveManager.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace trainer;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

#include "NrbfFixture.hpp"

int main() {
    const auto directory = std::filesystem::temp_directory_path() / ("trainer-config-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        ConfigManager config(directory / "settings.json");
        std::string error;
        Require(config.Load(error).executable.empty() && error.empty(), "Missing config defaults");

        Settings settings{"Game.exe", true, false, true};
        Require(config.Save(settings, error), "Initial save");
        settings.executable = "Other.exe";
        Require(config.Save(settings, error), "Atomic overwrite");

        const auto loaded = config.Load(error);
        Require(error.empty() && loaded.executable == "Other.exe" && loaded.autoConnect && !loaded.hotkeys && loaded.reducedMotion, "Round trip");

        settings.executable = "../Bad.exe";
        Require(!config.Save(settings, error) && config.Load(error).executable == "Other.exe", "Reject path without clobbering settings");

        { std::ofstream file(config.Path()); file << "{bad json"; }
        Require(config.Load(error).executable.empty() && !error.empty(), "Malformed JSON defaults");

        { std::ofstream file(config.Path()); file << R"({"version":999})"; }
        Require(!config.Load(error).autoConnect && !error.empty(), "Unsupported schema");

        { std::ofstream file(config.Path()); file << std::string(65537, 'x'); }
        Require(!config.Load(error).autoConnect && !error.empty(), "Oversized config");

        for (const auto& feature : FeatureManager::Unsupported()) {
            Require(!feature.name.empty() && !feature.category.empty() && !feature.reason.empty(),
                    "Unavailable features must explain their reason in the UI");
        }

        Logger logger(directory / "trainer.log");
        for (int i = 0; i < 250; ++i) logger.Write("Diagnostic event");
        Require(logger.Entries().size() == 200, "Bounded diagnostic history");

        // No adapter may turn a byte/string match into a confirmed game value.
        std::vector<std::uint8_t> mockBuffer(256, 0);
        mockBuffer[30] = 0x22;
        const std::string key = "currency_Soft[0];";
        std::copy(key.begin(), key.end(), mockBuffer.begin() + 50);
        Require(SaveManager::ParseParameters(mockBuffer).empty(), "Reject heuristic currency signatures");
        std::fill(mockBuffer.begin(), mockBuffer.end(), 0);
        std::copy(key.begin(), key.end(), mockBuffer.begin());
        Require(SaveManager::ParseParameters(mockBuffer).empty(), "Key at offset zero must terminate safely");
        std::copy(key.begin(), key.end(), mockBuffer.begin() + 8);
        Require(SaveManager::ParseParameters(mockBuffer).empty(), "Missing marker before key must terminate safely");

        const auto saves = directory / std::filesystem::path(u8"сохранения");
        std::filesystem::create_directories(saves);
        SaveManager manager(saves);
        Require(!manager.Load(error) && !error.empty(), "Missing save is an explicit failure");
        const auto save = saves / "player";
        const std::string header("\0\1\0\0\0\xff\xff\xff\xff\1\0\0\0\0\0\0\0", 17);
        const std::string original = header + std::string(256, 'x');
        auto put = [](const auto& path, const std::string& data) { std::ofstream file(path, std::ios::binary); file << data; };
        auto read = [](const auto& path) { std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); };
        put(save, original); put(saves / "player.bak", "private backup");
        Require(!manager.Load(error) && manager.Info().candidate && !manager.IsValid(), "Header is a candidate, not a validated save");
        Require(!manager.ModifyParameter("currency_Soft[0];", 1000, error) && !error.empty(), "Reject unverified writes");
        Require(!manager.RestoreBackup(error) && !error.empty(), "Reject unverified restoration");
        Require(read(save) == original && read(saves / "player.bak") == "private backup", "Save and backup bytes remain unchanged");
        Require(!std::filesystem::exists(saves / "player.tmp"), "Rejected writes create no temporary save");
        put(saves / "second", original);
        Require(!manager.Load(error) && !manager.Info().candidate && !error.empty(), "Ambiguous players must not select an arbitrary save");
        std::filesystem::remove(saves / "second");
        put(save, "truncated");
        Require(!manager.Load(error) && manager.Info().path.empty() && manager.Info().parameters.empty(), "Corrupt replacement clears old state");
        put(save, header);
        std::filesystem::resize_file(save, 16 * 1024 * 1024 + 1);
        Require(!manager.Load(error) && !manager.Info().candidate, "Oversized saves are not read into memory");
        std::filesystem::remove(save);
        put(saves / "other.BAK", original); put(saves / "other.tmp", original);
        Require(!manager.Load(error), "Backups and temporary files are not active saves");
        SaveManager missing(directory / "absent");
        Require(!missing.Load(error) && !error.empty(), "Nonexistent directories report errors");

        // Strict MS-NRBF parser positive & negative regression tests
        const auto validNrbf = BuildValidSavedDataNrbf(72, 88, 2);
        const auto parsedNrbf = nrbf::Parse(validNrbf);
        Require(parsedNrbf.ok && parsedNrbf.document.bytesConsumed == validNrbf.size(), "Valid synthetic NRBF must parse completely");

        const auto params = SaveManager::ParseParameters(validNrbf);
        Require(params.size() == 3, "Expected 3 parsed parameters (Soft, Hard, Level)");
        Require(params[0].id == "currency_Soft[0];" && params[0].value == 72 && params[0].isServerAuthoritative, "Soft currency match");
        Require(params[1].id == "currency_Hard[0];" && params[1].value == 88 && params[1].isServerAuthoritative, "Hard currency match");
        Require(params[2].id == "PlayerLeveler.rewardedLevel" && params[2].value == 2 && params[2].isServerAuthoritative, "Level match");

        // Trailing garbage after MessageEnd must fail
        auto trailing = validNrbf;
        trailing.push_back(0x00);
        Require(!nrbf::Parse(trailing).ok && SaveManager::ParseParameters(trailing).empty(), "Reject trailing bytes after MessageEnd");

        // Truncated stream (missing MessageEnd) must fail
        auto truncated = validNrbf;
        truncated.pop_back();
        Require(!nrbf::Parse(truncated).ok && SaveManager::ParseParameters(truncated).empty(), "Reject truncated NRBF stream");

        // Verified copy-only patching test
        const auto nrbfDir = directory / "nrbf_test";
        std::filesystem::create_directories(nrbfDir);
        const auto srcFile = nrbfDir / "fixture_save";
        const auto dstFile = nrbfDir / "fixture_save_copy";
        {
            std::ofstream out(srcFile, std::ios::binary);
            out.write(reinterpret_cast<const char*>(validNrbf.data()), static_cast<std::streamsize>(validNrbf.size()));
        }

        // SaveManager::Load on valid NRBF fixture should succeed (read-only)
        SaveManager nrbfManager(nrbfDir);
        Require(nrbfManager.Load(error) && nrbfManager.IsValid() && nrbfManager.Info().parameters.size() == 3, "Load valid NRBF fixture");
        Require(!nrbfManager.ModifyParameter("currency_Soft[0];", 500, error), "Direct ModifyParameter stays fail-closed");

        // PatchCopyFileVerified safety checks
        Require(!SaveManager::PatchCopyFileVerified(srcFile, srcFile, "currency_Soft[0];", 72, 500, error), "Reject in-place patch");
        Require(!SaveManager::PatchCopyFileVerified(srcFile, dstFile, "currency_Soft[0];", 999, 500, error), "Reject stale expectedOldValue");
        Require(!SaveManager::PatchCopyFileVerified(srcFile, dstFile, "currency_Soft[0];", 72, -1, error), "Reject negative value");
        Require(SaveManager::PatchCopyFileVerified(srcFile, dstFile, "currency_Soft[0];", 72, 500, error), "Verified copy patch succeeds");
        Require(!std::filesystem::exists(nrbfDir / "fixture_save_copy.tmp"), "No leftover .tmp file after verified patch");

        // Verify source file is completely untouched and destination copy parses cleanly with new value
        const auto srcAfter = read(srcFile);
        Require(srcAfter.size() == validNrbf.size() && std::memcmp(srcAfter.data(), validNrbf.data(), validNrbf.size()) == 0,
                "Source fixture must remain byte-for-byte unchanged");
        std::vector<std::uint8_t> dstBytes;
        {
            std::ifstream in(dstFile, std::ios::binary);
            dstBytes.assign(std::istreambuf_iterator<char>(in), {});
        }
        const auto dstParams = SaveManager::ParseParameters(dstBytes);
        Require(dstParams.size() == 3 && dstParams[0].value == 500 && dstParams[1].value == 88 && dstParams[2].value == 2,
                "Patched copy must update only the target field and remain valid NRBF");

        std::filesystem::remove_all(directory);
        std::cout << "Configuration validation, persistence, NRBF save parsing and feature gates: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
