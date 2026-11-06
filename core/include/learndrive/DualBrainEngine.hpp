#pragma once
#include "ContextualCognitionEngine.hpp"
#include "ContextualPolicyEngine.hpp"
#include "PostDriveSummary.hpp"

namespace ld::cognition {
class DualBrainEngine {
public:
    DualBrainEngine(SpeechDispatcher& speech, ContextualCognitionEngine::ModelFactory factory,
                    SceneClock clock = monotonicSeconds, CognitionConfig config = {});
    // Camera/vision producer, one latest scene. Never waits for inference/tick.
    bool publish(const SceneState& scene) noexcept;
    // Dedicated serial rules thread, every 10 ms. Telemetry is refreshed at
    // 100 Hz, while captureTime and object measurements remain camera timestamps.
    ReflexResult tick(const Telemetry& telemetry, double now) noexcept;
    bool minor(Action action, double now) noexcept { return arbiter_.minor(action, now); }
    void reset() noexcept; // Rules thread: clear old session on mode/clock changes.
    void stop() noexcept {driving_=false;}
    std::future<std::string> summarizeAfterDrive(std::string telemetryJson);
private:
    ReflexEngine reflex_;
    ContextualPolicyEngine policy_;
    PriorityArbiter arbiter_;
    ContextualCognitionEngine::ModelFactory summaryFactory_;
    bool driving_=true;
    std::mutex sceneMutex_;
    SceneState published_{}, current_{};
    bool hasPublished_{}, inhibited_{};
    double lastSubmit_ = -infinity, lastTick_ = -infinity;
};
} // namespace ld::cognition
