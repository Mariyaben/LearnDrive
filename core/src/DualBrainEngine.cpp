#include "learndrive/DualBrainEngine.hpp"

namespace ld::cognition {
DualBrainEngine::DualBrainEngine(SpeechDispatcher& speech, ContextualCognitionEngine::ModelFactory factory,
                                 SceneClock clock, CognitionConfig config)
    : arbiter_(speech), summaryFactory_(std::move(factory)) {(void)clock;(void)config;}
bool DualBrainEngine::publish(const SceneState& scene) noexcept {
    if (!std::isfinite(scene.captureTime) || scene.captureTime < 0 || scene.count > maxHazards) return false;
    std::unique_lock<std::mutex> lock(sceneMutex_, std::try_to_lock);
    if (!lock.owns_lock() || (hasPublished_ && scene.captureTime <= published_.captureTime)) return false;
    published_ = scene; hasPublished_ = true; return true;
}
ReflexResult DualBrainEngine::tick(const Telemetry& telemetry, double now) noexcept {
    if (!driving_||!std::isfinite(now) || now < 0 || now < lastTick_) return {};
    lastTick_ = now;
    {
        std::unique_lock<std::mutex> lock(sceneMutex_, std::try_to_lock);
        if (lock.owns_lock() && hasPublished_) current_ = published_;
    }
    current_.telemetry = telemetry;
    const auto reflex = reflex_.evaluate(current_, now);
    arbiter_.reflex(reflex, now); // Preempt speech before touching the SLM mailbox.
    const bool inhibit = reflex.command != ReflexCommand::none || reflex.suppressDistanceAlerts || !fresh(now, current_.captureTime);
    if (inhibit) {
        inhibited_ = true;
        return reflex;
    }
    inhibited_ = false;
    // Generated text has no route to the driving arbiter. Only deterministic
    // measured-scene actions may be dispatched while driving.
    if(now-lastSubmit_>=.1) {
        const auto d=policy_.evaluate(current_,now);
        if(d.action!=Action::none)arbiter_.contextual({d.action,d.priority,d.suppressOtherAlerts,
            current_.captureTime,0},current_,0,now);
        lastSubmit_=now;
    }
    return reflex;
}
void DualBrainEngine::reset() noexcept {
    arbiter_.reset(); current_ = {};driving_=true;
    // reset is a lifecycle/rules operation; the camera producer always try-locks.
    std::lock_guard<std::mutex> lock(sceneMutex_);
    published_ = {}; hasPublished_ = inhibited_ = false;
    lastSubmit_ = lastTick_ = -infinity;
}
std::future<std::string> DualBrainEngine::summarizeAfterDrive(std::string telemetry) {
    if(driving_)throw std::logic_error("stop the drive before requesting a summary");
    return PostDriveSummary::start(summaryFactory_,std::move(telemetry));
}
} // namespace ld::cognition
