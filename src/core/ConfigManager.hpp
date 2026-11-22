#pragma once
#include <filesystem>
#include <string>
namespace trainer {
struct Settings {
    std::string executable;
    bool autoConnect = false;
    bool hotkeys = true;
    bool reducedMotion = false;
};
class ConfigManager {
public:
    explicit ConfigManager(std::filesystem::path path) : path_(std::move(path)) {}
    Settings Load(std::string& error) const;
    bool Save(const Settings& settings, std::string& error) const;
    const std::filesystem::path& Path() const { return path_; }
private:
    std::filesystem::path path_;
};
}
