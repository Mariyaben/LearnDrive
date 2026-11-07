#pragma once
#include <algorithm>
#include <cmath>
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
    // Google route steps are placed by Google's distance (dead-reckoned between updates).
    bool byDistance{};double distanceM=-1,speedMps{},bandM{},entryM{};
    uint64_t quietEvent{};
    unsigned quietHolds{};
    // Act-by schedule at the current speed: preparation lead (s) and whether a
    // separate "brake, change down" step is needed (faster than actByApproachSpeed).
    double prepareLead=-1;
    bool fast{};
};
// Experimental timing constants of the task schedule (seconds of lead time).
inline constexpr double planningWindowOpen=40,planningWindowClose=22,approachWindowOpen=18,approachWindowClose=13;
inline constexpr double executionQuietLead=8,executionQuietHold=10,minimumUsefulWindow=3,episodeStepSpacing=8;
// Act-by timing (task scheduling on): an instructor speaks when the learner has to
// start acting, not at a fixed time. Preparing for a junction (look, signal, slow
// down) needs the time to brake from the current speed to ~15 km/h at a gentle
// 1.2 m/s^2 plus ~10 s to check, signal and hear the sentence: ~10 s on a slow lane
// (the instructor-labelled 2026-11-05 drive: 4-16 s), 18 s at 50 km/h, at most 25 s.
// "Clutch down, look both ways" comes 2-6 s before the junction itself. A separate
// "brake gently, change down" step is only needed above 25 km/h.
inline constexpr double actByLateLimit=6.5,actByEntryOpen=6,actByEntryClose=2,actByApproachOpen=10.5,actByApproachClose=6.5,
    actByApproachSpeed=25,actByStepSpacing=4.5;
inline double actByPrepareLead(double kmh) noexcept {return std::clamp(10+std::max(0.,(kmh-15)/3.6)/1.2,10.,25.);}
// Google route steps by distance, as an instructor (and Google) says them: the preparation at
// a spoken distance about 12 s ahead (50, 100, 150 or 200 m), "brake, change down" at about
// 50 m when faster than 12 km/h, and the turn itself about 3 s (at least 10 m) before it.
inline double routePrepareBand(double kmh) noexcept {
    const double want=std::max(50.,std::max(0.,kmh)/3.6*12);double best=50;
    for(double b:{100.,150.,200.})if(std::abs(b-want)<std::abs(best-want))best=b;
    return best;
}
inline double routeEntryDistance(double kmh) noexcept {return std::max(10.,std::max(0.,kmh)/3.6*3);}
inline double routeApproachDistance(double kmh) noexcept {return routePrepareBand(kmh)>50?62:std::max(20.,std::max(0.,kmh)/3.6*5);}
inline constexpr double routeApproachSpeed=12,routeEntrySpeed=30,routeProviderAge=5;
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
