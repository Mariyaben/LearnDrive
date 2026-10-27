#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/TemporalSceneInterpreter.hpp"
#include "learndrive/SceneState.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace ld::coaching {
namespace {
bool fresh(double now,double capture,double age=.35) noexcept {return cognition::fresh(now,capture,age);}
void number(std::ostream& s,double x){if(std::isfinite(x))s<<x;else s<<"null";}
bool delivered(const Event& e) noexcept {return e.acceptedAt>=0&&(e.visualOnly||e.deliveryStage==DeliveryStage::Completed);}
bool same(const EncounterEpisode& e,const ActorObservation& a) noexcept {
    return e.used&&e.space==int(a.identity.space)&&e.id==a.identity.id&&e.scope==a.identity.scope;
}
}
InstructionMeaning instructionMeaning(int p) noexcept {
    switch(static_cast<Prompt>(p)) {
    case Prompt::pedestrianSpace:case Prompt::riderSpace:case Prompt::roadUserQuiet:case Prompt::roadUser:case Prompt::watchPedestrians:return InstructionMeaning::RoadUserSpace;
    case Prompt::vehicleSpace:case Prompt::keepSafeGap:case Prompt::holdLaneDistance:return InstructionMeaning::FollowingGap;
    case Prompt::mapLeftTurn:case Prompt::mapRightTurn:case Prompt::mapLeftTurnNear:case Prompt::mapRightTurnNear:case Prompt::mapLeftTurn50:case Prompt::mapRightTurn50:case Prompt::mapJunction:return InstructionMeaning::TurnPreparation;
    case Prompt::bendLeft:case Prompt::bendRight:case Prompt::bendLeftQuiet:case Prompt::bendRightQuiet:case Prompt::mapLeftBend:case Prompt::mapRightBend:case Prompt::prepareBend:return InstructionMeaning::BendPreparation;
    case Prompt::shiftUp:case Prompt::shiftDown:return InstructionMeaning::VehicleShift;
    case Prompt::emergency:case Prompt::nearbyCollision:case Prompt::brakeHarder:case Prompt::emergencyStop:return InstructionMeaning::Critical;
    default:return slopePromptBand(static_cast<Prompt>(p))?InstructionMeaning::GradePreparation:InstructionMeaning::None;
    }
}
const char* encounterName(EncounterPhase x) noexcept {
    switch(x){case EncounterPhase::Observed:return "OBSERVED";case EncounterPhase::Continuing:return "CONTINUING";case EncounterPhase::PotentiallyRelevant:return "POTENTIALLY_RELEVANT";case EncounterPhase::Changing:return "CHANGING";case EncounterPhase::TemporarilyLost:return "TEMPORARILY_LOST";case EncounterPhase::Resolved:return "RESOLVED";default:return "EXPIRED";}
}
const char* responseName(ResponseEvidence x) noexcept {return x==ResponseEvidence::Observed?"RESPONSE_OBSERVED":x==ResponseEvidence::NotObserved?"RESPONSE_NOT_OBSERVED":"RESPONSE_UNCERTAIN";}
const char* silenceName(SilenceReason x) noexcept {
    switch(x){case SilenceReason::AlreadyInformed:return "ALREADY_INFORMED";case SilenceReason::ResponseObserved:return "RESPONSE_OBSERVED";case SilenceReason::LowValue:return "LOW_VALUE";case SilenceReason::UncertainEvidence:return "UNCERTAIN_EVIDENCE";case SilenceReason::ManeuverWorkload:return "MANEUVER_WORKLOAD";case SilenceReason::Resolved:return "RESOLVED_EVENT";case SilenceReason::Unchanged:return "UNCHANGED_EVENT";default:return "NO_SUPPORTED_EVENT";}
}
void TemporalSceneInterpreter::reset(uint64_t source) noexcept {scene_={};scene_.source=source;lastCamera_=lastNow_=-1;lastCandidates_=0;}
const InterpretedScene& TemporalSceneInterpreter::observe(const WorldState& w,const ShortMemory& memory,const EventLedger& ledger,uint64_t revision) noexcept {
    if(!std::isfinite(w.now)||w.now<0||w.now<lastNow_)return scene_;
    if(w.sourceId!=scene_.source)reset(w.sourceId);
    bool changed=scene_.revision!=revision;scene_.revision=revision;scene_.now=w.now;
    const bool newFrame=fresh(w.now,w.captureTime)&&w.captureTime>lastCamera_;
    const bool cameraFresh=fresh(w.now,w.captureTime);
    if(cameraFresh!=scene_.cameraFresh)changed=true;
    scene_.cameraFresh=cameraFresh;
    for(auto& e:scene_.encounters)if(e.used)e.fresh=false;
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i) {
        const auto& a=w.actors[i];if(!a.identity.id||!fresh(w.now,a.captureTime)||!std::isfinite(a.confidence)||a.confidence<.4||a.confidence>1||
            !std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.w)||!std::isfinite(a.h)||a.x<0||a.y<0||a.w<=0||a.h<=0||a.x+a.w>1.001||a.y+a.h>1.001)continue;
        EncounterEpisode* e=nullptr;
        for(auto& item:scene_.encounters)if(same(item,a)){e=&item;break;}
        if(!e) {
            for(auto& item:scene_.encounters)if(!item.used||w.now-item.lastSeen>=10){e=&item;break;}
            if(!e)e=&*std::min_element(scene_.encounters.begin(),scene_.encounters.end(),[](const auto& a,const auto& b){return a.lastSeen<b.lastSeen;});
            *e={};e->used=true;e->space=int(a.identity.space);e->id=a.identity.id;e->scope=a.identity.scope;e->firstSeen=a.captureTime;e->changedAt=w.now;changed=true;
        }
        e->fresh=true;
        if(a.captureTime<=e->captureTime)continue; // Duplicate rule ticks cannot manufacture continuity.
        const bool wasClosing=e->closing,wasRelevant=e->relevant;
        const double oldTti=e->tti;const auto oldPhase=e->phase;
        e->dx=e->observations?a.x+a.w*.5f-e->x-e->w*.5f:0;
        e->dy=e->observations?a.y+a.h*.5f-e->y-e->h*.5f:0;
        const bool material=e->observations&&(a.inPath!=e->inPath||a.leadSupported!=e->leadSupported||a.opticalClosing!=wasClosing||a.relevant!=wasRelevant||
            (a.opticalClosing&&std::isfinite(oldTti)&&a.tti<oldTti-.75));
        e->category=a.category;e->captureTime=a.captureTime;e->lastSeen=a.captureTime;e->confidence=a.confidence;e->tti=a.tti;
        e->x=a.x;e->y=a.y;e->w=a.w;e->h=a.h;e->closing=a.opticalClosing;e->relevant=a.relevant;
        e->inPath=a.inPath;e->leadSupported=a.leadSupported;e->missingFrames=0;++e->observations;
        if(!a.relevant&&e->wasRelevant)e->phase=EncounterPhase::Resolved;
        else if(material)e->phase=EncounterPhase::Changing;
        else if(oldPhase==EncounterPhase::TemporarilyLost)e->phase=EncounterPhase::Continuing;
        else if(a.relevant&&e->observations>=3&&a.captureTime-e->firstSeen>=.25&&!e->coached)e->phase=EncounterPhase::PotentiallyRelevant;
        else e->phase=e->observations==1?EncounterPhase::Observed:EncounterPhase::Continuing;
        if(material||e->phase!=oldPhase){e->changedAt=w.now;changed=true;}
        e->wasRelevant=e->wasRelevant||a.relevant;e->coached=false;
        for(const auto& event:ledger.entries())if(event.used&&event.sourceId==w.sourceId&&event.identity==a.identity&&delivered(event)){e->coached=true;break;}
    }
    scene_.count=0;
    for(auto& e:scene_.encounters)if(e.used) {
        const auto previous=e.phase;
        if(!e.fresh) {
            if(newFrame)++e.missingFrames;
            if(w.now-e.lastSeen>=10)e.phase=EncounterPhase::Expired;
            else if(w.now-e.lastSeen>=.35&&e.phase!=EncounterPhase::Resolved)e.phase=EncounterPhase::TemporarilyLost;
        }
        if(e.phase!=previous){e.changedAt=w.now;changed=true;}
        if(e.phase!=EncounterPhase::Expired)++scene_.count;
    }
    const bool routeFresh=w.route.valid&&fresh(w.now,w.route.captureTime,w.route.provider?3.:2.5)&&fresh(w.now,w.route.gpsTime,2.5);
    const int window=!routeFresh?0:w.route.distanceM<=20?3:w.route.distanceM<=70?2:w.route.distanceM<=150?1:0;
    if(scene_.routeProvider!=w.route.provider||scene_.routeFresh!=routeFresh||scene_.routeFeature!=w.route.feature||scene_.routeEvent!=w.route.eventId||scene_.routeId!=w.route.routeId||scene_.routeEpoch!=w.route.epoch||scene_.routeWindow!=window)changed=true;
    scene_.routeFresh=routeFresh;scene_.routeProvider=w.route.provider;scene_.routeId=w.route.routeId;scene_.routeEvent=w.route.eventId;scene_.routeEpoch=w.route.epoch;
    scene_.routeWindow=window;scene_.routeFeature=w.route.feature;scene_.routeDistance=w.route.distanceM;scene_.routeEta=w.route.etaSeconds;
    const int trend=int(memory.speedTrend(w.now));if(trend!=scene_.speedTrend)changed=true;scene_.speedTrend=trend;
    const bool speedValid=fresh(w.now,w.telemetry.time,.15)&&w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh)&&w.telemetry.speedKmh>=0;
    if(speedValid!=scene_.speedValid)changed=true;scene_.speedValid=speedValid;
    const auto oldInstruction=scene_.lastInstruction;const double oldInstructionAt=scene_.lastInstructionAt;const bool oldDelivered=scene_.lastInstructionDelivered;
    scene_.lastInstruction=0;scene_.lastInstructionAt=-1;scene_.lastInstructionDelivered=false;scene_.lastInstructionProvider=false;
    double latestAccepted=-1;
    for(const auto& e:ledger.entries())if(e.used&&e.sourceId==w.sourceId&&e.acceptedAt>latestAccepted) {
        latestAccepted=e.acceptedAt;
        scene_.lastInstruction=int(e.acceptedPrompt);scene_.lastInstructionAt=e.startedAt>=0?e.startedAt:e.acceptedAt;scene_.lastInstructionDelivered=delivered(e);scene_.lastInstructionProvider=e.providerContent;
    }
    if(oldInstruction!=scene_.lastInstruction||oldInstructionAt!=scene_.lastInstructionAt||oldDelivered!=scene_.lastInstructionDelivered)changed=true;
    auto response=ResponseEvidence::Uncertain;
    if(scene_.speedValid&&scene_.lastInstructionDelivered&&scene_.lastInstructionAt>=0&&w.now-scene_.lastInstructionAt>=1&&w.now-scene_.lastInstructionAt<=10&&trend!=int(SpeedTrend::Unknown))
        response=trend==int(SpeedTrend::Decreasing)?ResponseEvidence::Observed:ResponseEvidence::NotObserved;
    if(response!=scene_.response)changed=true;scene_.response=response;
    // Include the independently admissible choice set, without timestamp churn.
    // A new phrase/event/urgency or expired candidate invalidates cached reasoning.
    uint64_t signature=1469598103934665603ULL;SafetyValidator validator;
    const auto mix=[&](uint64_t value){signature^=value;signature*=1099511628211ULL;};
    for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i){auto c=w.candidates[i];c.prompt=SafetyValidator::neutral(c.prompt,c.mechanicalMeasured);
        if(validator.validate(c,w,w.now)!=Block::None)continue;
        mix(i);mix(int(c.prompt));mix(int(c.identity.space));mix(c.identity.id);mix(c.identity.scope);mix(c.urgent);
    }
    if(signature!=lastCandidates_){lastCandidates_=signature;changed=true;}
    scene_.silence=!w.active||!scene_.cameraFresh?SilenceReason::UncertainEvidence:SilenceReason::None;
    if(changed)++scene_.generation;if(newFrame)lastCamera_=w.captureTime;lastNow_=w.now;return scene_;
}

std::string TemporalSceneInterpreter::encode(const WorldState& w,const EventLedger& ledger) const {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(6)<<std::boolalpha;
    out<<"{\"schema_version\":2,\"model_eligible\":"<<(!scene_.routeProvider&&w.active)<<",\"source_id\":"<<scene_.source<<",\"revision\":"<<scene_.revision<<",\"generation\":"<<scene_.generation<<",\"time\":";number(out,w.now);
    out<<",\"vehicle\":{\"speed_kmh\":";number(out,scene_.speedValid?w.telemetry.speedKmh:infinity);
    out<<",\"speed_trend\":"<<scene_.speedTrend<<",\"response\":\""<<responseName(scene_.lastInstructionProvider?ResponseEvidence::Uncertain:scene_.response)<<"\",\"pedal_gear_rpm_known\":false}";
    out<<",\"navigation\":{\"provider_content_excluded\":"<<scene_.routeProvider;
    if(!scene_.routeProvider){out<<",\"fresh\":"<<scene_.routeFresh<<",\"event_id\":"<<scene_.routeEvent<<",\"route_id\":"<<scene_.routeId<<",\"epoch\":"<<scene_.routeEpoch<<",\"maneuver\":"<<scene_.routeFeature<<",\"window\":"<<scene_.routeWindow<<",\"distance_m\":";number(out,scene_.routeFresh?scene_.routeDistance:infinity);out<<",\"eta_s\":";number(out,scene_.routeFresh?scene_.routeEta:infinity);}
    out<<"},\"events\":[";bool comma=false;size_t encoded=0;
    // Bound the summary by importance, not array allocation order. Otherwise
    // old lost tracks can crowd the currently actionable actor out of the prompt.
    std::array<const EncounterEpisode*,64> ordered{};size_t count=0;SafetyValidator validator;
    for(const auto& e:scene_.encounters)if(e.used&&e.phase!=EncounterPhase::Expired)ordered[count++]=&e;
    const auto rank=[&](const EncounterEpisode& e){int score=(e.fresh?100:0)+(e.relevant?20:0)+(e.closing?20:0);
        for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i){auto c=w.candidates[i];c.prompt=SafetyValidator::neutral(c.prompt,c.mechanicalMeasured);
            if(e.space==int(c.identity.space)&&e.id==c.identity.id&&e.scope==c.identity.scope&&validator.validate(c,w,w.now)==Block::None)score+=200;}
        return score;};
    std::sort(ordered.begin(),ordered.begin()+count,[&](const auto* a,const auto* b){const int x=rank(*a),y=rank(*b);return x!=y?x>y:a->lastSeen>b->lastSeen;});
    for(size_t i=0;i<std::min(count,size_t(12));++i){const auto& e=*ordered[i];
        if(comma)out<<',';comma=true;++encoded;
        out<<"{\"space\":"<<e.space<<",\"id\":"<<e.id<<",\"scope\":"<<e.scope<<",\"category\":"<<e.category<<",\"state\":\""<<encounterName(e.phase)<<"\",\"fresh\":"<<e.fresh<<",\"capture_time\":";number(out,e.captureTime);
        out<<",\"observations\":"<<e.observations<<",\"confidence\":"<<e.confidence<<",\"relevant_region\":"<<e.relevant<<",\"supported_path\":"<<e.inPath<<",\"optical_closing\":"<<e.closing<<",\"tti_s\":";number(out,e.closing?e.tti:infinity);
        out<<",\"coached\":"<<e.coached<<",\"image_position\":["<<e.x<<','<<e.y<<','<<e.w<<','<<e.h<<"],\"image_delta\":["<<e.dx<<','<<e.dy<<"]}";
    }
    out<<"],\"history\":[";comma=false;size_t history=0;
    std::array<const Event*,64> deliveredHistory{};size_t historyCount=0;
    for(const auto& e:ledger.entries())if(e.used&&e.sourceId==w.sourceId&&e.acceptedAt>=0&&w.now-e.acceptedAt<=10&&!e.providerContent&&
        !(e.identity.space==IdentitySpace::Route&&scene_.routeProvider))deliveredHistory[historyCount++]=&e;
    std::sort(deliveredHistory.begin(),deliveredHistory.begin()+historyCount,[](const auto* a,const auto* b){return a->acceptedAt>b->acceptedAt;});
    for(size_t i=0;i<std::min(historyCount,size_t(8));++i){const auto& e=*deliveredHistory[i];
        if(comma)out<<',';comma=true;++history;out<<"{\"event_number\":"<<e.number<<",\"space\":"<<int(e.identity.space)<<",\"actor_id\":"<<e.identity.id<<",\"scope\":"<<e.identity.scope<<",\"instruction_id\":"<<int(e.acceptedPrompt)<<",\"meaning\":"<<int(instructionMeaning(int(e.acceptedPrompt)))<<",\"accepted_at\":"<<e.acceptedAt<<",\"submitted\":"<<e.voiceSubmitted<<",\"started\":"<<(e.startedAt>=0)<<",\"visual_only\":"<<e.visualOnly<<",\"delivery_stage\":"<<int(e.deliveryStage)<<",\"delivered\":"<<delivered(e)<<"}";
    }
    out<<"],\"candidates\":[";comma=false;
    for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i) {
        auto c=w.candidates[i];c.prompt=SafetyValidator::neutral(c.prompt,c.mechanicalMeasured);
        if(validator.validate(c,w,w.now)!=Block::None||(scene_.routeProvider&&c.origin==5))continue;
        if(comma)out<<',';comma=true;
        out<<"{\"index\":"<<i<<",\"instruction_id\":"<<int(c.prompt)<<",\"meaning\":"<<int(instructionMeaning(int(c.prompt)))<<",\"event_id\":"<<c.identity.id<<",\"space\":"<<int(c.identity.space)<<",\"urgent\":"<<c.urgent<<",\"approved_text\":\""<<promptText(c.prompt)<<"\",\"capture_time\":"<<c.captureTime<<",\"expires_at\":"<<c.expiresAt<<"}";
    }
    out<<"],\"limitations\":[\"No pedal, clutch, selected gear or driver intent inferred\",\"Image motion is not metric trajectory\",\"Model proposals have no live authority\"]}";return out.str();
}

PolicyChoice TemporalPolicy::choose(const PolicyContext& context) const noexcept {
    const auto baseline=fallback_.choose(context);
    if(!context.scene||!context.ledger||baseline.mode==Mode::WARN)return baseline;
    const auto& s=*context.scene;
    if(baseline.mode==Mode::SILENCE||baseline.candidate>=context.world.candidateCount)return baseline;
    const auto& c=context.world.candidates[baseline.candidate];
    if(!c.urgent)for(const auto& e:context.ledger->entries())if(e.used&&e.sourceId==context.world.sourceId&&e.identity==c.identity&&delivered(e)&&
       instructionMeaning(int(e.acceptedPrompt))==instructionMeaning(int(c.prompt))&&context.world.now-e.acceptedAt<=10)
        return {Mode::SILENCE,baseline.candidate,"ALREADY_INFORMED"};
    if(!c.urgent&&s.response==ResponseEvidence::Observed&&instructionMeaning(s.lastInstruction)==InstructionMeaning::TurnPreparation&&
        (c.prompt==Prompt::ease||c.prompt==Prompt::brake||c.prompt==Prompt::slowSmoothly))return {Mode::SILENCE,baseline.candidate,"RESPONSE_OBSERVED"};
    if(c.kind==EventKind::Attention&&!c.urgent)for(const auto& e:s.encounters)if(e.used&&e.space==int(c.identity.space)&&e.id==c.identity.id&&e.scope==c.identity.scope) {
        if(!e.fresh||e.phase==EncounterPhase::TemporarilyLost||e.phase==EncounterPhase::Resolved)return {Mode::SILENCE,baseline.candidate,"UNCERTAIN_EVIDENCE"};
        if(e.phase==EncounterPhase::Observed&&e.observations<2&&c.evidenceKind==0)return {Mode::DEFER,baseline.candidate,"NOVEL_ENCOUNTER_PENDING_CONTINUITY"};
    }
    return baseline;
}
} // namespace ld::coaching
