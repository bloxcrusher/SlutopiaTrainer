#pragma once
#include "ProcessInspector.hpp"
namespace trainer {
class GameDetector {
public:
    // Runtime module evidence only, never an identification of a particular game/build.
    static std::string EngineEvidence(const std::vector<ModuleInfo>& modules);
};
}
