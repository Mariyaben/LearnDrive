#include "learndrive/PriorityArbiter.hpp"

namespace ld::cognition {
const char* actionText(Action a) noexcept {
    switch (a) {
    case Action::holdLaneAndDistance: return "Hold lane and distance";
    case Action::keepSafeGap: return "Keep a safe gap";
    case Action::slowSmoothly: return "Slow down smoothly";
    case Action::watchPedestrians: return "Watch for pedestrians";
    case Action::prepareBend: return "Prepare for the bend";
    case Action::observeSign: return "Observe the road sign";
    default: return "";
    }
}
namespace {
// Intentionally a small schema reader, not a permissive substring extractor.
// Escapes/control/non-ASCII text are rejected: the permitted vocabulary is ASCII.
class Reader {
public:
    explicit Reader(std::string_view s) : s_(s) {}
    bool take(char c) { space(); if (i_ == s_.size() || s_[i_] != c) return false; ++i_; return true; }
    std::string_view string() {
        if (!take('"')) { valid_ = false; return {}; }
        size_t start = i_;
        while (i_ < s_.size() && s_[i_] != '"') {
            unsigned char c = static_cast<unsigned char>(s_[i_++]);
            if (c < 32 || c > 126 || c == '\\') { valid_ = false; return {}; }
        }
        if (i_ == s_.size()) { valid_ = false; return {}; }
        const auto result = s_.substr(start, i_ - start); ++i_; return result;
    }
    int priority() { space(); if (i_ == s_.size() || s_[i_] < '2' || s_[i_] > '4') return 0; return s_[i_++] - '0'; }
    bool boolean(bool& out) {
        space();
        if (s_.substr(i_, 4) == "true") { i_ += 4; out = true; return true; }
        if (s_.substr(i_, 5) == "false") { i_ += 5; out = false; return true; }
        return false;
    }
    bool done() { space(); return valid_ && i_ == s_.size(); }
private:
    void space() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_; }
    std::string_view s_;
    size_t i_{};
    bool valid_ = true;
};
}
bool parseResponse(std::string_view json, ContextualDecision& result) noexcept {
    if (json.empty() || json.size() > 256) return false;
    Reader r(json);
    ContextualDecision parsed;
    unsigned fields = 0;
    if (!r.take('{')) return false;
    for (int i = 0; i < 3; ++i) {
        if (i && !r.take(',')) return false;
        auto key = r.string();
        if (!r.take(':')) return false;
        unsigned bit = 0;
        if (key == "action_prompt") {
            bit = 1;
            auto text = r.string();
            for (int a = 1; a <= 6; ++a) if (text == actionText(static_cast<Action>(a))) parsed.action = static_cast<Action>(a);
            if (parsed.action == Action::none) return false;
        } else if (key == "priority") {
            bit = 2; parsed.priority = r.priority(); if (!parsed.priority) return false;
        } else if (key == "suppress_other_alerts") {
            bit = 4; if (!r.boolean(parsed.suppressOtherAlerts)) return false;
        } else return false;
        if (fields & bit) return false;
        fields |= bit;
    }
    if (fields != 7 || !r.take('}') || !r.done()) return false;
    result = parsed;
    return true;
}
bool actionSupported(Action action, const SceneState& scene) noexcept {
    bool vehicle = false, obstacle = false, pedestrian = false, bend = false, sign = false;
    for (size_t i = 0; i < std::min(scene.count, maxHazards); ++i) {
        const auto& h = scene.hazards[i];
        if (!std::isfinite(h.confidence) || h.confidence < .65 || h.confidence > 1) continue;
        sign |= h.type == HazardType::sign && h.lane == Lane::ego && std::isfinite(h.distanceM) && h.distanceM >= 0 && h.distanceM <= 40;
        if (!h.inTrajectory) continue;
        vehicle |= h.type == HazardType::vehicle || h.type == HazardType::bus;
        pedestrian |= h.type == HazardType::pedestrian;
        bend |= h.type == HazardType::bend;
        obstacle |= h.type == HazardType::pothole;
    }
    switch (action) {
    case Action::holdLaneAndDistance: case Action::keepSafeGap: return vehicle;
    case Action::slowSmoothly: return vehicle || obstacle || pedestrian || bend;
    case Action::watchPedestrians: return pedestrian;
    case Action::prepareBend: return bend;
    case Action::observeSign: return sign;
    default: return false;
    }
}
void PriorityArbiter::reset() noexcept {
    speech_.stopDistanceAlerts();
    lastAction_.fill(-infinity);
    lastNonEmergency_ = suppressUntil_ = lastEmergency_ = lastNow_ = -infinity;
    emergency_ = braking_ = false;
}
void PriorityArbiter::reflex(const ReflexResult& r, double now) noexcept {
    if (!std::isfinite(now) || now < 0 || now < lastNow_) return;
    lastNow_ = now;
    braking_ = r.suppressDistanceAlerts;
    emergency_ = r.command != ReflexCommand::none;
    if (braking_&&!emergency_) speech_.stopDistanceAlerts();
    if (!emergency_) return; // Brief detector gaps must not restart the sentence.
    // Do not restart the same sentence on all 100 ticks. Continuing emergencies
    // repeat at 2 seconds, bypassing both non-emergency cooldown and dedup.
    const bool harder=r.command==ReflexCommand::BRAKE_HARDER;
    const bool stop=r.command==ReflexCommand::EMERGENCY_STOP;
    const char* text=harder?"Brake harder":stop?"Emergency stop":"Brake now";
    const char* code=harder?"BRAKE_HARDER":stop?"EMERGENCY_STOP":"BRAKE_NOW";
    if ((now-lastEmergency_>=2.0||reflexSeverity(r.command)>reflexSeverity(lastCommand_))&&speech_.trySpeak({text,code,0,true,true})) {
        lastEmergency_ = now;
        lastCommand_=r.command;
        suppressUntil_ = std::max(suppressUntil_, now + 3.5);
    }
}
bool PriorityArbiter::speak(Action a, int priority, bool suppress, double now) noexcept {
    const size_t index = static_cast<size_t>(a);
    if (!std::isfinite(now) || now < 0 || now < lastNow_ || emergency_ || braking_ || index == 0 || index >= lastAction_.size() ||
        now - lastNonEmergency_ < 3.5 || now - lastAction_[index] < 10.0) return false;
    if (!speech_.trySpeak({actionText(a), "TACTICAL", priority, suppress, a != Action::prepareBend})) return false;
    lastNonEmergency_ = now; lastAction_[index] = now; lastNow_ = now;
    if (suppress) suppressUntil_ = now + 3.5;
    return true;
}
bool PriorityArbiter::contextual(const ContextualDecision& d, const SceneState& scene, uint64_t epoch, double now) noexcept {
    if (d.epoch != epoch || d.priority < 2 || d.priority > 4 || now < suppressUntil_ ||
        !fresh(now, d.captureTime) || !fresh(now, scene.captureTime) || !actionSupported(d.action, scene)) return false;
    return speak(d.action, d.priority, d.suppressOtherAlerts, now);
}
bool PriorityArbiter::minor(Action a, double now) noexcept {
    return now >= suppressUntil_ && speak(a, 4, false, now);
}
} // namespace ld::cognition
