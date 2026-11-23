#include "TrainerManager.hpp"

namespace trainer {

TrainerManager::TrainerManager(std::filesystem::path directory, bool inspectInstallation)
    : logger(directory / "trainer.log"),
      config(directory / "settings.json"),
      processes(logger) {
    settings = config.Load(notice);
    if (!notice.empty()) logger.Write(notice);

    if (inspectInstallation) DetectGame();

    // If no target was manually configured, but game was detected, default to detected executable
    if (settings.executable.empty() && gameInfo.installed) {
        settings.executable = "Slutopia.exe";
        // Preserve the user's auto-connect preference.
    }

    processes.Configure(Wide(settings.executable), settings.autoConnect);
    if (inspectInstallation) RefreshSave();
}

void TrainerManager::DetectGame() {
    gameInfo = GameDetector::DetectInstallation();
    if (gameInfo.installed) {
        logger.Write("Slutopia detected at: " + Utf8(gameInfo.executablePath.wstring()) +
                      " (Build: " + gameInfo.buildId + ", PE: " + gameInfo.fileVersion + ")");
    }
}

void TrainerManager::RefreshSave() {
    std::string err;
    saves.Load(err);
    notice = err;
    if (!err.empty()) logger.Write("Save inspection: " + err);
}

void TrainerManager::Save() {
    if (config.Save(settings, notice)) {
        notice = "Settings saved";
        processes.Configure(Wide(settings.executable), settings.autoConnect);
    }
    logger.Write(notice);
}

void TrainerManager::Connect() {
    processes.Configure(Wide(settings.executable), true);
}

void TrainerManager::Disconnect() {
    processes.Configure(Wide(settings.executable), false);
}

} // namespace trainer
