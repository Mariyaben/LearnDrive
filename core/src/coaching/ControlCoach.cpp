#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/ControlCoach.hpp"
#include "learndrive/SceneState.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace ld::coaching {
namespace {
bool speedOk(const WorldState& w,double now) noexcept {
    return cognition::fresh(now,w.telemetry.time,.15)&&w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh)&&
        w.telemetry.speedKmh>=0&&w.telemetry.speedKmh<=300;
}
int bandFor(const GearBands& b,double v) noexcept {
    int g=1;for(int k=2;k<=5;++k)if(v>b.up[size_t(k)])g=k;return g;
}
Prompt upPrompt(int g) noexcept {
    return g==2?Prompt::gearUpSecond:g==3?Prompt::gearUpThird:g==4?Prompt::gearUpFourth:g==5?Prompt::gearUpFifth:Prompt::none;
}
const GearBands defaultBands{};
}
void ControlCoach::reset() noexcept {
    gear_=candidateGear_=slopeSign_=0;candidateSince_=stoppedSince_=slopeClearSince_=exitAt_=driftLeftSince_=driftRightSince_=lastLaneCue_=lastBendCue_=-1;stopCued_=moveOffCued_=lastBendRight_=false;
    hillAt_=-1;hillSign_=0;hillSteep_=false;
    lastQuietEvent_=lastBump_=lastBend_=lastSlope_=0;active_={};
}
void ControlCoach::propose(int prompt,double now,double seconds) noexcept {
    if(prompt<=0||prompt>promptCount)return;
    // Replace an expired slot, else the one expiring first. A new instruction
    // supersedes any active instruction of the same prompt.
    Active* slot=nullptr;
    for(auto& a:active_)if(a.prompt==prompt||a.until<=now){slot=&a;break;}
    if(!slot)slot=&*std::min_element(active_.begin(),active_.end(),[](const Active& a,const Active& b){return a.until<b.until;});
    *slot={prompt,next_++,now+seconds};
    if(size_t(prompt)<counts_.size())++counts_[size_t(prompt)];
}
void ControlCoach::observe(const WorldState& w,const ShortMemory& memory,const TaskCoach& task,int support) noexcept {
    if(!enabled_)return;
    if(!w.active){reset();return;}
    const double now=w.now;
    for(auto& a:active_)if(a.until<=now)a={};
    if(!speedOk(w,now))return; // Unknown speed: no control instruction and no state change.
    const double v=w.telemetry.speedKmh;
    const auto recent=memory.speedTrend(now,now-3);
    const auto& j=task.progress();
    const bool junctionLesson=j.event&&j.leadSeconds>=0&&j.leadSeconds<=planningWindowOpen;
    const bool quiet=task.quietHeld(now)||(j.phase==TaskPhase::ExecutionQuiet&&j.leadSeconds>=0);
    // After a turn: navigation moved on from the held junction while moving.
    // Navigation advances at the maneuver point (mid-turn); the instructor's
    // "straighten up" comes as the turn completes, shortly afterwards.
    // ...only for a turn actually made: about 30 degrees of heading change measured from the
    // junction until the cue is due (navigation advances mid-turn), or with no turn rate, Google
    // not rerouting, which is what a missed turn looks like (Nov 6: "Straighten the wheel" after
    // the learner went straight past the turn).
    if(j.quietEvent||exitAt_>=0) {
        if(w.yawKnown&&lastYawAt_>=0&&now>lastYawAt_)turned_+=w.yawRate*std::min(.2,now-lastYawAt_);
        turnMeasured_|=w.yawKnown;reroutedInTurn_|=w.rerouting;
    }
    lastYawAt_=w.yawKnown?now:-1;
    if(lastQuietEvent_&&!j.quietEvent&&w.route.valid&&w.route.eventId!=lastQuietEvent_&&support>=1)exitAt_=now+2.5;
    lastQuietEvent_=j.quietEvent;
    if(exitAt_>=0&&now>=exitAt_) {
        exitAt_=-1;
        const bool turnMade=turnMeasured_?std::abs(turned_)>=.5:!reroutedInTurn_&&!w.rerouting;
        if(turnMade&&v>3&&v<30)propose(int(Prompt::turnExit),now,5);
        turned_=0;turnMeasured_=reroutedInTurn_=false;
    } else if(!j.quietEvent&&exitAt_<0){turned_=0;turnMeasured_=reroutedInTurn_=false;}
    // Google found a new route while driving: say so once, calmly (the next turn comes as usual).
    // Only once the new route is there and its next turn is a different one: Google's feed also
    // blinks to "rerouting" when it merely refreshes the same route. The fact stands for a few
    // seconds, so the sentence can wait its turn (on Nov 8 the blink was over before it could be said).
    if(!w.routeChanged)routeChangeCued_=false;
    else if(!routeChangeCued_&&v>5&&support>=1){routeChangeCued_=true;propose(int(Prompt::routeChanged),now,6);}
    // Stopping and moving off.
    if(v<=2){if(stoppedSince_<0)stoppedSince_=now;}
    else stoppedSince_=-1;
    if(v>20){stopCued_=false;moveOffCued_=false;}
    if(!stopCued_&&v>3&&v<12&&recent==SpeedTrend::Decreasing&&!quiet&&support>=1){stopCued_=true;propose(int(Prompt::stopClutch),now,3);}
    // Stopped at the destination: the drive is over, so no moving-off preparation.
    if(stoppedSince_>=0&&now-stoppedSince_>=4&&!moveOffCued_&&support>=2&&!w.arriving){moveOffCued_=true;propose(int(Prompt::moveOffPrep),now,6);}
    if(v<=3)gear_=1;
    // A mapped bend or bump within about 10 s: an instructor keeps the learner in gear
    // (no "change up") until past it, rather than accelerating into it.
    const double mapMps=std::max(v,5.)/3.6;
    const bool mapHazardNear=cognition::fresh(now,w.mapTime,2.5)&&
        ((w.bendId&&w.bendAheadM/mapMps<=10)||(w.bumpId&&w.bumpAheadM/mapMps<=10));
    // Gear changes for the measured speed (hysteresis + 0.6 s persistence).
    if(!gear_)gear_=bandFor(bands_,v);
    int target=gear_;
    if(gear_<5&&v>bands_.up[size_t(gear_+1)]&&recent!=SpeedTrend::Decreasing&&!mapHazardNear)target=gear_+1;
    else if(gear_>2&&v<bands_.down[size_t(gear_)]&&recent==SpeedTrend::Decreasing)target=gear_-1;
    if(target!=gear_) {
        if(candidateGear_!=target){candidateGear_=target;candidateSince_=now;}
        else if(now-candidateSince_>=.6) {
            const bool up=target>gear_;gear_=target;candidateGear_=0;
            // Junction lessons and execution quiet own the speech near a turn.
            if(!quiet&&!(junctionLesson&&!up)) {
                if(up&&(support>=2||(support==1&&target<=3)))propose(int(upPrompt(target)),now,4);
                if(!up&&target==3&&support>=2)propose(int(Prompt::gearDownThird),now,4);
                if(!up&&target==2&&support>=1)propose(int(Prompt::gearDownSecond),now,4);
            }
        }
    } else candidateGear_=0;
    // Lane keeping: only with lane lines consistently seen (marked roads), sustained
    // drift (>28% of the lane width for 1.5 s), away from junctions, at most every 20 s.
    // The centre of the lane between the left edge and the centre line is where to be.
    const bool laneFresh=w.laneValid&&cognition::fresh(now,w.laneTime,.6)&&w.laneRight-w.laneLeft>.15&&w.laneRight-w.laneLeft<.95;
    const bool nearJunction=j.event&&j.leadSeconds>=0&&j.leadSeconds<10;
    if(laneFresh&&v>15&&!nearJunction) {
        const double offset=(.5-(w.laneLeft+w.laneRight)*.5)/(w.laneRight-w.laneLeft); // + = car right of lane centre
        if(offset>.28){if(driftRightSince_<0)driftRightSince_=now;}else driftRightSince_=-1;
        if(offset<-.28){if(driftLeftSince_<0)driftLeftSince_=now;}else driftLeftSince_=-1;
        const bool due=lastLaneCue_<0||now-lastLaneCue_>=20;
        if(due&&driftRightSince_>=0&&now-driftRightSince_>=1.5){lastLaneCue_=now;driftRightSince_=-1;propose(int(Prompt::driftRightKeepLeft),now,3);}
        if(due&&driftLeftSince_>=0&&now-driftLeftSince_>=1.5){lastLaneCue_=now;driftLeftSince_=-1;propose(int(Prompt::driftLeftCentre),now,3);}
    } else driftLeftSince_=driftRightSince_=-1;
    // The road ahead from the map: an instructor names a speed bump and a sharp (blind)
    // bend about 7 s before it (time to slow, change down and sound the horn), once each,
    // and never once the car is already on it. Lead = distance at the measured speed.
    if(cognition::fresh(now,w.mapTime,2.5)&&!quiet) {
        const double mps=mapMps;
        if(w.bumpId&&w.bumpId!=lastBump_&&std::isfinite(w.bumpAheadM)&&v>5&&w.bumpAheadM>=5&&(w.bumpAheadM/mps<=7||w.bumpAheadM<=30)){
            lastBump_=w.bumpId;propose(int(Prompt::bumpAhead),now,4);}
        if(w.hornRules&&w.bendId&&w.bendId!=lastBend_&&std::isfinite(w.bendAheadM)&&v>8&&!nearJunction&&
           w.bendAheadM>=10&&w.bendAheadM/mps>=1.5&&(w.bendAheadM/mps<=7||w.bendAheadM<=30)){
            lastBend_=w.bendId;
            // The map's bend point moves as the car nears it; a same-direction bend named
            // under 8 s ago is the same bend (or one continuous curve), not a new lesson.
            if(!(lastBendCue_>=0&&now-lastBendCue_<8&&w.bendRight==lastBendRight_)){
                lastBendCue_=now;lastBendRight_=w.bendRight;propose(int(Prompt::bendHorn),now,4);}
        }
    }
    // Hills from the map (elevation along the route): name the slope and what to do about 7 s
    // before it starts (9 s when the learner must slow down first; at least 40 m), so the gear is
    // changed before the hill, not on it. First seen already on it: said once if enough is left.
    // The next stretch of a climb or descent soon after the last (a ghat road between hairpins) is
    // the same hill: the learner is already in its gear. Named again only if steeper, or after
    // 45 s without one.
    const bool mapHills=w.mapGradeKnown||w.slopeId;
    const bool mapFresh=w.slopeId&&cognition::fresh(now,w.mapTime,2.5);
    const int hillSign=w.slopeGrade<0?-1:1;const bool hillSteep=std::abs(w.slopeGrade)>=10;
    if(mapFresh&&w.slopeId!=lastSlope_&&v>8&&!quiet) {
        const auto gear=hillGear(bands_,w.slopeGrade,v);
        if(w.slopeAheadM<=std::max(40.,(gear.brakeFirst?9:7)*v/3.6)) {
            lastSlope_=w.slopeId;slopeSign_=hillSign;
            const bool sameHill=hillSign==hillSign_&&hillAt_>=0&&now-hillAt_<45&&(!hillSteep||hillSteep_);
            if(!sameHill&&(w.slopeAheadM>0||w.slopeLengthM>=60))
                propose(int(hillSign<0?(hillSteep?Prompt::mapSteepDownhill:Prompt::mapDownhill):(hillSteep?Prompt::mapSteepUphill:Prompt::mapUphill)),now,6);
            if(!sameHill){hillSign_=hillSign;hillSteep_=hillSteep;}
            hillAt_=now;
        }
    }
    // On the hill named (or passed over as the same hill): it carries on while the car climbs or descends it.
    if(mapFresh&&w.slopeId==lastSlope_&&w.slopeAheadM<=0&&hillSign==hillSign_){hillAt_=now;hillSteep_|=hillSteep;}
    // Hills from the phone's tilt: only where the route has no elevation (a sagging or fallen mount
    // misleads it).
    const bool pitch=!mapHills&&w.telemetry.calibrated&&w.telemetry.pitchValid&&std::isfinite(w.telemetry.pitchDegrees);
    const int band=pitch?w.gradeBand:0;const int sign=band>=2?1:band<=-2?-1:0;
    if(!pitch||std::abs(band)<1){if(slopeClearSince_<0)slopeClearSince_=now;if(now-slopeClearSince_>=5)slopeSign_=0;}
    else slopeClearSince_=-1;
    if(sign&&sign!=slopeSign_&&!quiet&&v>8) {
        slopeSign_=sign;
        if(sign<0&&v>12)propose(int(band<=-3?Prompt::downhillSecond:Prompt::downhillThird),now,5);
        if(sign>0&&v<45)propose(int(v<25?Prompt::uphillSecond:Prompt::uphillThird),now,5);
    }
}
size_t ControlCoach::proposals(const WorldState& w,Candidate* out,size_t capacity) const noexcept {
    if(!enabled_||!out)return 0;size_t n=0;
    for(const auto& a:active_)if(a.prompt&&a.until>w.now&&n<capacity) {
        Candidate c;c.prompt=static_cast<Prompt>(a.prompt);c.origin=6;c.kind=EventKind::Status;
        c.identity={IdentitySpace::Control,a.id,uint64_t(a.prompt)};c.captureTime=w.telemetry.time;c.maxAge=maxVisionAgeSeconds;
        c.expiresAt=c.captureTime+c.maxAge;c.confidence=.9;c.supported=true;
        if(evidence(c,w,w.now))out[n++]=c;
    }
    return n;
}
bool ControlCoach::evidence(const Candidate& c,const WorldState& w,double now) noexcept {
    if(c.origin!=6||c.kind!=EventKind::Status||c.identity.space!=IdentitySpace::Control||!isControlPrompt(c.prompt)||c.urgent||c.critical)return false;
    if(!speedOk(w,now))return false;
    const double v=w.telemetry.speedKmh;const auto& b=defaultBands;(void)b;
    switch(c.prompt) {
    case Prompt::moveOffPrep:return v<=3&&!w.arriving;
    case Prompt::stopClutch:return v>2&&v<15;
    case Prompt::turnExit:return v>3&&v<35;
    case Prompt::bumpAhead:return v>3&&cognition::fresh(now,w.mapTime,2.5)&&w.bumpId&&w.bumpAheadM<=80;
    case Prompt::routeChanged:return w.routeChanged;
    case Prompt::bendHorn:return v>5&&w.hornRules&&cognition::fresh(now,w.mapTime,2.5)&&w.bendId&&w.bendAheadM<=80&&w.bendAheadM>=5;
    case Prompt::driftLeftCentre:case Prompt::driftRightKeepLeft:return v>12&&w.laneValid&&cognition::fresh(now,w.laneTime,.6);
    case Prompt::mapDownhill:case Prompt::mapSteepDownhill:case Prompt::mapUphill:case Prompt::mapSteepUphill: {
        // On a fresh route fix, the same way: still ahead (within about 11 s), just begun, or
        // with enough of it left.
        if(v<=8||!cognition::fresh(now,w.mapTime,2.5)||!w.slopeId||!std::isfinite(w.slopeAheadM)||
           w.slopeAheadM>std::max(60.,11*v/3.6)||(w.slopeAheadM<-30&&w.slopeLengthM<60))return false;
        return c.prompt==Prompt::mapDownhill||c.prompt==Prompt::mapSteepDownhill?w.slopeGrade<0:w.slopeGrade>0;
    }
    case Prompt::downhillSecond:case Prompt::downhillThird:case Prompt::uphillSecond:case Prompt::uphillThird: {
        // The phone's measured tilt, only where the route has no elevation.
        if(w.mapGradeKnown||!w.telemetry.calibrated||!w.telemetry.pitchValid||!std::isfinite(w.telemetry.pitchDegrees)||v<=3)return false;
        if(c.prompt==Prompt::downhillSecond)return w.gradeBand<=-3;
        if(c.prompt==Prompt::downhillThird)return w.gradeBand==-2;
        return w.gradeBand>=2;
    }
    default:return v>3; // Gear changes: moving, measured speed.
    }
}
ControlCoach::HillGear ControlCoach::hillGear(const GearBands& b,double grade,double v) noexcept {
    HillGear h;if(!std::isfinite(grade)||!std::isfinite(v)||v<0)return h;
    const int hill=std::abs(grade)>=10?2:3,speed=bandFor(b,v);
    if(speed<=hill)return h;
    if(grade>0){h.gear=speed-hill>=2?-1:hill;return h;}
    h.gear=hill;h.brakeFirst=speed-hill>=2;return h;
}
std::string ControlCoach::json() const {
    std::ostringstream o;o<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"advised_gear_for_speed\":"<<gear_
    <<",\"selected_gear\":\"UNOBSERVED\",\"pedals\":\"UNOBSERVED\",\"proposed\":{";
    bool comma=false;for(size_t i=0;i<counts_.size();++i)if(counts_[i]){o<<(comma?",":"")<<'"'<<i<<"\":"<<counts_[i];comma=true;}
    o<<"}}";return o.str();
}
}
