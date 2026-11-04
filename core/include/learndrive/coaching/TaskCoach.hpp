#pragma once
#include <cstdint>
#include <string>
namespace ld::coaching {
struct Candidate;struct WorldState;class ShortMemory;class EventLedger;
enum class TaskPhase { Off, Waiting, Preparing, AwaitingDelivery, ObservingResponse, Approach, ExecutionQuiet, Uncertain, WaitingWindow, Entry };
struct TaskProgress {
    TaskPhase phase=TaskPhase::Off;
    uint64_t source{},route{},event{},epoch{};
    double startedAt=-1,leadSeconds=-1,responseSince=-1;
    bool planningDelivered{},approachDelivered{},speedReductionObserved{},entryDelivered{};
    unsigned opportunities{},planningCompletions{},approachCompletions{},observedReductions{},unassessedOutcomes{},entryCompletions{};
    // Execution quiet survives a route/GPS dropout for a bounded time. Missing
    // evidence near a junction is a reason for routine silence, not for speech.
    double quietUntil=-1;
    uint64_t quietEvent{};
    unsigned quietHolds{};
};
// Experimental timing constants of the task schedule (seconds of lead time).
inline constexpr double planningWindowOpen=40,planningWindowClose=22,approachWindowOpen=18,approachWindowClose=13;
inline constexpr double executionQuietLead=8,executionQuietHold=10,minimumUsefulWindow=3,episodeStepSpacing=8;
// A bounded teaching episode. No inference of mirror, indicator, control use or
// manoeuvre correctness. Evidence and sequence are rechecked at delivery onset.
class TaskCoach {
public:
    void enable(bool value) noexcept;
    bool enabled() const noexcept {return enabled_;}
    void sourceChanged() noexcept;
    void observe(const WorldState&,const ShortMemory&,const EventLedger&) noexcept;
    Candidate proposal(const WorldState&) const noexcept;
    bool valid(const Candidate&,const WorldState&,double,const EventLedger&) const noexcept;
    // Already-started speech of a lesson step: same episode, fresh evidence and
    // still outside execution quiet. The onset window is not re-applied, or a
    // cue started near the end of its window is truncated mid-sentence.
    bool continuing(const Candidate&,const WorldState&,double,const EventLedger&) const noexcept;
    static bool evidence(const Candidate&,const WorldState&,double,bool started=false) noexcept;
    const TaskProgress& progress() const noexcept {return state_;}
    std::string json() const;
    // Task-aware scheduling. Disabled reproduces the 0.7.1 behavior exactly.
    void enableScheduling(bool value) noexcept {scheduling_=value;if(!value){state_.quietUntil=-1;state_.quietEvent=0;}}
    bool scheduling() const noexcept {return scheduling_;}
    bool quietHeld(double now) const noexcept {return scheduling_&&enabled_&&state_.quietUntil>now;}
    // True when a routine cue accepted now (blocking ordinary speech for
    // blockSeconds at the current measured speed) would leave the next pending
    // step of this episode less than minimumUsefulWindow of its delivery window.
    bool reserves(double blockSeconds) const noexcept;
private:
    bool enabled_{},scheduling_=true;
    TaskProgress state_{};
};
}
