#include "GameDetector.hpp"
#include "SaveManager.hpp"
#include <windows.h>
#include <fstream>
#include <regex>

#pragma comment(lib, "version.lib")

namespace trainer {

std::string GameDetector::EngineEvidence(const std::vector<ModuleInfo>& modules) {
    bool unity = false, il2cpp = false, mono = false;
    for (const auto& m : modules) {
        if (_wcsicmp(m.name.c_str(), L"UnityPlayer.dll") == 0) unity = true;
        if (_wcsicmp(m.name.c_str(), L"GameAssembly.dll") == 0) il2cpp = true;
        if (_wcsicmp(m.name.c_str(), L"mono-2.0-bdwgc.dll") == 0) mono = true;
    }
    if (unity) return il2cpp ? "Unity module + IL2CPP candidate" : mono ? "Unity 6000.0 (Mono runtime / x64)" : "UnityPlayer.dll observed";
    return "Unknown (no recognized module evidence)";
}

std::string GameDetector::QueryFileVersion(const std::filesystem::path& path) {
    DWORD handle = 0;
    const auto pathStr = path.wstring();
    const DWORD size = GetFileVersionInfoSizeW(pathStr.c_str(), &handle);
    if (size == 0) return "Unknown";

    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(pathStr.c_str(), handle, size, data.data())) return "Unknown";

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT len = 0;
    if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fileInfo), &len) && fileInfo) {
        const int major = HIWORD(fileInfo->dwFileVersionMS);
        const int minor = LOWORD(fileInfo->dwFileVersionMS);
        const int build = HIWORD(fileInfo->dwFileVersionLS);
        const int revision = LOWORD(fileInfo->dwFileVersionLS);
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(build) + "." + std::to_string(revision);
    }
    return "Unknown";
}

GameInstallation GameDetector::DetectInstallation() {
    GameInstallation info;
    info.saveDirectory = SaveManager::DefaultSaveDirectory();

    // 1. Check currently running processes
    std::string err;
    const auto proc = FindProcess(L"Slutopia.exe", &err);
    if (proc) {
        info.running = true;
        info.runningPid = proc->pid;
        info.executableName = L"Slutopia.exe";
        const auto img = QueryImagePath(proc->pid);
        if (!img.empty()) {
            info.executablePath = img;
            info.installed = true;
        }
    }

    // 2. Check Steam common installation paths
    if (!info.installed) {
        const std::filesystem::path candidates[] = {
            L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Slutopia\\Slutopia.exe",
            L"C:\\Program Files\\Steam\\steamapps\\common\\Slutopia\\Slutopia.exe",
            L"D:\\SteamLibrary\\steamapps\\common\\Slutopia\\Slutopia.exe",
            L"E:\\SteamLibrary\\steamapps\\common\\Slutopia\\Slutopia.exe"
        };
        for (const auto& c : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(c, ec)) {
                info.installed = true;
                info.executablePath = c;
                info.executableName = c.filename().wstring();
                break;
            }
        }
    }

    // 3. Check Steam registry path if still not found
    if (!info.installed) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t steamPath[MAX_PATH]{};
            DWORD dwSize = sizeof(steamPath);
            if (RegQueryValueExW(hKey, L"SteamPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(steamPath), &dwSize) == ERROR_SUCCESS) {
                const std::filesystem::path p = std::filesystem::path(steamPath) / L"steamapps" / L"common" / L"Slutopia" / L"Slutopia.exe";
                std::error_code ec;
                if (std::filesystem::exists(p, ec)) {
                    info.installed = true;
                    info.executablePath = p;
                    info.executableName = p.filename().wstring();
                }
            }
            RegCloseKey(hKey);
        }
    }

    // 4. Query PE file version and Steam appmanifest build id
    if (info.installed && !info.executablePath.empty()) {
        info.fileVersion = QueryFileVersion(info.executablePath);
        // Look for appmanifest_4612690.acf in parent steamapps folder
        const auto steamapps = info.executablePath.parent_path().parent_path();
        const auto manifest = steamapps / L"appmanifest_4612690.acf";
        std::error_code ec;
        if (std::filesystem::exists(manifest, ec)) {
            std::ifstream mf(manifest);
            std::string line;
            std::regex buildRe(R"raw("buildid"\s+"([0-9]+)")raw");
            while (std::getline(mf, line)) {
                std::smatch m;
                if (std::regex_search(line, m, buildRe)) {
                    info.buildId = m[1].str();
                    break;
                }
            }
        }
        info.productVersion = "6000.0.60f1";
    }

    return info;
}

} // namespace trainer
