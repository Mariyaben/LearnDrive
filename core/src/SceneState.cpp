#include "learndrive/SceneState.hpp"
#include <iomanip>
#include <locale>
#include <sstream>

namespace ld::cognition {
bool fresh(double now, double timestamp, double maxAge) noexcept {
    return std::isfinite(now) && now >= 0 && std::isfinite(timestamp) && timestamp >= 0 &&
           now >= timestamp && now - timestamp < maxAge;
}
bool hardBraking(const Telemetry& t, double now) noexcept {
    return fresh(now, t.time, .15) && (t.hardBraking ||
        (std::isfinite(t.longitudinalAcceleration) && -t.longitudinalAcceleration > 3.0));
}
const char* hazardName(HazardType t) noexcept {
    switch (t) {
    case HazardType::vehicle: return "vehicle";
    case HazardType::bus: return "bus";
    case HazardType::pedestrian: return "pedestrian";
    case HazardType::pothole: return "pothole";
    case HazardType::sign: return "sign";
    case HazardType::bend: return "bend";
    default: return "unknown";
    }
}
const char* laneName(Lane lane) noexcept {
    switch (lane) {
    case Lane::ego: return "ego";
    case Lane::adjacent: return "adjacent";
    case Lane::sidewalk: return "sidewalk";
    default: return "unknown";
    }
}
std::string serializeScene(const SceneState& scene, double now) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(1);
    auto number = [&](double value, bool valid) {
        if (valid && std::isfinite(value)) out << value;
        else out << "null";
    };
    const auto& t = scene.telemetry;
    const bool telemetryFresh = fresh(now, t.time, .15);
    out << "{\"speed_kmh\":";
    number(t.speedKmh, telemetryFresh && t.speedValid && t.speedKmh >= 0);
    out << ",\"incline_deg\":";
    number(t.pitchDegrees, telemetryFresh && t.pitchValid && std::abs(t.pitchDegrees) <= 30);
    out << ",\"braking\":" << ((telemetryFresh && t.braking) || hardBraking(t, now) ? "true" : "false");
    out << ",\"hazards\":[";
    std::array<size_t, maxHazards> order{};
    size_t count = 0;
    if (fresh(now, scene.captureTime)) {
        for (size_t i = 0; i < std::min(scene.count, maxHazards); ++i) {
            const auto& h = scene.hazards[i];
            if (std::isfinite(h.confidence) && h.confidence >= .65 && h.confidence <= 1) order[count++] = i;
        }
    }
    // In-trajectory hazards precede parallel noise; urgent objects cannot be
    // crowded out by arbitrary detector order when limiting the token budget.
    std::stable_sort(order.begin(), order.begin() + count, [&](size_t a, size_t b) {
        const auto& x = scene.hazards[a]; const auto& y = scene.hazards[b];
        if (x.inTrajectory != y.inTrajectory) return x.inTrajectory;
        auto tti = [](double v) { return std::isfinite(v) && v >= 0 ? v : infinity; };
        return tti(x.ttiSeconds) < tti(y.ttiSeconds);
    });
    const size_t kept = std::min(count, size_t(12));
    for (size_t j = 0; j < kept; ++j) {
        const auto& h = scene.hazards[order[j]];
        if (j) out << ',';
        out << "{\"type\":\"" << hazardName(h.type) << "\",\"distance_m\":";
        number(h.distanceM, h.distanceM >= 0);
        out << ",\"tti_s\":";
        number(std::max(0.0, h.ttiSeconds - (now - scene.captureTime)), std::isfinite(h.ttiSeconds) && h.ttiSeconds >= 0);
        out << ",\"lane\":\"" << laneName(h.lane) << "\",\"in_path\":" << (h.inTrajectory ? "true" : "false")
            << ",\"decelerating\":" << (h.decelerating ? "true" : "false") << '}';
    }
    out << "],\"omitted\":" << count - kept << '}';
    return out.str();
}
} // namespace ld::cognition
