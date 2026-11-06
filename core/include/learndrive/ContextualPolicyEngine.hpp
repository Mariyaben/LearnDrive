#pragma once
#include "PriorityArbiter.hpp"
#include <array>

namespace ld::cognition {
struct PolicyDecision {
    Action action = Action::none;
    int priority = 4; // 2 is highest contextual priority; level 1 is reflex-only.
    bool suppressOtherAlerts{};
    const char* reason = "no contextual action";
    double threatScore{};
};

// Deterministic tactical authority. It consumes only measured scene fields and
// can select only the bounded Action vocabulary; it never generates language.
class ContextualPolicyEngine {
public:
    PolicyDecision evaluate(const SceneState& scene, double now) const noexcept;
};

struct EmbeddedDecision {
    Action action = Action::none;
    bool brakeNow{};
    bool suppressOtherAlerts{};
    const char* reason = "no contextual action";
    ReflexCommand reflexCommand = ReflexCommand::none;
};

// Mobile coordinator for the synchronous 100 Hz rules worker. The contextual
// policy evaluates at most every 100 ms, while reflex evaluation runs each tick.
// Cooldown/dedup are committed only after the audio worker accepts a prompt.
class EmbeddedDualBrain {
public:
    EmbeddedDecision evaluate(const SceneState& scene, double now) noexcept;
    void accepted(Action action, bool brakeNow, double now) noexcept;
    void reset() noexcept;
private:
    ReflexEngine reflex_;
    ContextualPolicyEngine policy_;
    std::array<double, 7> lastAction_{};
    PolicyDecision pending_{};
    uint64_t evaluatedSequence_{};
    double lastEvaluation_ = -infinity;
    double lastNonEmergency_ = -infinity;
    double lastEmergency_ = -infinity;
    ReflexCommand lastReflex_ = ReflexCommand::none;
    ReflexCommand pendingReflex_ = ReflexCommand::none;
    double lastNow_=-infinity;
    bool initialized_{};
};
} // namespace ld::cognition
