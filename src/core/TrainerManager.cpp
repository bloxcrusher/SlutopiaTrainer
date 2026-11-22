#include "TrainerManager.hpp"
namespace trainer {
TrainerManager::TrainerManager(std::filesystem::path directory) : logger(directory / "trainer.log"),
    config(directory / "settings.json"), processes(logger) {
    settings = config.Load(notice);
    if (!notice.empty()) logger.Write(notice);
    processes.Configure(Wide(settings.executable), settings.autoConnect);
}
void TrainerManager::Save() {
    if (config.Save(settings, notice)) { notice = "Settings saved"; processes.Configure(Wide(settings.executable), settings.autoConnect); }
    logger.Write(notice);
}
void TrainerManager::Connect() { processes.Configure(Wide(settings.executable), true); }
void TrainerManager::Disconnect() { processes.Configure(Wide(settings.executable), false); }
}
