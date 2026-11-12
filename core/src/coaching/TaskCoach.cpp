#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/TaskCoach.hpp"
#include "learndrive/SceneState.hpp"
#include <cmath>
#include <sstream>
namespace ld::coaching {
namespace {
double lead(const WorldState& w,double now) noexcept {
    if(w.route.provider) {
        // Google's own distance (dead-reckoned) and the measured speed. Neither the camera nor
        // our GPS fix quality decides whether a Google turn is coached.
        if(!w.active||!w.route.valid||!w.route.routeId||!w.route.eventId||w.route.feature<3||w.route.feature>5||
           !cognition::fresh(now,w.route.captureTime,routeProviderAge)||!cognition::fresh(now,w.telemetry.time,.15)||
           !w.telemetry.speedValid||!std::isfinite(w.telemetry.speedKmh)||w.telemetry.speedKmh<0||w.telemetry.speedKmh>300||
           !std::isfinite(w.route.distanceM)||w.route.distanceM<0)return -1;
        return w.route.distanceM/std::max(w.telemetry.speedKmh/3.6,1.);
    }
    if(!w.active||!w.route.valid||!w.route.routeId||!w.route.eventId||w.route.feature<3||w.route.feature>5||
       !cognition::fresh(now,w.route.captureTime,w.route.provider?3.:2.5)||!cognition::fresh(now,w.route.gpsTime,2.5)||
       !cognition::fresh(now,w.captureTime)||!cognition::fresh(now,w.telemetry.time,.15)||!w.telemetry.speedValid||!std::isfinite(w.telemetry.speedKmh)||w.telemetry.speedKmh<=3||w.telemetry.speedKmh>300||
       !std::isfinite(w.route.distanceM)||w.route.distanceM<0)return -1;
    double seconds=w.route.distanceM/(w.telemetry.speedKmh/3.6);
    if(w.route.provider){if(!std::isfinite(w.route.etaSeconds)||w.route.etaSeconds<0)return -1;seconds=std::min(seconds,w.route.etaSeconds);}
    return seconds;
}
bool delivered(const Event& e) noexcept {return e.visualOnly||e.deliveryStage==DeliveryStage::Completed;}
// Interrupted, but only after the part that matters had been spoken.
bool saidEnough(const Event& e) noexcept {
    return e.deliveryStage==DeliveryStage::Interrupted&&std::isfinite(e.startedAt)&&e.startedAt>=0&&e.endedAt-e.startedAt>=saidEnoughSeconds;
}
const char* phase(TaskPhase p) {
    switch(p){case TaskPhase::Off:return "OFF";case TaskPhase::Waiting:return "WAITING_FOR_ROUTE";case TaskPhase::Preparing:return "PREPARING";
    case TaskPhase::AwaitingDelivery:return "AWAITING_DELIVERY";case TaskPhase::ObservingResponse:return "OBSERVING_RESPONSE";
    case TaskPhase::Approach:return "APPROACH_PREPARATION";case TaskPhase::ExecutionQuiet:return "EXECUTION_QUIET";
    case TaskPhase::Uncertain:return "EVIDENCE_UNAVAILABLE";case TaskPhase::Entry:return "JUNCTION_ENTRY";case TaskPhase::WaitingWindow:return "WAITING_FOR_APPROACH";default:return "OUTCOME_UNASSESSED";}
}
}
void TaskCoach::enable(bool value) noexcept {if(value==enabled_)return;enabled_=value;state_={};state_.phase=value?TaskPhase::Waiting:TaskPhase::Off;}
bool TaskCoach::reserves(double block) const noexcept {
    if(!scheduling_||!enabled_||!std::isfinite(block)||block<=0||state_.leadSeconds<0||!state_.event)return false;
    if(state_.byDistance) {
        // Metres covered while the other speech plays: keep room for a step still to come.
        const double mps=std::max(state_.speedMps,1.),d=state_.distanceM,after=d-block*mps;
        if(!state_.planningDelivered&&d>state_.entryM)return after<state_.entryM+mps*minimumUsefulWindow;
        if(!state_.entryDelivered&&d>state_.entryM)return after<state_.entryM*.5;
        return false;
    }
    const double lead=state_.leadSeconds,after=lead-block;
    // Preparation not yet given and its act-by window still ahead.
    if(!state_.planningDelivered&&state_.phase==TaskPhase::WaitingWindow)return after<actByLateLimit+minimumUsefulWindow;
    // "Brake, change down" still to come at speed.
    if(state_.planningDelivered&&!state_.approachDelivered&&state_.fast&&lead>actByApproachOpen)return after<actByApproachClose+minimumUsefulWindow;
    // The junction-entry instruction still to come.
    if(!state_.entryDelivered&&lead>actByEntryOpen)return after<actByEntryClose+minimumUsefulWindow;
    return false;
}
void TaskCoach::sourceChanged() noexcept {
    if(state_.event) {
        ++state_.unassessedOutcomes;
        // Remember what was said about this turn (a step cut off by the restart counts once its
        // key words were out): if the same Google turn comes back, it is not coached again.
        const auto heard=[&](int step){return state_.flightStep==step&&state_.flightHeard>=saidEnoughSeconds;};
        state_.said={state_.route,state_.event,state_.epoch,state_.planningDelivered||heard(1),state_.approachDelivered||heard(2),state_.entryDelivered||heard(3)};
        state_.event=0;
    }
    state_.phase=enabled_?TaskPhase::Waiting:TaskPhase::Off;state_.planningDelivered=state_.approachDelivered=state_.speedReductionObserved=state_.entryDelivered=false;
    state_.flightStep=0;state_.flightHeard=0;
}
double TaskCoach::leadSeconds(const WorldState& w,double now) noexcept {return lead(w,now);}
bool TaskCoach::evidence(const Candidate& c,const WorldState& w,double now,bool started) noexcept {
    double seconds=lead(w,now);
    // Started speech: slowing to a stop at this junction is the expected response,
    // not lost evidence. Same fresh route/GPS/camera, measured speed at or below 3 km/h.
    if(started&&seconds<0&&w.active&&w.route.valid&&w.route.feature>=3&&w.route.feature<=5&&
       cognition::fresh(now,w.route.captureTime,w.route.provider?routeProviderAge:2.5)&&
       (w.route.provider||(cognition::fresh(now,w.route.gpsTime,2.5)&&cognition::fresh(now,w.captureTime)))&&
       cognition::fresh(now,w.telemetry.time,.15)&&w.telemetry.speedValid&&
       std::isfinite(w.telemetry.speedKmh)&&w.telemetry.speedKmh>=0&&w.telemetry.speedKmh<=3&&!w.telemetry.shock&&
       c.origin==5&&c.kind==EventKind::Route&&c.lessonEpoch==w.route.epoch&&(c.lessonStep==2||c.lessonStep==3))return true;
    if(w.route.held&&!started)return false;
    // Braking to approach the junction is the expected response, not missing evidence.
    if(seconds<0||w.telemetry.hardBraking||w.telemetry.shock||c.origin!=5||c.kind!=EventKind::Route||c.urgent||c.critical||c.lessonEpoch!=w.route.epoch)return false;
    if(w.route.provider) {
        // Google route steps by distance (see observe()); the timed windows below still apply.
        const double v=w.telemetry.speedKmh,d=w.route.distanceM,entry=routeEntryDistance(v);
        const Prompt expected=w.route.feature==4?Prompt::junctionPlanLeft:w.route.feature==5?Prompt::junctionPlanRight:Prompt::junctionPlan;
        const bool approachPrompt=c.prompt==Prompt::junctionApproach||c.prompt==Prompt::junctionGearDown||c.prompt==Prompt::junctionBrakeGearDown;
        if(c.lessonStep==1&&c.prompt==expected&&(started||(d<=routePrepareBand(v)+20&&d>entry*.7)))return true;
        if(c.lessonStep==3&&c.prompt==Prompt::junctionEntry&&(started||(d<=entry+6&&d>=1.5&&v<=routeEntrySpeed)))return true;
        if(c.lessonStep==2&&approachPrompt&&(started||(d<=routeApproachDistance(v)+10&&d>entry*.7)))return true;
    }
    // Either schedule's window (the fixed 0.7.1 windows or act-by timing) admits a step;
    // which one proposes it is decided by the scheduling mode in observe().
    const double v=w.telemetry.speedKmh,prepare=actByPrepareLead(v);
    if(c.lessonStep==1){const Prompt expected=w.route.feature==4?Prompt::junctionPlanLeft:w.route.feature==5?Prompt::junctionPlanRight:Prompt::junctionPlan;
        // Started speech has no upper bound: slowing raises distance/speed lead.
        const bool fixed=seconds>=(started?executionQuietLead:planningWindowClose)&&(started||seconds<=planningWindowOpen);
        const bool actBy=seconds>=(started?actByEntryClose:actByLateLimit)&&(started||seconds<=prepare+1);
        return c.prompt==expected&&(fixed||actBy);}
    // The 3.8 s recorded approach cue needs room to finish before the 8 s
    // quiet boundary. These are conservative experimental timing windows.
    if(c.lessonStep==3)return c.prompt==Prompt::junctionEntry&&seconds>=(started?0:std::min(1.5,actByEntryClose))&&
        seconds<std::max(executionQuietLead,actByEntryOpen)+(started?2:0)&&(started||v<=actByApproachSpeed);
    const bool approachPrompt=c.prompt==Prompt::junctionApproach||c.prompt==Prompt::junctionGearDown||
        (c.prompt==Prompt::junctionBrakeGearDown&&(started||v>20));
    const bool fixed=seconds>=(started?executionQuietLead:approachWindowClose)&&(started||seconds<=approachWindowOpen);
    const bool actBy=seconds>=(started?actByEntryClose:actByApproachClose)&&(started||seconds<=actByApproachOpen);
    return c.lessonStep==2&&approachPrompt&&(fixed||actBy);
}
// What the ledger says about the current turn's steps: said (finished, shown, or cut off only after
// its key words: never started over then), still being spoken, or neither.
void TaskCoach::absorb(const EventLedger& ledger,double now,bool& submitted,bool& entrySubmitted) noexcept {
    state_.flightStep=0;state_.flightHeard=0;
    for(const auto& e:ledger.entries())if(e.used&&e.sourceId==state_.source&&e.identity.space==IdentitySpace::Route&&e.identity.id==state_.event&&
        e.identity.scope==state_.route&&e.lessonEpoch==state_.epoch&&e.acceptedAt>=state_.startedAt&&e.lessonStep){
        const bool said=delivered(e)||saidEnough(e);
        if(said&&e.lessonStep==1&&!state_.planningDelivered){state_.planningDelivered=true;state_.responseSince=now;++state_.planningCompletions;}
        if(said&&e.lessonStep==2&&!state_.approachDelivered){state_.approachDelivered=true;++state_.approachCompletions;}
        if(said&&e.lessonStep==3&&!state_.entryDelivered){state_.entryDelivered=true;++state_.entryCompletions;}
        if(e.deliveryStage==DeliveryStage::Started&&std::isfinite(e.startedAt)&&e.startedAt>=0){state_.flightStep=e.lessonStep;state_.flightHeard=now-e.startedAt;}
        submitted|=e.deliveryStage==DeliveryStage::Submitted||e.deliveryStage==DeliveryStage::Started;
        entrySubmitted|=e.lessonStep==3&&(e.deliveryStage==DeliveryStage::Submitted||e.deliveryStage==DeliveryStage::Started);
    }
}
void TaskCoach::settle(const EventLedger& ledger,double now) noexcept {
    if(!enabled_||!state_.event||!std::isfinite(now))return;
    bool submitted=false,entrySubmitted=false;absorb(ledger,now,submitted,entrySubmitted);
}
void TaskCoach::observe(const WorldState& w,const ShortMemory& memory,const EventLedger& ledger) noexcept {
    if(!enabled_)return;
    if(!w.active){state_.quietUntil=-1;state_.quietEvent=0;sourceChanged();return;}
    // Route loss keeps any bounded execution hold (see TaskProgress::quietUntil).
    if(!w.route.routeId||!w.route.eventId){state_.leadSeconds=-1;sourceChanged();return;}
    // Fresh navigation has moved on to a different event: the held junction ended.
    if(state_.quietEvent&&w.route.valid&&w.route.eventId!=state_.quietEvent&&cognition::fresh(w.now,w.route.captureTime,w.route.provider?3.:2.5)){
        state_.quietUntil=-1;state_.quietEvent=0;}
    const double seconds=lead(w,w.now);state_.leadSeconds=seconds;
    const bool different=state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch;
    if(scheduling_&&seconds>=0) {
        if(seconds<executionQuietLead){if(state_.quietUntil<=w.now||state_.quietEvent!=w.route.eventId)++state_.quietHolds;
            state_.quietUntil=w.now+executionQuietHold;state_.quietEvent=w.route.eventId;}
        else {state_.quietUntil=-1;state_.quietEvent=0;} // Measured lead is clear of the execution window again.
    }
    if(seconds<0||w.telemetry.hardBraking||w.telemetry.shock){state_.phase=TaskPhase::Uncertain;return;}
    if(different){
        // The same Google turn after a camera restart (only the source changed) or after a moment
        // without readings: Google's turn does not depend on the camera, so what was already said
        // about it stands. (A hot phone's camera restarting mid-junction started the turn's
        // instructions over.) Junctions the app finds itself keep starting afresh: the same
        // junction can be driven twice.
        const bool held=state_.event&&state_.route==w.route.routeId&&state_.event==w.route.eventId&&state_.epoch==w.route.epoch;
        const bool back=!state_.event&&state_.said.event&&state_.said.route==w.route.routeId&&state_.said.event==w.route.eventId&&state_.said.epoch==w.route.epoch;
        const bool same=w.route.provider&&(held||back);
        const auto heard=[&](int step){return state_.flightStep==step&&state_.flightHeard>=saidEnoughSeconds;};
        const bool planning=same&&(held?state_.planningDelivered||heard(1):state_.said.planning);
        const bool approach=same&&(held?state_.approachDelivered||heard(2):state_.said.approach);
        const bool entry=same&&(held?state_.entryDelivered||heard(3):state_.said.entry);
        if(state_.event&&!same)++state_.unassessedOutcomes;
        if(same&&back&&state_.unassessedOutcomes)--state_.unassessedOutcomes;
        state_.source=w.sourceId;state_.route=w.route.routeId;state_.event=w.route.eventId;state_.epoch=w.route.epoch;
        state_.startedAt=w.now;state_.responseSince=planning?w.now:-1;state_.speedReductionObserved=false;
        state_.planningDelivered=planning;state_.approachDelivered=approach;state_.entryDelivered=entry;
        if(!same)++state_.opportunities;
        state_.said={};
    }
    bool submitted=false,entrySubmitted=false;
    absorb(ledger,w.now,submitted,entrySubmitted);
    if(state_.planningDelivered&&memory.speedTrend(w.now,state_.responseSince)==SpeedTrend::Decreasing&&!state_.speedReductionObserved){state_.speedReductionObserved=true;++state_.observedReductions;}
    state_.byDistance=false;
    if(scheduling_&&w.route.provider) {
        // Google route: steps by Google's distance. The preparation stays due from its spoken
        // distance until the turn (said late rather than lost); "brake, change down" at about
        // 50 m at speed; the turn itself about 3 s before it.
        const double v=std::max(0.,w.telemetry.speedKmh),d=w.route.distanceM,mps=std::max(v/3.6,1.);
        const double band=routePrepareBand(v),entry=routeEntryDistance(v),approach=routeApproachDistance(v);
        state_.byDistance=true;state_.distanceM=d;state_.speedMps=mps;state_.bandM=band;state_.entryM=entry;
        state_.prepareLead=(band+12)/mps;state_.fast=v>routeApproachSpeed;
        if(submitted){state_.phase=TaskPhase::AwaitingDelivery;return;}
        if(d<=entry){state_.phase=!state_.entryDelivered&&d>=2&&v<=routeEntrySpeed?TaskPhase::Entry:TaskPhase::ExecutionQuiet;return;}
        if(!state_.planningDelivered){state_.phase=d>band+12?TaskPhase::WaitingWindow:TaskPhase::Preparing;return;}
        state_.phase=!state_.approachDelivered&&state_.fast&&d<=approach?TaskPhase::Approach:TaskPhase::ObservingResponse;
        return;
    }
    if(scheduling_) {
        // Act-by schedule: each step when the learner must act at THIS speed, and
        // a later step still comes if an earlier one was missed.
        const double v=w.telemetry.speedKmh;state_.prepareLead=actByPrepareLead(v);state_.fast=v>actByApproachSpeed;
        if(submitted){state_.phase=TaskPhase::AwaitingDelivery;return;}
        if(seconds<actByEntryOpen){state_.phase=!state_.entryDelivered&&seconds>=actByEntryClose&&v<=actByApproachSpeed?TaskPhase::Entry:TaskPhase::ExecutionQuiet;return;}
        if(!state_.planningDelivered){state_.phase=seconds>state_.prepareLead+1?TaskPhase::WaitingWindow:seconds>=actByLateLimit?TaskPhase::Preparing:TaskPhase::ExecutionQuiet;return;}
        state_.phase=!state_.approachDelivered&&state_.fast&&seconds>=actByApproachClose&&seconds<=actByApproachOpen?TaskPhase::Approach:TaskPhase::ObservingResponse;
        return;
    }
    (void)entrySubmitted;
    if(seconds<8){
        // One short control instruction at the junction itself (instructor-style), then quiet.
        state_.phase=scheduling_&&!submitted&&state_.planningDelivered&&!state_.entryDelivered&&seconds>=1.5&&w.telemetry.speedKmh<=20?
            TaskPhase::Entry:TaskPhase::ExecutionQuiet;return;}
    if(submitted){state_.phase=TaskPhase::AwaitingDelivery;return;}
    if(!state_.planningDelivered)state_.phase=seconds>40?TaskPhase::WaitingWindow:seconds>=22?TaskPhase::Preparing:TaskPhase::ExecutionQuiet;
    else if(seconds>=13&&seconds<=18&&!state_.approachDelivered&&(!state_.speedReductionObserved||scheduling_))state_.phase=TaskPhase::Approach;
    else state_.phase=TaskPhase::ObservingResponse;
}
Candidate TaskCoach::proposal(const WorldState& w) const noexcept {
    Candidate c;
    if(state_.phase==TaskPhase::Preparing){c.lessonStep=1;c.prompt=w.route.feature==4?Prompt::junctionPlanLeft:w.route.feature==5?Prompt::junctionPlanRight:Prompt::junctionPlan;}
    else if(state_.phase==TaskPhase::Approach){c.lessonStep=2;
        // Adapt rather than repeat: already slowing (or slow) only needs the gear and brake cover.
        c.prompt=!scheduling_?Prompt::junctionApproach:state_.speedReductionObserved||w.telemetry.speedKmh<=20?Prompt::junctionGearDown:Prompt::junctionBrakeGearDown;}
    else if(state_.phase==TaskPhase::Entry){c.lessonStep=3;c.prompt=Prompt::junctionEntry;}
    else return c;
    c.origin=5;c.kind=EventKind::Route;c.identity={IdentitySpace::Route,w.route.eventId,w.route.routeId};c.supported=true;c.lessonEpoch=w.route.epoch;
    c.captureTime=w.route.captureTime;c.maxAge=w.route.provider?3:2.5;c.expiresAt=c.captureTime+c.maxAge;
    if(w.route.held||!evidence(c,w,w.now))return {}; // Nothing new starts on a turn carried through a feed blink.
    return c;
}
bool TaskCoach::valid(const Candidate& c,const WorldState& w,double now,const EventLedger& ledger) const noexcept {
    if(!c.lessonStep){const double seconds=lead(w,now);
        // A developing road-user conflict is not routine: it may speak during junction execution.
        const bool conflict=c.origin==7&&priority(c.prompt)>=4;
        if(!c.urgent&&!conflict&&quietHeld(now))return false; // Routine silence persists through evidence loss.
        if(conflict)return true;
        return !isLessonPrompt(c.prompt)&&(!enabled_||c.urgent||seconds<0||seconds>=executionQuietLead);}
    if(!enabled_||state_.phase==TaskPhase::Uncertain||state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch||!evidence(c,w,now))return false;
    if(c.lessonStep==1)return !state_.planningDelivered;
    if(c.lessonStep==3)return scheduling_&&!state_.entryDelivered; // Still said if the preparation was missed.
    if(!state_.planningDelivered||state_.approachDelivered)return false;
    if(state_.speedReductionObserved&&!(scheduling_&&c.prompt==Prompt::junctionGearDown))return false;
    // The preparation was said (see observe: finished, said enough, or carried over for the same turn).
    return true;
}
bool TaskCoach::continuing(const Candidate& c,const WorldState& w,double now,const EventLedger& ledger) const noexcept {
    if(!c.lessonStep||!scheduling_)return valid(c,w,now,ledger);
    if(!enabled_||state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch||!evidence(c,w,now,true))return false;
    if(c.lessonStep==1)return !state_.planningDelivered;
    if(c.lessonStep==3)return !state_.entryDelivered;
    return state_.planningDelivered&&!state_.approachDelivered;
}
std::string TaskCoach::json() const {
    std::ostringstream o;o<<"{\"lesson\":\"junction_preparation\",\"enabled\":"<<(enabled_?"true":"false")<<",\"phase\":\""<<phase(state_.phase)<<"\",\"opportunities\":"<<state_.opportunities
    <<",\"planning_deliveries\":"<<state_.planningCompletions<<",\"approach_deliveries\":"<<state_.approachCompletions<<",\"observed_speed_reductions\":"<<state_.observedReductions
    <<",\"unassessed_outcomes\":"<<state_.unassessedOutcomes<<",\"execution_quiet_holds\":"<<state_.quietHolds<<",\"entry_deliveries\":"<<state_.entryCompletions<<",\"mirror_check\":\"UNOBSERVED\",\"signal\":\"UNOBSERVED\",\"position\":\"UNOBSERVED\",\"controls\":\"UNOBSERVED\",\"driving_outcome\":\"UNASSESSED\"}";return o.str();
}
}
