#pragma once
#include <cstdint>
#include <string>
namespace ld::coaching {
struct Candidate;struct WorldState;class ShortMemory;class EventLedger;
enum class TaskPhase { Off, Waiting, Preparing, AwaitingDelivery, ObservingResponse, Approach, ExecutionQuiet, Uncertain, WaitingWindow };
struct TaskProgress {
    TaskPhase phase=TaskPhase::Off;
    uint64_t source{},route{},event{},epoch{};
    double startedAt=-1,leadSeconds=-1,responseSince=-1;
    bool planningDelivered{},approachDelivered{},speedReductionObserved{};
    unsigned opportunities{},planningCompletions{},approachCompletions{},observedReductions{},unassessedOutcomes{};
};
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
    static bool evidence(const Candidate&,const WorldState&,double) noexcept;
    const TaskProgress& progress() const noexcept {return state_;}
    std::string json() const;
private:
    bool enabled_{};
    TaskProgress state_{};
};
}
