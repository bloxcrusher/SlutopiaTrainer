#pragma once
#include "ProcessInspector.hpp"
#include <filesystem>

namespace trainer {

struct GameInstallation {
    bool installed = false;
    bool running = false;
    std::uint32_t runningPid = 0;
    std::wstring executableName = L"Slutopia.exe";
    std::filesystem::path executablePath;
    std::filesystem::path saveDirectory;
    std::string fileVersion = "Unknown";
    std::string productVersion = "Unknown";
    std::string buildId = "Unknown";
    std::string appId = "4612690";
    std::string engine = "Unity 6000.0 (Mono / x64)";
};

class GameDetector {
public:
    static std::string EngineEvidence(const std::vector<ModuleInfo>& modules);
    static GameInstallation DetectInstallation();
    static std::string QueryFileVersion(const std::filesystem::path& path);
};

} // namespace trainer
