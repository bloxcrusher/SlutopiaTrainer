#pragma once
#include "ConfigManager.hpp"
#include "GameDetector.hpp"
#include "ProcessManager.hpp"
#include "SaveManager.hpp"
#include "FeatureManager.hpp"

namespace trainer {

class TrainerManager {
public:
    explicit TrainerManager(std::filesystem::path directory);
    void Save();
    void Connect();
    void Disconnect();
    void DetectGame();
    void RefreshSave();

    Settings settings;
    Logger logger;
    ConfigManager config;
    ProcessManager processes;
    SaveManager saves;
    GameInstallation gameInfo;
    std::string notice;
};

} // namespace trainer
