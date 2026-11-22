#pragma once
#include "ConfigManager.hpp"
#include "ProcessManager.hpp"
#include "FeatureManager.hpp"
namespace trainer {
class TrainerManager {
public:
    explicit TrainerManager(std::filesystem::path directory);
    void Save();
    void Connect();
    void Disconnect();
    Settings settings;
    Logger logger;
    ConfigManager config;
    ProcessManager processes;
    std::string notice;
};
}
