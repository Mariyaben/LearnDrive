#include "learndrive/ReflexEngine.hpp"

namespace ld::cognition {
DriverAction classifyDriverAction(const Telemetry& telemetry, double now) noexcept {
    if (!fresh(now, telemetry.time, .15) || !std::isfinite(telemetry.longitudinalAcceleration))
        return DriverAction::coasting;
    if (telemetry.longitudinalAcceleration < -3.5)
        return DriverAction::hardBraking;
    if (telemetry.longitudinalAcceleration <= -1.8)
        return DriverAction::moderateBraking;
    return DriverAction::coasting;
}
ReflexResult ReflexEngine::evaluate(const SceneState& scene, double now) const noexcept {
    ReflexResult result;
    result.driverAction = classifyDriverAction(scene.telemetry, now);
    result.suppressDistanceAlerts = result.driverAction != DriverAction::coasting;
    if(fresh(now,scene.telemetry.time,.15)&&scene.telemetry.shock) {
        result.suppressDistanceAlerts=true;return result;
    }
    if (!fresh(now, scene.captureTime)) return result;
    const double age = now - scene.captureTime;
    double bestTti=infinity;
    for (size_t i = 0; i < std::min(scene.count, maxHazards); ++i) {
        const auto& h = scene.hazards[i];
        if (!std::isfinite(h.confidence) || h.confidence < .65 || h.confidence > 1 || !h.inTrajectory||h.type==HazardType::tailgater) continue;
        double tti = std::isfinite(h.ttiSeconds)&&h.ttiSeconds>=0 ? std::max(0.,h.ttiSeconds-age) : infinity;
        if (!std::isfinite(h.ttiSeconds) && std::isfinite(h.distanceM)&&h.distanceM>0&&h.opticalRelativeVelocityMps<-.1)
            tti=std::max(0.,h.distanceM/-h.opticalRelativeVelocityMps-age);
        const bool imminent = std::isfinite(tti) && tti >= 0 && tti < 2.0;
        const bool pedestrian = h.type == HazardType::pedestrian && std::isfinite(h.distanceM) &&
                                h.distanceM >= 0 && h.distanceM <= 5.0;
        // While the driver is already braking hard, routine distance speech is
        // muted. A critically collapsing TTI still escalates because the current
        // braking effort may be insufficient.
        const bool criticalWhileBraking = result.suppressDistanceAlerts && imminent && tti < 1.2;
        if ((!result.suppressDistanceAlerts && (imminent || pedestrian)) || criticalWhileBraking) {
            const auto command = criticalWhileBraking ? ReflexCommand::BRAKE_HARDER :
                (imminent&&tti<1.2?ReflexCommand::EMERGENCY_STOP:ReflexCommand::BRAKE_NOW);
            if(result.command==ReflexCommand::none||tti<bestTti) {
                result.command=command;result.hazardId=h.id;result.priority=0;bestTti=tti;
            }
        }
    }
    return result;
}
} // namespace ld::cognition
