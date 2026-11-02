#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/DriveCoach.hpp"
#include "learndrive/SceneState.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace ld::coaching {
namespace {
DriveTask domain(const Candidate& c) noexcept {
    switch(instructionMeaning(int(c.prompt))){
    case InstructionMeaning::TurnPreparation:case InstructionMeaning::ApproachPreparation:return DriveTask::Junction;
    case InstructionMeaning::BendPreparation:return DriveTask::Bend;
    case InstructionMeaning::GradePreparation:return DriveTask::Grade;
    case InstructionMeaning::VehicleShift:return DriveTask::Vehicle;
    default:return c.kind==EventKind::Attention||c.kind==EventKind::Approach||c.kind==EventKind::Critical?DriveTask::Space:DriveTask::Observing;
    }
}
const char* name(DriveTask d) noexcept {
    switch(d){case DriveTask::Junction:return "JUNCTION";case DriveTask::Bend:return "BEND";case DriveTask::Grade:return "HILL";
    case DriveTask::Space:return "ROAD_USER_SPACE";case DriveTask::Vehicle:return "VEHICLE";default:return "OBSERVING";}
}
bool informed(const Candidate& c,const PolicyContext& context) noexcept {
    if(c.urgent||!context.ledger)return false;
    const auto meaning=instructionMeaning(int(c.prompt));
    if(meaning==InstructionMeaning::None)return false;
    for(const auto& e:context.ledger->entries())if(e.used&&e.sourceId==context.world.sourceId&&e.identity==c.identity&&
        e.state!=EventState::Resolved&&e.state!=EventState::Expired&&!e.retryable&&
        (e.visualOnly||e.deliveryStage==DeliveryStage::Completed)&&instructionMeaning(int(e.acceptedPrompt))==meaning&&
        (!c.lessonStep||e.lessonEpoch==c.lessonEpoch))return true;
    return false;
}
int rank(const Candidate& c,const WorldState& w) noexcept {
    // Validated urgent evidence always wins. A developing closing conflict takes
    // precedence over pedagogy, without granting it warning/cooldown authority.
    if(c.urgent)return 1000+priority(c.prompt);
    if(c.kind==EventKind::Approach||c.kind==EventKind::Attention)
        for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i){const auto& a=w.actors[i];
            if(a.identity==c.identity&&a.relevant&&a.opticalClosing&&(a.inPath||a.leadSupported)&&std::isfinite(a.tti)&&a.tti>0&&a.tti<=12)return 100;}
    if(c.lessonStep)return 80;
    switch(domain(c)){case DriveTask::Bend:return 65;case DriveTask::Space:return 60;case DriveTask::Grade:return 55;
    case DriveTask::Vehicle:return 45;default:return 40;}
}
}
void DriveCoach::enable(bool value) noexcept {if(enabled_==value)return;enabled_=value;deliveries_={};sourceChanged();}
void DriveCoach::sourceChanged() noexcept {task_=DriveTask::Observing;phase_=enabled_?"WATCHING_ROAD":"OFF";lastDelivery_=-1;}
void DriveCoach::observe(const WorldState& w,const TaskCoach& junction,const Ticket& delivery) noexcept {
    if(!enabled_)return;
    if(!w.active){task_=DriveTask::Observing;phase_="INACTIVE";return;}
    if(!cognition::fresh(w.now,w.captureTime)||!cognition::fresh(w.now,w.telemetry.time,.15)){
        task_=DriveTask::Observing;phase_="EVIDENCE_UNAVAILABLE";return;
    }
    const auto& j=junction.progress();
    if(j.phase==TaskPhase::ExecutionQuiet&&j.leadSeconds>=0&&j.leadSeconds<8){task_=DriveTask::Junction;phase_="EXECUTION_QUIET";return;}
    if(delivery.valid&&(delivery.stage==DeliveryStage::Submitted||delivery.stage==DeliveryStage::Started)){
        task_=domain(delivery.candidate);phase_="DELIVERING";return;
    }
    if(lastDelivery_>=0&&w.now-lastDelivery_<=4){phase_="OBSERVING_RESPONSE";return;}
    task_=DriveTask::Observing;phase_="WATCHING_ROAD";
    if(j.phase==TaskPhase::Preparing||j.phase==TaskPhase::Approach){task_=DriveTask::Junction;phase_="PREPARING";}
}
PolicyChoice DriveCoach::choose(const PolicyContext& context) const noexcept {
    auto focused=context.world;focused.candidateCount=0;
    for(size_t i=0;i<std::min(context.world.candidateCount,context.world.candidates.size());++i){
        const auto& c=context.world.candidates[i];if(!valid(c,context.world,context.world.now)||informed(c,context))continue;
        focused.candidates[focused.candidateCount++]=c;
    }
    // Ask the temporal policy about each candidate separately. An already
    // informed/low-value candidate cannot starve a different useful task.
    TemporalPolicy temporal;PolicyChoice best;int bestRank=-10000;bool deferred=false;
    for(size_t i=0;i<focused.candidateCount;++i){
        auto one=focused;one.candidateCount=1;one.candidates[0]=focused.candidates[i];
        const auto choice=temporal.choose({one,context.memory,context.scene,context.ledger});
        if(choice.mode==Mode::SILENCE)continue;
        const int score=rank(focused.candidates[i],context.world)-(choice.mode==Mode::DEFER?500:0);
        if(score>bestRank){
            bestRank=score;best=choice;deferred=choice.mode==Mode::DEFER;
            // Return the original safe-world index, not the filtered index.
            for(size_t k=0;k<context.world.candidateCount;++k)if(context.world.candidates[k].identity==focused.candidates[i].identity&&
                context.world.candidates[k].prompt==focused.candidates[i].prompt&&context.world.candidates[k].lessonStep==focused.candidates[i].lessonStep){best.candidate=k;break;}
        }
    }
    if(bestRank==-10000)return {};
    if(!deferred)best.reason=best.mode==Mode::WARN?"SUPPORTED_WARNING_PRIORITY":"AUTOMATIC_DRIVING_TASK";
    return best;
}
bool DriveCoach::valid(const Candidate& c,const WorldState& w,double now) const noexcept {
    if(!enabled_||c.urgent)return true;
    if(!w.active||!cognition::fresh(now,w.captureTime))return false;
    const auto d=domain(c);
    if(d==DriveTask::Grade||d==DriveTask::Bend||d==DriveTask::Junction)
        return w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh)&&w.telemetry.speedKmh>3&&w.telemetry.speedKmh<=300;
    return !(w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh)&&w.telemetry.speedKmh<=3);
}
void DriveCoach::selected(const Candidate& c,double) noexcept {if(enabled_){task_=domain(c);phase_=c.urgent?"WARNING":"PREPARING";}}
void DriveCoach::delivered(const Candidate& c,double now) noexcept {if(enabled_){++deliveries_[size_t(domain(c))];lastDelivery_=now;task_=domain(c);phase_="OBSERVING_RESPONSE";}}
std::string DriveCoach::json(const TaskCoach& junction) const {
    std::ostringstream o;o<<"{\"automatic\":"<<(enabled_?"true":"false")<<",\"task\":\""<<name(task_)<<"\",\"phase\":\""<<phase_
    <<"\",\"road_user_deliveries\":"<<deliveries_[size_t(DriveTask::Space)]<<",\"bend_deliveries\":"<<deliveries_[size_t(DriveTask::Bend)]
    <<",\"hill_deliveries\":"<<deliveries_[size_t(DriveTask::Grade)]<<",\"junction_deliveries\":"<<deliveries_[size_t(DriveTask::Junction)]
    <<",\"vehicle_deliveries\":"<<deliveries_[size_t(DriveTask::Vehicle)]<<",\"driving_outcome\":\"UNASSESSED\",\"junction\":"<<junction.json()<<'}';return o.str();
}
}
