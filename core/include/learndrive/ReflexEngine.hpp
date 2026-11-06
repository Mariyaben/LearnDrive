#pragma once
#include "SceneState.hpp"

namespace ld::cognition {
enum class DriverAction { coasting, moderateBraking, hardBraking };
enum class ReflexCommand { none, BRAKE_NOW, BRAKE_HARDER, EMERGENCY_STOP };
constexpr int reflexSeverity(ReflexCommand command) noexcept {
    return command==ReflexCommand::BRAKE_HARDER?3:command==ReflexCommand::EMERGENCY_STOP?2:
        command==ReflexCommand::BRAKE_NOW?1:0;
}
struct ReflexResult {
    ReflexCommand command = ReflexCommand::none;
    bool suppressDistanceAlerts{};
    uint64_t hazardId{};
    int priority{}; // 1 is highest. Separate from legacy ld::priority().
    DriverAction driverAction = DriverAction::coasting;
};
DriverAction classifyDriverAction(const Telemetry& telemetry, double now) noexcept;
class ReflexEngine {
public:
    // Call at 100 Hz with the latest scene and fresh sensor telemetry.
    // No allocation, I/O, locks, inference or speech synthesis on this path.
    ReflexResult evaluate(const SceneState& scene, double now) const noexcept;
};
} // namespace ld::cognition
