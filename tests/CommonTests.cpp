#include "core/ConfigManager.hpp"
#include "core/FeatureManager.hpp"
#include "core/GameDetector.hpp"
#include "core/Logger.hpp"
#include "core/SaveManager.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace trainer;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

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

        // SaveManager test
        SaveManager saveMgr(directory / "non_existent_dir");
        Require(!saveMgr.IsValid(), "Non-existent save dir should not be valid");

        // Test synthetic buffer parsing
        std::vector<std::uint8_t> mockBuffer(256, 0);
        // Put class id marker \x22\x00\x00\x00 at offset 30
        mockBuffer[30] = 0x22;
        // Put count 777 at offset 34
        const std::int32_t count = 777;
        std::memcpy(&mockBuffer[34], &count, sizeof(count));
        // Put currency_Soft[0]; at offset 50
        const std::string key = "currency_Soft[0];";
        std::memcpy(&mockBuffer[50], key.data(), key.size());

        const auto parsed = SaveManager::ParseParameters(mockBuffer);
        Require(!parsed.empty(), "Synthetic buffer should parse currency_Soft");
        Require(parsed[0].value == 777, "Parsed value must match 777");

        // GameDetector test
        const auto detected = GameDetector::DetectInstallation();
        Require(!detected.engine.empty(), "Engine name should be non-empty");

        std::filesystem::remove_all(directory);
        std::cout << "Configuration validation, persistence, save parsing and feature gates: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
