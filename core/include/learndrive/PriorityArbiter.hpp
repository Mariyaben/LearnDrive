#pragma once
#include "ReflexEngine.hpp"
#include <array>
#include <string_view>

namespace ld::cognition {
// A bounded voice vocabulary prevents arbitrary model text becoming driving
// instructions. Cache these phrases in the platform TTS worker before driving.
enum class Action { none, holdLaneAndDistance, keepSafeGap, slowSmoothly, watchPedestrians, prepareBend, observeSign,
    ACTION_HOLD_DISTANCE_BUS=holdLaneAndDistance, ACTION_PREPARE_STOP=slowSmoothly };
const char* actionText(Action action) noexcept;
struct ContextualDecision {
    Action action = Action::none;
    int priority = 4;
    bool suppressOtherAlerts{};
    double captureTime = -infinity;
    uint64_t epoch{};
};
// Strict bounded JSON: exactly three unique fields, no prose or trailing data.
// Only 2-5-word approved actions pass; priority 1 is reserved for the reflex.
bool parseResponse(std::string_view json, ContextualDecision& result) noexcept;
bool actionSupported(Action action, const SceneState& scene) noexcept;

struct SpeechCommand {
    const char* text{};
    const char* code{}; // BRAKE_NOW for L1; otherwise TACTICAL.
    int priority{};
    bool flush{}; // Atomically stop current speech, clear queue, then play.
    bool distanceRelated{};
};
class SpeechDispatcher {
public:
    virtual ~SpeechDispatcher() = default;
    // Must be bounded, nonblocking, noexcept; enqueue cached audio only.
    // Return false on unavailable output. Cooldowns commit only on acceptance.
    virtual bool trySpeak(const SpeechCommand& command) noexcept = 0;
    virtual void stopDistanceAlerts() noexcept = 0;
};
// Single consumer: every method runs on the 100 Hz rules thread. Inference
// publishes to a mailbox and must never call the arbiter/audio directly.
class PriorityArbiter {
public:
    explicit PriorityArbiter(SpeechDispatcher& speech) : speech_(speech) { lastAction_.fill(-infinity); }
    void reflex(const ReflexResult& result, double now) noexcept;
    bool contextual(const ContextualDecision& decision, const SceneState& current,
                    uint64_t epoch, double now) noexcept;
    // Optional minor warning from the existing policy. Same global cooldown and
    // dedup, and suppressed while accepted contextual guidance holds the floor.
    bool minor(Action action, double now) noexcept;
    void reset() noexcept;
private:
    bool speak(Action action, int priority, bool suppress, double now) noexcept;
    SpeechDispatcher& speech_;
    std::array<double, 7> lastAction_{};
    double lastNonEmergency_ = -infinity, suppressUntil_ = -infinity;
    double lastEmergency_ = -infinity, lastNow_ = -infinity;
    bool emergency_{}, braking_{};
    ReflexCommand lastCommand_=ReflexCommand::none;
};
} // namespace ld::cognition
