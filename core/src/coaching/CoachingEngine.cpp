#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/SceneState.hpp"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>

namespace ld::coaching {
namespace {
bool fresh(double now,double at,double age=maxVisionAgeSeconds) noexcept {return cognition::fresh(now,at,age);}
bool sameEvent(const Event& e,uint64_t source,const Candidate& c) noexcept {
    return e.used&&e.sourceId==source&&e.identity==c.identity&&e.kind==c.kind;
}
int severity(const Candidate& c) noexcept {return c.critical?3:c.urgent?2:1;}
bool terminal(EventState s) noexcept {return s==EventState::Resolved||s==EventState::Expired;}
}
void ShortMemory::clear() noexcept {samples_={};cursor_=count_=0;last_=-infinity;source_=0;vehicle_.beginSource(0);}
bool ShortMemory::observe(const WorldState& w) noexcept {
    if(source_!=w.sourceId){clear();source_=w.sourceId;vehicle_.beginSource(source_);}
    vehicle_.observe(w.sourceId,w.telemetry.time,w.telemetry.speedKmh,w.telemetry.speedValid,w.now);
    if(!std::isfinite(w.now)||w.now<0||w.now<last_||w.now-last_<.099)return false;
    MemorySample s;s.time=w.now;s.frameSequence=w.frameSequence;
    s.speedValid=fresh(w.now,w.telemetry.time,.15)&&w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh)&&w.telemetry.speedKmh>=0;
    s.speedKmh=s.speedValid?w.telemetry.speedKmh:0;
    s.pitchValid=fresh(w.now,w.telemetry.time,.15)&&w.telemetry.calibrated&&w.telemetry.pitchValid&&std::isfinite(w.telemetry.pitchDegrees);
    s.pitchDegrees=s.pitchValid?w.telemetry.pitchDegrees:0;
    s.routeValid=w.route.valid;s.routeEvent=w.route.valid?w.route.eventId:0;
    s.telemetryTime=w.telemetry.time;s.accelerationValid=fresh(w.now,w.telemetry.time,.15)&&std::isfinite(w.telemetry.longitudinalAcceleration);
    s.accelerationMps2=s.accelerationValid?w.telemetry.longitudinalAcceleration:0;
    s.roadCaptureTime=w.roadCaptureTime;s.roadValid=w.roadValid&&fresh(w.now,w.roadCaptureTime);s.roadBend=s.roadValid?w.roadBend:0;
    s.routeCaptureTime=w.route.captureTime;s.routeDistanceM=w.route.valid?w.route.distanceM:infinity;s.routeFeature=w.route.valid?w.route.feature:0;
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i) {
        const auto& a=w.actors[i];if(!a.relevant||!fresh(w.now,a.captureTime))continue;
        if(s.actors<s.observations.size())s.observations[s.actors]=a;
        ++s.actors;
        if(a.opticalClosing&&std::isfinite(a.tti)&&a.tti>0)s.closestTti=std::min(s.closestTti,a.tti);
    }
    samples_[cursor_]=s;cursor_=(cursor_+1)%samples_.size();count_=std::min(count_+1,samples_.size());last_=w.now;return true;
}
size_t ShortMemory::size(double now) const noexcept {
    size_t n=0;for(size_t i=0;i<count_;++i)if(fresh(now,samples_[i].time,10.000001))++n;return n;
}
SpeedTrend ShortMemory::speedTrend(double now) const noexcept {
    return static_cast<SpeedTrend>(vehicle_.trend(now));
}
Event& EventLedger::observe(uint64_t source,const Candidate& c,double now) noexcept {
    for(auto& e:events_)if(sameEvent(e,source,c)) {
        if(e.state==EventState::Resolved&&(c.kind==EventKind::Slope||c.kind==EventKind::Bend||c.kind==EventKind::Shift)) {
            e={next_++,source,c.identity,c.kind,EventState::Active,now,now,-infinity,0,true};e.lastPrompt=c.prompt;e.lastCapture=c.captureTime;e.lastConfidence=c.confidence;return e;
        }
        e.lastSeen=now;e.lastPrompt=c.prompt;e.lastCapture=c.captureTime;e.lastConfidence=c.confidence;
        if(terminal(e.state))e.state=e.acceptedAt>=0?EventState::Accepted:EventState::Active;return e;
    }
    Event* slot=nullptr;
    for(auto& e:events_)if(!e.used){slot=&e;break;}
    if(!slot)for(auto& e:events_)if(terminal(e.state)&&(!slot||e.lastSeen<slot->lastSeen))slot=&e;
    if(!slot)slot=&*std::min_element(events_.begin(),events_.end(),[](const Event& a,const Event& b){return a.lastSeen<b.lastSeen;});
    *slot={next_++,source,c.identity,c.kind,EventState::Active,now,now,-infinity,0,true};slot->lastPrompt=c.prompt;slot->lastCapture=c.captureTime;slot->lastConfidence=c.confidence;return *slot;
}
void EventLedger::reconcile(uint64_t source,const WorldState& w) noexcept {
    for(auto& e:events_)if(e.used&&!terminal(e.state)) {
        if(e.sourceId!=source){e.state=EventState::Resolved;continue;}
        bool supported=false;
        for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i)if(sameEvent(e,source,w.candidates[i])&&w.candidates[i].supported){supported=true;break;}
        if(!supported)for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i)
            if(w.actors[i].identity==e.identity&&w.actors[i].relevant&&fresh(w.now,w.actors[i].captureTime)){supported=true;break;}
        if(e.identity.space==IdentitySpace::Route&&(!w.route.valid||w.route.eventId!=e.identity.id||w.route.routeId!=e.identity.scope)) {
            e.state=EventState::Resolved;continue;
        }
        if(supported)e.lastSeen=w.now;
        else if(w.now-e.lastSeen>10)e.state=EventState::Expired;
        else if(w.now-e.lastSeen>=2&&((e.kind==EventKind::Slope&&fresh(w.now,w.telemetry.time,.15)&&w.telemetry.pitchValid&&
                    uint64_t(w.gradeBand+5)!=e.identity.id)||
                (e.kind==EventKind::Bend&&w.roadValid&&fresh(w.now,w.roadCaptureTime)&&uint64_t(w.roadBend+2)!=e.identity.id)||
                (e.kind==EventKind::Shift&&w.engineValid&&fresh(w.now,w.engineCaptureTime,1))||
                ((e.kind==EventKind::Attention||e.kind==EventKind::Approach||e.kind==EventKind::Critical)&&w.frameSequence&&fresh(w.now,w.captureTime))))e.state=EventState::Resolved;
    }
}
void EventLedger::sourceChanged() noexcept {for(auto& e:events_)if(e.used&&!terminal(e.state))e.state=EventState::Resolved;}
size_t EventLedger::activeCount() const noexcept {size_t n=0;for(const auto&e:events_)if(e.used&&!terminal(e.state))++n;return n;}
size_t EventLedger::resolvedCount() const noexcept {size_t n=0;for(const auto&e:events_)if(e.used&&terminal(e.state))++n;return n;}
PolicyChoice BalancedPolicy::choose(const PolicyContext& c) const noexcept {
    size_t chosen=0;int best=-1;
    for(size_t i=0;i<std::min(c.world.candidateCount,c.world.candidates.size());++i) {
        const auto& candidate=c.world.candidates[i];if(!candidate.supported||candidate.prompt==Prompt::none)continue;
        const int rank=(candidate.urgent?100:0)+ld::priority(candidate.prompt);
        if(rank>best){best=rank;chosen=i;}
    }
    if(best<0)return {};
    const auto& selected=c.world.candidates[chosen];
    if(selected.urgent)return {Mode::WARN,chosen};
    const auto& route=c.world.route;
    const bool turn=route.feature==4||route.feature==5;
    const bool imminent=route.valid&&turn&&fresh(c.world.now,route.captureTime,route.provider?3.:2.5)&&fresh(c.world.now,route.gpsTime,2.5)&&
        ((std::isfinite(route.distanceM)&&route.distanceM>=0&&route.distanceM<=20)||(std::isfinite(route.etaSeconds)&&route.etaSeconds>=0&&route.etaSeconds<=4));
    if(imminent&&selected.origin!=5)return {Mode::DEFER,chosen,"routine cue deferred during an imminent turn; experimental maneuver window"};
    const auto p=selected.prompt;
    const bool speedOnly=p==Prompt::ease||p==Prompt::brake||p==Prompt::slowSmoothly||p==Prompt::gentleDown||p==Prompt::downhill||p==Prompt::steepDown;
    if(speedOnly&&c.memory.speedTrend(c.world.now)==SpeedTrend::Decreasing)
        return {Mode::SILENCE,chosen,"measured speed decreasing; redundant routine speed cue quiet"};
    return {Mode::COACH,chosen};
}
Prompt SafetyValidator::neutral(Prompt p,bool measured) noexcept {
    if(p==Prompt::camera||p==Prompt::slopeUnavailable)return Prompt::none;
    if(measured)return p;
    switch(p) {
    case Prompt::hill:return Prompt::uphill;
    case Prompt::downhillGear:return Prompt::downhill;
    case Prompt::steepDownGear:return Prompt::steepDown;
    case Prompt::bendLeftGear:return Prompt::bendLeftQuiet;
    case Prompt::bendRightGear:return Prompt::bendRightQuiet;
    case Prompt::closeGear:return Prompt::close;
    case Prompt::closeGearQuiet:return Prompt::closeQuiet;
    case Prompt::imageCloseGear:return Prompt::imageClose;
    case Prompt::shiftUp:case Prompt::shiftDown:case Prompt::maintainSpeed:return Prompt::none;
    default:return p;
    }
}
Block SafetyValidator::validate(const Candidate& c,const WorldState& w,double now) const noexcept {
    if(!w.active)return Block::Inactive;
    if(!std::isfinite(now)||now<w.now||!w.sourceId||!fresh(now,w.telemetry.time,.15))return Block::Stale;
    if(!c.supported||c.prompt==Prompt::none||int(c.prompt)<0||int(c.prompt)>promptCount||!c.identity.id||c.origin<1||c.origin>5)return Block::Unsupported;
    if(!std::isfinite(c.maxAge)||c.maxAge<=0||c.maxAge>(c.origin==5?(w.route.provider?3.:2.5):maxVisionAgeSeconds)||
       !std::isfinite(c.expiresAt)||c.expiresAt<=c.captureTime||!std::isfinite(c.confidence)||c.confidence<0||c.confidence>1||std::isnan(c.tti))return Block::Unsupported;
    const auto space=c.identity.space;
    const bool actorSpace=space==IdentitySpace::Attention||space==IdentitySpace::Optical||space==IdentitySpace::Spatial;
    const bool actorEvent=c.kind==EventKind::Attention||c.kind==EventKind::Approach||c.kind==EventKind::Critical;
    if((c.origin==4&&(space!=IdentitySpace::Attention||!actorEvent))||
       ((c.origin==1||c.origin==2)&&(!(space==IdentitySpace::Optical||space==IdentitySpace::Spatial||(space==IdentitySpace::Tactical&&w.simulation))||!actorEvent))||
       (c.origin==3&&!((space==IdentitySpace::Slope&&c.kind==EventKind::Slope)||(space==IdentitySpace::Bend&&c.kind==EventKind::Bend)||(space==IdentitySpace::Engine&&c.kind==EventKind::Shift)))||
       (c.origin==5&&(space!=IdentitySpace::Route||c.kind!=EventKind::Route)))return Block::Unsupported;
    if(c.urgent&&!actorEvent)return Block::Unsupported;
    if(c.origin==2&&c.prompt!=Prompt::ease&&c.prompt!=Prompt::brake&&c.prompt!=Prompt::emergency)return Block::Unsupported;
    if(c.origin==1&&c.prompt!=Prompt::holdLaneDistance&&c.prompt!=Prompt::keepSafeGap&&c.prompt!=Prompt::slowSmoothly&&
       c.prompt!=Prompt::watchPedestrians&&c.prompt!=Prompt::prepareBend&&c.prompt!=Prompt::observeSign&&
       c.prompt!=Prompt::emergency&&c.prompt!=Prompt::brakeHarder&&c.prompt!=Prompt::emergencyStop)return Block::Unsupported;
    if(c.kind==EventKind::Slope&&ld::slopePromptBand(c.prompt)==0)return Block::Unsupported;
    if(c.kind==EventKind::Shift&&c.prompt!=Prompt::shiftUp&&c.prompt!=Prompt::shiftDown)return Block::Unsupported;
    if(!fresh(now,c.captureTime,c.maxAge)||now>=c.expiresAt)return Block::Stale;
    const bool criticalPrompt=c.prompt==Prompt::emergency||c.prompt==Prompt::nearbyCollision||c.prompt==Prompt::brakeHarder||c.prompt==Prompt::emergencyStop;
    if((c.critical||criticalPrompt)&&!w.allowCritical)return Block::CriticalDisabled;
    if(criticalPrompt&&(!c.critical||!c.urgent))return Block::Unsupported;
    if(c.critical!=(c.kind==EventKind::Critical))return Block::Unsupported;
    if(w.telemetry.shock||((w.telemetry.braking||w.telemetry.hardBraking)&&!c.critical))return Block::DriverResponse;
    if(c.origin==5) {
        if(!w.route.valid||c.identity.space!=IdentitySpace::Route||c.identity.id!=w.route.eventId||c.identity.scope!=w.route.routeId||
            !fresh(now,w.route.captureTime,w.route.provider?3.:2.5)||!fresh(now,w.route.gpsTime,2.5))return Block::Unsupported;
        const bool left=c.prompt==Prompt::mapLeftTurn||c.prompt==Prompt::mapLeftTurnNear||c.prompt==Prompt::mapLeftTurn50;
        const bool right=c.prompt==Prompt::mapRightTurn||c.prompt==Prompt::mapRightTurnNear||c.prompt==Prompt::mapRightTurn50;
        if(int(c.prompt)<47||int(c.prompt)>55||(w.route.provider&&int(c.prompt)>=52))return Block::Unsupported;
        if((left&&w.route.feature!=4)||(right&&w.route.feature!=5)||
           (c.prompt==Prompt::mapLeftBend&&w.route.feature!=1)||(c.prompt==Prompt::mapRightBend&&w.route.feature!=2)||
           (c.prompt==Prompt::mapJunction&&w.route.feature!=3))return Block::Unsupported;
    }
    if(c.kind==EventKind::Slope&&(!w.telemetry.calibrated||!w.telemetry.pitchValid||!std::isfinite(w.telemetry.pitchDegrees)||
       w.gradeBand!=ld::slopePromptBand(c.prompt)||c.identity.id!=uint64_t(w.gradeBand+5)))return Block::Unsupported;
    if(c.kind==EventKind::Bend&&(!w.roadValid||!fresh(now,w.roadCaptureTime)||w.roadBend==0||uint64_t(w.roadBend+2)!=c.identity.id))return Block::Unsupported;
    if(c.kind==EventKind::Bend) {
        const bool left=c.prompt==Prompt::bendLeft||c.prompt==Prompt::bendLeftQuiet||c.prompt==Prompt::bendLeftGear;
        const bool right=c.prompt==Prompt::bendRight||c.prompt==Prompt::bendRightQuiet||c.prompt==Prompt::bendRightGear;
        if((!left&&!right)||(left&&w.roadBend>=0)||(right&&w.roadBend<=0))return Block::Unsupported;
    }
    const bool mechanical=neutral(c.prompt,false)!=c.prompt;
    if(mechanical||c.kind==EventKind::Shift) {
        if(!c.mechanicalMeasured||!w.engineValid||!fresh(now,w.engineCaptureTime,1)||!w.engineUnderLoad||
           w.engineGear<1||w.engineMaxGear>10||w.engineGear>w.engineMaxGear||
           !std::isfinite(w.engineRpm)||w.engineRpm<500||w.engineRpm>12000||
           !std::isfinite(w.engineLowerRpm)||!std::isfinite(w.engineUpperRpm)||w.engineLowerRpm<500||
           w.engineUpperRpm<=w.engineLowerRpm+500||w.engineUpperRpm>12000)return Block::Unsupported;
        if(c.kind==EventKind::Shift&&c.identity.scope!=uint64_t(w.engineGear))return Block::Unsupported;
        if(c.prompt==Prompt::shiftUp&&(w.engineRpm<=w.engineUpperRpm||w.engineGear>=w.engineMaxGear||
            w.gradeBand<0||(w.roadValid&&fresh(now,w.roadCaptureTime)&&w.roadBend!=0)))return Block::Unsupported;
        if(c.prompt==Prompt::shiftDown&&(w.engineRpm>=w.engineLowerRpm||w.engineGear<=1))return Block::Unsupported;
    }
    if(neutral(c.prompt,c.mechanicalMeasured)!=c.prompt)return Block::Unsupported;
    if(actorSpace) {
        const ActorObservation* selected=nullptr;
        for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i)if(w.actors[i].identity==c.identity){selected=&w.actors[i];break;}
        if(!selected||!selected->relevant||!fresh(now,selected->captureTime))return Block::Unsupported;
        if(!std::isfinite(selected->confidence)||selected->confidence<.4||selected->confidence>1||
           !std::isfinite(selected->x)||!std::isfinite(selected->y)||!std::isfinite(selected->w)||!std::isfinite(selected->h)||
           selected->x<0||selected->y<0||selected->w<=0||selected->h<=0||selected->x+selected->w>1.001||selected->y+selected->h>1.001)return Block::Unsupported;
        if(c.origin==4&&c.prompt!=Prompt::pedestrianSpace&&c.prompt!=Prompt::riderSpace&&c.prompt!=Prompt::vehicleSpace&&
           c.prompt!=Prompt::roadUserQuiet&&c.prompt!=Prompt::keepSafeGap&&c.prompt!=Prompt::emergency&&c.prompt!=Prompt::nearbyCollision)return Block::Unsupported;
        if((c.prompt==Prompt::pedestrianSpace||c.prompt==Prompt::watchPedestrians)&&selected->category!=0)return Block::Unsupported;
        if(c.prompt==Prompt::riderSpace&&selected->category!=1&&selected->category!=3)return Block::Unsupported;
        if(c.actorCategory>=0&&c.actorCategory!=selected->category)return Block::Unsupported;
        if(c.captureTime>selected->captureTime+1e-6)return Block::Stale;
        const bool speedAction=c.prompt==Prompt::ease||c.prompt==Prompt::brake||c.prompt==Prompt::slowSmoothly;
        if(speedAction&&(!selected->opticalClosing||(!selected->inPath&&!selected->leadSupported)||!std::isfinite(c.tti)||
            !w.telemetry.speedValid||!std::isfinite(w.telemetry.speedKmh)))return Block::Unsupported;
        if((c.prompt==Prompt::keepSafeGap||c.prompt==Prompt::vehicleSpace)&&!selected->leadSupported)return Block::Unsupported;
        if(c.urgent&&(!selected->opticalClosing||!std::isfinite(selected->tti)||selected->tti<=0||selected->confidence<.65))return Block::Unsupported;
        const bool attentionWarning=c.origin==4&&c.evidenceKind>=2&&
            (c.prompt==Prompt::pedestrianSpace||c.prompt==Prompt::riderSpace||c.prompt==Prompt::roadUserQuiet||c.prompt==Prompt::vehicleSpace||c.prompt==Prompt::nearbyCollision);
        if(c.urgent&&!selected->inPath&&!selected->leadSupported&&!attentionWarning)return Block::Unsupported;
        if(c.urgent&&selected->tti-(now-selected->captureTime)>(c.critical?2.:5.))return Block::Unsupported;
    }
    if(c.urgent&&(!std::isfinite(c.tti)||c.tti<=0||c.tti-(now-c.captureTime)>5||!std::isfinite(c.confidence)||c.confidence<.65||c.confidence>1))return Block::Unsupported;
    return Block::None;
}
bool CoachingEngine::configure(int p,bool voice,uint64_t revision) noexcept {
    if(p<0||p>2||!revision||revision<revision_)return false;
    if(configured_&&revision==revision_)return p==int(profile_)&&voice==voice_;
    if(accepted_.valid&&accepted_.stage!=DeliveryStage::Completed&&accepted_.stage!=DeliveryStage::VisualOnly)
        deliveryEvent(accepted_.sequence,int(accepted_.candidate.prompt),DeliveryStage::Interrupted,lastNow_);
    profile_=static_cast<Profile>(p);voice_=voice;revision_=revision;configured_=true;
    pending_={};accepted_={};deferred_={};decision_={};return true;
}
void CoachingEngine::beginSource(uint64_t source) noexcept {
    if(source==source_)return;
    invalidateEvidence();source_=source;ledger_.sourceChanged();
    synchronizer_.beginSource(source);tracks_.beginSource(source);
    interpreter_.reset(source);lastWorld_={};
    // Pacing is deliberately retained through camera/session/settings changes.
}
void CoachingEngine::invalidateEvidence() noexcept {
    if(accepted_.valid&&accepted_.stage!=DeliveryStage::Completed&&accepted_.stage!=DeliveryStage::VisualOnly)
        deliveryEvent(accepted_.sequence,int(accepted_.candidate.prompt),DeliveryStage::Interrupted,lastNow_);
    pending_={};accepted_={};deferred_={};decision_={};memory_.clear();interpreter_.reset(source_);
}
double CoachingEngine::interval() const noexcept {return profile_==Profile::Minimal?20:profile_==Profile::Guided?8:12;}
Decision CoachingEngine::decide(const WorldState& input,uint64_t sequence) noexcept {
    auto w=input;
    decision_={};pending_={};policyReason_=nullptr;
    if(!configured_||!std::isfinite(w.now)||w.now<0||w.now<lastNow_||w.sourceId!=source_) {decision_.blocked=Block::Stale;return decision_;}
    lastNow_=w.now;
    const auto telemetry=synchronizer_.accept(ObservationChannel::Telemetry,source_,0,w.telemetry.time,w.now,.15);
    if(telemetry==ObservationResult::OutOfOrder||telemetry==ObservationResult::Invalid)w.telemetry.time=-infinity;
    const auto camera=synchronizer_.accept(ObservationChannel::Camera,source_,w.frameSequence,w.captureTime,w.now);
    if(camera==ObservationResult::OutOfOrder||camera==ObservationResult::Invalid) {
        // Independent telemetry/navigation can proceed after a visual-clock
        // rejection. A bad frame cannot support an actor or bend candidate.
        if(std::isfinite(w.captureTime)){w.actorCount=0;w.roadValid=false;}
    }
    if(w.route.valid)synchronizer_.accept(ObservationChannel::Navigation,source_,0,w.route.captureTime,w.now,w.route.provider?3:2.5);
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i){auto&a=w.actors[i];
        const auto result=tracks_.observe({{int(a.identity.space),source_,a.identity.id},a.captureTime,a.confidence,a.category,a.x,a.y,a.w,a.h,a.relevant},w.now);
        if(result==ObservationResult::Invalid||result==ObservationResult::OutOfOrder||result==ObservationResult::Stale)a.relevant=false;
    }
    tracks_.expire(w.now);memory_.observe(w);ledger_.reconcile(source_,w);
    interpreter_.observe(w,memory_,ledger_,revision_);lastWorld_=w;
    if(deferred_.valid&&validator_.validate(deferred_.candidate,w,w.now)!=Block::None){deferred_={};++stats_.expired;}
    WorldState safe=w;safe.candidateCount=0;Block rejected=Block::None;bool duplicate=false;
    for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i) {
        auto candidate=w.candidates[i];candidate.prompt=SafetyValidator::neutral(candidate.prompt,candidate.mechanicalMeasured);
        const auto block=validator_.validate(candidate,w,w.now);
        if(block!=Block::None){rejected=block;continue;}
        auto& event=ledger_.observe(source_,candidate,w.now);
        event.providerContent=event.providerContent||(candidate.origin==5&&w.route.provider);
        const bool worsened=candidate.urgent&&std::isfinite(candidate.tti)&&std::isfinite(event.acceptedTti)&&
            candidate.tti<=event.acceptedTti-std::max(.75,event.acceptedTti*.25);
        if(event.acceptedAt>=0&&!event.retryable&&severity(candidate)<=event.acceptedSeverity&&
           (!worsened||w.now-event.acceptedAt<2)) {duplicate=true;continue;}
        safe.candidates[safe.candidateCount++]=candidate;
    }
    const auto choice=policy_->choose({safe,memory_,&interpreter_.scene(),&ledger_});
    if(int(choice.mode)<0||int(choice.mode)>3){decision_.blocked=Block::Unsupported;return decision_;}
    policyReason_=choice.reason;
    if(choice.mode==Mode::SILENCE||choice.candidate>=safe.candidateCount) {
        decision_.blocked=duplicate?Block::Duplicate:rejected;
        if(duplicate)++stats_.duplicates;else if(rejected!=Block::None)++stats_.selectionRejected;
        return decision_;
    }
    const auto& c=safe.candidates[choice.candidate];auto& event=ledger_.observe(source_,c,w.now);
    Ticket ticket{c,sequence,source_,revision_,event.number,w.now,-infinity,true,false,false,false};
    decision_.mode=choice.mode;decision_.ticket=ticket;
    if((choice.mode==Mode::WARN&&!c.urgent)||(c.urgent&&choice.mode!=Mode::WARN)){decision_.mode=Mode::SILENCE;decision_.blocked=Block::Unsupported;return decision_;}
    const bool ordinary=choice.mode!=Mode::WARN;
    if(choice.mode==Mode::DEFER||(ordinary&&w.now-lastOrdinary_<interval())||(!ordinary&&w.now-lastWarning_<.25)) {
        decision_.mode=Mode::DEFER;decision_.blocked=Block::Cooldown;event.state=EventState::Deferred;
        if(!deferred_.valid||deferred_.eventNumber!=ticket.eventNumber)++stats_.deferred;
        deferred_=ticket;return decision_;
    }
    // A single fresh slot is chosen anew every tick, never a FIFO of old cues.
    deferred_={};pending_=ticket;decision_.eligible=true;event.state=EventState::Active;return decision_;
}
bool CoachingEngine::ticketValid(const Ticket& t,uint64_t sequence,int prompt,const WorldState& w,double now) const noexcept {
    return t.valid&&sequence==t.sequence&&prompt==int(t.candidate.prompt)&&t.sourceId==source_&&w.sourceId==source_&&
        t.revision==revision_&&now>=t.decidedAt&&validator_.validate(t.candidate,w,now)==Block::None;
}
bool CoachingEngine::pending(uint64_t sequence,int prompt,const WorldState& w,double now) const noexcept {return ticketValid(pending_,sequence,prompt,w,now)&&!pending_.accepted;}
bool CoachingEngine::accept(uint64_t sequence,int prompt,const WorldState& w,double now,bool submitted) noexcept {
    if(!pending(sequence,prompt,w,now)||(submitted&&(!voice_||!w.audioReady)))return false;
    // Voice-on failure is not intentional visual-only delivery. Keep the event
    // eligible for a later fresh attempt and do not consume its routine interval.
    if(voice_&&!submitted){++stats_.submissionFailed;deliveryEvent(sequence,prompt,DeliveryStage::Failed,now);return false;}
    if(accepted_.valid&&accepted_.stage!=DeliveryStage::Completed&&accepted_.stage!=DeliveryStage::VisualOnly)
        deliveryEvent(accepted_.sequence,int(accepted_.candidate.prompt),DeliveryStage::Interrupted,now);
    auto& event=ledger_.observe(source_,pending_.candidate,now);
    event.state=EventState::Accepted;event.acceptedAt=now;event.acceptedSeverity=severity(pending_.candidate);
    event.acceptedPrompt=pending_.candidate.prompt;++event.acceptances;
    event.acceptedTti=pending_.candidate.tti;
    event.voiceSubmitted=submitted;event.visualOnly=!voice_;event.startedAt=-infinity;
    event.deliveryStage=submitted?DeliveryStage::Submitted:DeliveryStage::VisualOnly;event.retryable=false;
    accepted_=pending_;accepted_.accepted=true;accepted_.acceptedAt=now;accepted_.voiceSubmitted=submitted;
    accepted_.stage=event.deliveryStage;accepted_.priorOrdinary=lastOrdinary_;accepted_.priorWarning=lastWarning_;
    if(pending_.candidate.urgent)lastWarning_=now;else lastOrdinary_=now;
    ++stats_.logical;if(submitted)++stats_.voiceSubmitted;pending_={};return true;
}
bool CoachingEngine::playbackValid(uint64_t sequence,int prompt,const WorldState& w,double now,uint64_t revision) const noexcept {
    return revision==revision_&&voice_&&w.audioReady&&accepted_.accepted&&!accepted_.onset&&accepted_.stage==DeliveryStage::Submitted&&accepted_.voiceSubmitted&&ticketValid(accepted_,sequence,prompt,w,now);
}
bool CoachingEngine::playbackStarted(uint64_t sequence,int prompt,const WorldState& w,double now,uint64_t revision) noexcept {
    if(!playbackValid(sequence,prompt,w,now,revision)||accepted_.onset){++stats_.onsetRejected;return false;}
    accepted_.onset=true;++stats_.playbackOnsets;
    for(const auto& entry:ledger_.entries())if(entry.used&&entry.number==accepted_.eventNumber) {
        auto& event=ledger_.observe(source_,accepted_.candidate,now);event.startedAt=now;event.deliveryStage=DeliveryStage::Started;accepted_.stage=DeliveryStage::Started;break;
    }
    return true;
}
bool CoachingEngine::deliveryEvent(uint64_t sequence,int prompt,DeliveryStage stage,double now) noexcept {
    Ticket* ticket=nullptr;
    if(accepted_.valid&&accepted_.sequence==sequence&&int(accepted_.candidate.prompt)==prompt)ticket=&accepted_;
    else if(pending_.valid&&pending_.sequence==sequence&&int(pending_.candidate.prompt)==prompt)ticket=&pending_;
    if(!ticket||!std::isfinite(now)||now<ticket->decidedAt||
       (stage!=DeliveryStage::Scheduled&&stage!=DeliveryStage::Interrupted&&stage!=DeliveryStage::Expired&&stage!=DeliveryStage::Failed&&stage!=DeliveryStage::Completed))return false;
    if(ticket->stage==DeliveryStage::Completed||ticket->stage==DeliveryStage::VisualOnly)return stage==ticket->stage;
    if(stage==DeliveryStage::Completed&&!ticket->onset)return false;
    ticket->stage=stage;
    auto& event=ledger_.observe(source_,ticket->candidate,now);event.deliveryStage=stage;
    const bool failed=stage==DeliveryStage::Interrupted||stage==DeliveryStage::Expired||stage==DeliveryStage::Failed;
    if(failed) {
        event.retryable=true;
        if(ticket->accepted) {
            if(ticket->candidate.urgent&&lastWarning_==ticket->acceptedAt)lastWarning_=ticket->priorWarning;
            else if(!ticket->candidate.urgent&&lastOrdinary_==ticket->acceptedAt)lastOrdinary_=ticket->priorOrdinary;
        }
    }
    return true;
}
size_t CoachingEngine::diagnostics(double now,double* out,size_t capacity) const noexcept {
    if(!out||capacity<20)return 0;
    const auto& t=decision_.ticket;const double capture=t.valid?t.candidate.captureTime:-1;
    const double v[]={double(configured_),double(profile_),double(voice_),double(revision_),double(decision_.mode),double(decision_.blocked),double(t.eventNumber),capture,
        capture>=0&&std::isfinite(now)?now-capture:-1,double(memory_.size(now)),double(ledger_.activeCount()),double(ledger_.resolvedCount()),double(stats_.logical),
        double(stats_.voiceSubmitted),double(stats_.playbackOnsets),double(stats_.onsetRejected),double(stats_.duplicates),double(stats_.deferred),double(memory_.speedTrend(now)),interval()};
    std::copy_n(v,20,out);return 20;
}
std::string CoachingEngine::diagnosticsJson(double now) const {
    double v[20];diagnostics(now,v,20);std::ostringstream out;out.imbue(std::locale::classic());
    const char* names[]={"enabled","profile","voice","revision","mode","block","event_id","capture_time","evidence_age","history_samples","active_events","resolved_events",
        "logical_accepted","voice_submitted","playback_onsets","onset_rejected","duplicates","deferred","speed_trend","ordinary_interval_s"};
    out<<"{";for(int i=0;i<20;++i){if(i)out<<',';out<<'"'<<names[i]<<"\":";if(std::isfinite(v[i]))out<<v[i];else out<<"null";}
    out<<",\"submission_failed\":"<<stats_.submissionFailed<<",\"delivery_stage\":"<<int(accepted_.valid?accepted_.stage:pending_.stage)<<",\"reason_code\":\""<<reasonCode()<<"\",\"heard\":false,\"timing_status\":\"experimental\",\"speed_trend_is_pedal_state\":false}";return out.str();
}
const char* CoachingEngine::reasonCode() const noexcept {
    if(policyReason_&&*policyReason_>='A'&&*policyReason_<='Z')return policyReason_;
    switch(decision_.blocked){case Block::Inactive:return "SOURCE_INACTIVE";case Block::Stale:return "STALE_EVIDENCE";case Block::Unsupported:return "UNSUPPORTED_ACTION";case Block::Cooldown:return "PACING_OR_WORKLOAD";case Block::Duplicate:return "ALREADY_ACCEPTED_EVENT";case Block::Audio:return "AUDIO_UNAVAILABLE";case Block::Revision:return "REVISION_CHANGED";case Block::CriticalDisabled:return "CRITICAL_DISABLED";case Block::DriverResponse:return "MEASURED_BRAKING_OR_SHOCK";default:return decision_.mode==Mode::WARN?"IMPORTANT_SUPPORTED_EVENT":decision_.eligible?"NEW_SUPPORTED_EVENT":"NO_SUPPORTED_EVENT";}
}
const char* CoachingEngine::reason() const noexcept {
    if(policyReason_&&(decision_.blocked==Block::None||decision_.mode==Mode::DEFER))return policyReason_;
    switch(decision_.blocked) {
    case Block::Inactive:return "coaching source inactive";case Block::Stale:return "original evidence expired or clock/source invalid";
    case Block::Unsupported:return "current evidence does not support this cue";case Block::Cooldown:return "routine cue deferred by experimental coaching interval";
    case Block::Duplicate:return "event already presented; waiting for a meaningful change";case Block::Audio:return "audio unavailable";
    case Block::Revision:return "settings revision changed";case Block::CriticalDisabled:return "experimental critical command disabled";
    case Block::DriverResponse:return "measured braking or motion shock suppresses routine speech";default:break;
    }
    return decision_.eligible?(decision_.mode==Mode::WARN?"fresh independently supported approach warning":"fresh event selected by coaching policy"):"no new supported coaching event";
}
} // namespace ld::coaching
