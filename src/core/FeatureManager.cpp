#include "FeatureManager.hpp"
#include <array>
namespace trainer {
std::span<const Feature> FeatureManager::Unsupported() {
    static constexpr std::array features{
        Feature{"Local resources", "RESOURCES", "Save schema and resource ownership have not been verified."},
        Feature{"Local counters", "RESOURCES", "No version-matched counter structure or address is confirmed."},
        Feature{"Confirmed values", "RESOURCES", "No verified game adapter is available. No values are displayed."},
        Feature{"Local timers", "PROGRESSION", "Timer storage and units have not been verified."},
        Feature{"Offline process speed", "PROGRESSION", "Offline simulation and server boundaries are unknown."},
        Feature{"Local progress", "PROGRESSION", "Progress persistence and safe rollback have not been validated."}
    };
    return features;
}
}
