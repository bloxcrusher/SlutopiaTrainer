#pragma once
#include <string_view>
#include <span>
namespace trainer {
struct Feature { std::string_view name, category, reason; };
class FeatureManager {
public:
    // No verified game adapter is registered. Descriptors cannot execute memory operations.
    static std::span<const Feature> Unsupported();
};
}
