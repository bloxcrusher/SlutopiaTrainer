#include "core/TrainerManager.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace trainer;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"trainer-detection-" + std::to_wstring(GetCurrentProcessId()));
    try {
        const auto steamapps = root / L"steamapps";
        const auto game = steamapps / L"common" / L"Slutopia" / L"Slutopia.exe";
        std::filesystem::create_directories(game.parent_path());
        { std::ofstream file(game); file << "Not an actual game binary"; }
        const auto manifest = steamapps / L"appmanifest_4612690.acf";
        auto put = [&](std::string data) { std::ofstream file(manifest); file << data; };
        put(R"("AppState" { "appid" "4612690" "installdir" "Slutopia" "buildid" "12345" })");
        auto info = GameDetector::InspectExecutable(game);
        Check(info.installed && info.buildId == "12345", "Manifest is read from steamapps, not common");
        Check(info.engine == "Unknown" && info.productVersion == "Unknown" && info.fileVersion == "Unknown", "No fabricated engine or file version");
        put(R"("appid" "4612700" "installdir" "Slutopia" "buildid" "12345")");
        Check(GameDetector::InspectExecutable(game).buildId == "Unknown", "Reject wrong app identity");
        put(R"("appid" "4612690" "installdir" "Other" "buildid" "12345")");
        Check(GameDetector::InspectExecutable(game).buildId == "Unknown", "Reject wrong install directory");
        put(R"("appid" "4612690" "installdir" "Slutopia" "buildid" "123" "buildid" "456")");
        Check(GameDetector::InspectExecutable(game).buildId == "Unknown", "Reject ambiguous build IDs");
        std::filesystem::remove(manifest);
        Check(!GameDetector::InspectExecutable(game).error.empty(), "Report unavailable metadata");
        const std::vector<ModuleInfo> modules{{L"UnityPlayer.dll"}, {L"mono-2.0-bdwgc.dll"}};
        Check(GameDetector::EngineEvidence(modules) == "Unity module + Mono module", "Module names do not establish Unity version or architecture");
        std::string error;
        ConfigManager config(root / L"trainer" / L"settings.json");
        Check(config.Save(Settings{"Custom.exe", false, true, false}, error), "Create settings");
        {
            TrainerManager trainer(root / L"trainer", false);
            Check(trainer.settings.executable == "Custom.exe" && !trainer.settings.autoConnect, "Preserve target and auto-connect preference");
            Check(!trainer.saves.Info().candidate && !trainer.gameInfo.installed, "Smoke mode must not inspect real profile data");
        }
        std::filesystem::remove_all(root);
        std::cout << "Installation metadata and trainer isolation: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
