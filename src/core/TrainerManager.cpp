#include "TrainerManager.hpp"

namespace trainer {

TrainerManager::TrainerManager(std::filesystem::path directory)
    : logger(directory / "trainer.log"),
      config(directory / "settings.json"),
      processes(logger) {
    settings = config.Load(notice);
    if (!notice.empty()) logger.Write(notice);

    DetectGame();

    // If no target was manually configured, but game was detected, default to detected executable
    if (settings.executable.empty() && gameInfo.installed) {
        settings.executable = "Slutopia.exe";
        settings.autoConnect = true;
    }

    processes.Configure(Wide(settings.executable), settings.autoConnect);
    RefreshSave();
}

void TrainerManager::DetectGame() {
    gameInfo = GameDetector::DetectInstallation();
    if (gameInfo.installed) {
        logger.Write("Slutopia detected at: " + gameInfo.executablePath.string() +
                      " (Build: " + gameInfo.buildId + ", PE: " + gameInfo.fileVersion + ")");
    }
}

void TrainerManager::RefreshSave() {
    std::string err;
    if (saves.Load(err)) {
        logger.Write("Save loaded: " + saves.Info().playerId + " (" +
                      std::to_string(saves.Info().parameters.size()) + " verified parameters)");
    } else if (!err.empty()) {
        logger.Write("Save inspection notice: " + err);
    }
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
