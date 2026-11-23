#include "GameDetector.hpp"
#include "SaveManager.hpp"
#include <fstream>
#include <regex>
#include <array>
#include <cstddef>

namespace trainer {
namespace {
std::optional<std::string> ReadText(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 256 * 1024) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    std::string data(static_cast<std::size_t>(size), '\0');
    if (!file.read(data.data(), static_cast<std::streamsize>(data.size()))) return std::nullopt;
    return data;
}
std::optional<std::string> Field(const std::string& data, const char* field) {
    const std::regex expression(std::string("\"") + field + "\"\\s+\"([^\"]*)\"");
    std::sregex_iterator it(data.begin(), data.end(), expression), end;
    if (it == end) return std::nullopt;
    auto value = (*it)[1].str();
    if (++it != end) return std::nullopt;
    return value;
}
std::vector<std::filesystem::path> SteamLibraries(const std::filesystem::path& steam) {
    std::vector<std::filesystem::path> result{steam};
    const auto contents = ReadText(steam / L"steamapps" / L"libraryfolders.vdf");
    if (!contents) return result;
    const std::regex pattern(R"vdf("path"\s+"([^"]+)")vdf");
    for (std::sregex_iterator it(contents->begin(), contents->end(), pattern), end; it != end; ++it) {
        auto value = (*it)[1].str();
        for (std::size_t pos = 0; (pos = value.find("\\\\", pos)) != std::string::npos; ++pos) value.replace(pos, 2, "\\");
        result.emplace_back(Wide(value));
    }
    return result;
}
}
std::string GameDetector::EngineEvidence(const std::vector<ModuleInfo>& modules) {
    bool unity = false, il2cpp = false, mono = false;
    for (const auto& m : modules) {
        if (_wcsicmp(m.name.c_str(), L"UnityPlayer.dll") == 0) unity = true;
        if (_wcsicmp(m.name.c_str(), L"GameAssembly.dll") == 0) il2cpp = true;
        if (_wcsicmp(m.name.c_str(), L"mono-2.0-bdwgc.dll") == 0) mono = true;
    }
    if (unity) return il2cpp ? "Unity module + IL2CPP candidate" : mono ? "Unity module + Mono module" : "UnityPlayer.dll observed";
    return "Unknown (no recognized module evidence)";
}
std::string GameDetector::QueryFileVersion(const std::filesystem::path& path) {
    DWORD ignored{};
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0 || size > 1024 * 1024) return "Unknown";
    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return "Unknown";
    VS_FIXEDFILEINFO* info = nullptr; UINT length{};
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        !info || length < sizeof(*info) || info->dwSignature != 0xfeef04bd) return "Unknown";
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
        std::to_string(HIWORD(info->dwFileVersionLS)) + "." + std::to_string(LOWORD(info->dwFileVersionLS));
}
GameInstallation GameDetector::InspectExecutable(const std::filesystem::path& executable) {
    GameInstallation result;
    result.saveDirectory = SaveManager::DefaultSaveDirectory();
    std::error_code ec;
    if (_wcsicmp(executable.filename().c_str(), L"Slutopia.exe") != 0 || !std::filesystem::is_regular_file(executable, ec)) return result;
    result.installed = true; // Candidate by filename, not a build/schema verification.
    result.executablePath = executable;
    result.fileVersion = QueryFileVersion(executable);
    const auto gameDirectory = executable.parent_path();
    // .../steamapps/common/<installdir>/Slutopia.exe
    const auto steamapps = gameDirectory.parent_path().parent_path();
    const auto contents = ReadText(steamapps / L"appmanifest_4612690.acf");
    if (contents) {
        const auto app = Field(*contents, "appid"), directory = Field(*contents, "installdir"), build = Field(*contents, "buildid");
        if (app && *app == "4612690" && directory && Wide(*directory) == gameDirectory.filename().wstring() &&
            build && !build->empty() && build->find_first_not_of("0123456789") == std::string::npos) result.buildId = *build;
        else result.error = "Manifest identity/directory/build fields are missing, ambiguous or invalid";
    } else result.error = "Steam manifest unavailable; build identity is unverified";
    return result;
}
GameInstallation GameDetector::DetectInstallation() {
    GameInstallation result;
    result.saveDirectory = SaveManager::DefaultSaveDirectory();
    std::string error;
    const auto process = FindProcess(L"Slutopia.exe", &error);
    if (process) {
        const auto image = QueryImagePath(process->pid);
        if (!image.empty()) result = InspectExecutable(image);
        result.running = true; result.runningPid = process->pid;
        const auto modules = EnumerateModules(process->pid, &error);
        result.engine = EngineEvidence(modules);
        if (result.installed) return result;
    }
    std::vector<std::filesystem::path> libraries;
    std::array<wchar_t,32768> registryPath{};
    DWORD size = static_cast<DWORD>(registryPath.size() * sizeof(wchar_t));
    const auto status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
        nullptr, registryPath.data(), &size);
    if (status == ERROR_SUCCESS) libraries = SteamLibraries(registryPath.data());
    for (const auto* path : {L"C:\\Program Files (x86)\\Steam", L"C:\\Program Files\\Steam", L"D:\\SteamLibrary", L"E:\\SteamLibrary"})
        libraries.emplace_back(path);
    for (const auto& library : libraries) {
        auto candidate = InspectExecutable(library / L"steamapps" / L"common" / L"Slutopia" / L"Slutopia.exe");
        if (candidate.installed) {
            candidate.running = result.running; candidate.runningPid = result.runningPid;
            return candidate;
        }
    }
    result.error = error;
    return result;
}
} // namespace trainer
