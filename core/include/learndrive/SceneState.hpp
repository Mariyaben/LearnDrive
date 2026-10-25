#pragma once
#include "types.hpp"
#include <array>
#include <cstdint>

namespace ld::cognition {
constexpr size_t maxHazards = 64;
enum class HazardType { vehicle, bus, pedestrian, pothole, sign, bend, unknown, tailgater };
enum class Lane { ego, adjacent, sidewalk, unknown };
struct SceneHazard {
    uint64_t id{};
    HazardType type = HazardType::unknown;
    Lane lane = Lane::unknown;
    double distanceM = infinity; // Only calibrated metric ranging; never box size.
    double ttiSeconds = infinity;
    double confidence{};
    bool inTrajectory{};
    bool decelerating{};
    double lateralDistanceM = 1.0;
    double opticalRelativeVelocityMps{}; // Negative means closing.
};
// Fixed capacity bounds copies and reflex work; captureTime is the vision clock.
// Producers must use the same monotonic seconds as Telemetry::time and tick().
struct SceneState {
    uint64_t sequence{};
    double captureTime = -infinity;
    Telemetry telemetry{};
    std::array<SceneHazard, maxHazards> hazards{};
    size_t count{};
};
bool fresh(double now, double timestamp, double maxAge = maxVisionAgeSeconds) noexcept;
bool hardBraking(const Telemetry& telemetry, double now) noexcept;
const char* hazardName(HazardType type) noexcept;
const char* laneName(Lane lane) noexcept;
// Worker-thread only. Invalid measurements become null, never JSON NaN/Infinity.
// All reflex hazards are considered; the token budget retains the top 12 here.
std::string serializeScene(const SceneState& scene, double now);
} // namespace ld::cognition
