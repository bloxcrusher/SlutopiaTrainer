#include "ConfigManager.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace trainer {
namespace {
bool ValidName(const std::string& name) {
    return name.size() <= 240 && name.find_first_of("/\\:\r\n") == std::string::npos && name.find('\0') == std::string::npos;
}
}
Settings ConfigManager::Load(std::string& error) const {
    error.clear();
    try {
        if (!std::filesystem::exists(path_)) return {};
        if (std::filesystem::file_size(path_) > 65536) throw std::runtime_error("Configuration exceeds 64 KiB");
        std::ifstream file(path_);
        const auto j = nlohmann::json::parse(file);
        if (!j.is_object() || j.at("version").get<int>() != 1) throw std::runtime_error("Unsupported configuration schema");
        Settings s{j.at("executable").get<std::string>(), j.at("autoConnect").get<bool>(),
            j.at("hotkeys").get<bool>(), j.at("reducedMotion").get<bool>()};
        if (!ValidName(s.executable)) throw std::runtime_error("Invalid executable filename");
        return s;
    } catch (const std::exception& ex) { error = std::string("Configuration load failed: ") + ex.what(); return {}; }
}
bool ConfigManager::Save(const Settings& s, std::string& error) const {
    error.clear();
    try {
        if (!ValidName(s.executable)) throw std::runtime_error("Use an executable filename, not a path");
        if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path());
        auto temp = path_; temp += ".tmp";
        {
            std::ofstream file(temp, std::ios::trunc);
            file.exceptions(std::ios::failbit | std::ios::badbit);
            file << nlohmann::json{{"version",1},{"executable",s.executable},{"autoConnect",s.autoConnect},
                {"hotkeys",s.hotkeys},{"reducedMotion",s.reducedMotion}}.dump(2) << '\n';
            file.close();
        }
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Atomic replace failed, Windows error " + std::to_string(GetLastError()));
#else
        std::filesystem::rename(temp, path_);
#endif
        return true;
    } catch (const std::exception& ex) { error = std::string("Configuration save failed: ") + ex.what(); return false; }
}
}
