#include "GameDetector.hpp"
namespace trainer {
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
}
