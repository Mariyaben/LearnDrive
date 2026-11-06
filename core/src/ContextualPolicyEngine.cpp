#include "learndrive/ContextualPolicyEngine.hpp"

namespace ld::cognition {
PolicyDecision ContextualPolicyEngine::evaluate(const SceneState& scene, double now) const noexcept {
    if (!fresh(now, scene.captureTime) || scene.telemetry.shock || classifyDriverAction(scene.telemetry, now) != DriverAction::coasting)
        return {Action::none, 4, false, "context unavailable or driver braking"};
    bool vehicle = false, deceleratingVehicle = false, pedestrian = false;
    bool pothole = false, bend = false, sign = false;
    double vehicleTti = infinity, pedestrianTti = infinity, potholeTti = infinity;
    double vehicleThreat = 0, pedestrianThreat = 0, potholeThreat = 0;
    const double egoMps = scene.telemetry.speedValid && std::isfinite(scene.telemetry.speedKmh)
        ? std::max(0.1, scene.telemetry.speedKmh/3.6) : 0.1;
    for (size_t i = 0; i < std::min(scene.count, maxHazards); ++i) {
        const auto& h = scene.hazards[i];
        if (!std::isfinite(h.confidence) || h.confidence < .65 || h.confidence > 1) continue;
        const double tti = std::isfinite(h.ttiSeconds) && h.ttiSeconds >= 0
            ? std::max(0.0, h.ttiSeconds - (now - scene.captureTime)) : infinity;
        const double lateral = std::max(.25, std::isfinite(h.lateralDistanceM) ? std::abs(h.lateralDistanceM) : 1.0);
        const double denominator = std::max(.1, tti)*lateral;
        const double severity = (h.type==HazardType::bus || h.type==HazardType::pedestrian) ? 1.0 :
            h.type==HazardType::pothole ? .25 : h.type==HazardType::tailgater ? .10 : .75;
        const double threat = std::isfinite(tti) ? severity*egoMps/denominator : 0;
        if (h.type == HazardType::sign && h.lane == Lane::ego && std::isfinite(h.distanceM) &&
            h.distanceM >= 0 && h.distanceM <= 40) sign = true;
        if (!h.inTrajectory) continue;
        if (h.type == HazardType::tailgater) continue;
        if (h.type == HazardType::vehicle || h.type == HazardType::bus) {
            vehicle |= tti < 6.0 || h.decelerating;
            deceleratingVehicle |= h.decelerating;
            vehicleTti = std::min(vehicleTti, tti);
            vehicleThreat = std::max(vehicleThreat, threat);
        } else if (h.type == HazardType::pedestrian) {
            pedestrian = true; pedestrianTti = std::min(pedestrianTti, tti); pedestrianThreat=std::max(pedestrianThreat,threat);
        } else if (h.type == HazardType::pothole) {
            pothole = true; potholeTti = std::min(potholeTti, tti); potholeThreat=std::max(potholeThreat,threat);
        } else if (h.type == HazardType::bend) bend = true;
    }
    // A lead vehicle constrains lateral avoidance. Holding the lane/gap wins
    // over a bump warning, especially when the lead vehicle is decelerating.
    if (vehicle && pothole&&std::max(vehicleThreat,potholeThreat)>=pedestrianThreat)
        return {Action::holdLaneAndDistance, 2, true, "lead vehicle makes pothole avoidance unsafe", std::max(vehicleThreat,potholeThreat)};
    if (pedestrian && (pedestrianTti < 5.0 || !std::isfinite(pedestrianTti))&&
        pedestrianThreat>=std::max(vehicleThreat,potholeThreat))
        return {Action::watchPedestrians, 2, true, "pedestrian intersects active trajectory", pedestrianThreat};
    if (vehicle && (deceleratingVehicle || vehicleTti < 5.0)&&vehicleThreat>=potholeThreat)
        return {Action::keepSafeGap, vehicleTti < 3.0 ? 2 : 3, true, "approaching lead vehicle", vehicleThreat};
    if (pothole && (potholeTti < 5.0 || !std::isfinite(potholeTti)))
        return {Action::slowSmoothly, 3, false, "pothole in active trajectory", potholeThreat};
    if (bend) return {Action::prepareBend, 3, false, "bend in active trajectory"};
    if (sign) return {Action::observeSign, 4, false, "relevant ego-lane sign"};
    return {};
}

void EmbeddedDualBrain::reset() noexcept {
    lastAction_.fill(-infinity); pending_ = {}; evaluatedSequence_ = 0;
    lastEvaluation_ = lastNonEmergency_ = lastEmergency_ = lastNow_ = -infinity;
    lastReflex_=pendingReflex_=ReflexCommand::none;initialized_ = false;
}

EmbeddedDecision EmbeddedDualBrain::evaluate(const SceneState& scene, double now) noexcept {
    if (!initialized_) { lastAction_.fill(-infinity); initialized_ = true; }
    if (!std::isfinite(now) || now < 0||now<lastNow_) return {};
    lastNow_=now;
    const auto reflex = reflex_.evaluate(scene, now);
    pendingReflex_=reflex.command;
    if (reflex.command != ReflexCommand::none) {
        if (now - lastEmergency_ >= 2.0||reflexSeverity(reflex.command)>reflexSeverity(lastReflex_))
            return {Action::none, true, true, reflex.command == ReflexCommand::BRAKE_HARDER ?
                "level 0 reflex: braking insufficient" : "level 0 reflex: impact time below threshold", reflex.command};
        return {Action::none, false, true, "level 1 reflex already announced"};
    }
    if (reflex.suppressDistanceAlerts) {
        pending_ = {};
        return {Action::none, false, true, "driver braking: routine distance guidance suppressed"};
    }
    if (!fresh(now, scene.captureTime)) { pending_ = {}; return {}; }
    if (now - lastEvaluation_ >= .1) {
        pending_ = policy_.evaluate(scene, now);
        evaluatedSequence_ = scene.sequence; lastEvaluation_ = now;
    }
    const size_t index = static_cast<size_t>(pending_.action);
    if (index == 0 || index >= lastAction_.size() || now - lastNonEmergency_ < 3.5 ||
        now - lastAction_[index] < 10.0 || !actionSupported(pending_.action, scene)) return {};
    return {pending_.action, false, pending_.suppressOtherAlerts, pending_.reason};
}

void EmbeddedDualBrain::accepted(Action action, bool brakeNow, double now) noexcept {
    if (!std::isfinite(now) || now < 0) return;
    if (brakeNow) { lastEmergency_ = now;lastReflex_=pendingReflex_;return; }
    const size_t index = static_cast<size_t>(action);
    if (index == 0 || index >= lastAction_.size()) return;
    lastAction_[index] = lastNonEmergency_ = now;
    if (pending_.action == action) pending_ = {};
}
} // namespace ld::cognition
