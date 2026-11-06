// Full-interval teaching benchmark: SYNTHETIC scenario timelines only.
// Every tick of each interval is evaluated, including silent ones. The same
// intervals run through the 0.7.1 automatic policy (task scheduling off) and the
// task-aware schedule (on). Measured world inputs are scripted, not perceived;
// playback is simulated (fixed recording lengths, busy worker refuses routine
// cues, urgent cues preempt). No instructor labels exist for these intervals.
// Results establish scheduling behavior, not teaching quality.
#include "learndrive/coaching/CoachingEngine.hpp"
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ld;using namespace ld::coaching;
namespace {
void check(bool b,const std::string& why){if(!b)throw std::runtime_error(why);}
struct Truth {double lead=-1;bool junction=false;}; // Ground-truth lead to the scripted junction.
WorldState base(double t,double speed=30){WorldState w;w.sourceId=1;w.frameSequence=uint64_t(t*1000)+1;w.now=w.captureTime=t;
    w.active=w.audioReady=true;w.telemetry={t,speed,0,0,1,true,true,true,false,false,false};return w;}
void route(WorldState& w,double lead,uint64_t event=9,int feature=4,double capture=-1){const double at=capture<0?w.now:capture;
    w.route={17,event,2,at,lead*w.telemetry.speedKmh/3.6,lead,at,feature,true,true};}
void user(WorldState& w,uint64_t id,double tti=infinity,bool urgent=false){
    ActorObservation a;a.identity={IdentitySpace::Attention,id,0};a.captureTime=w.now;a.confidence=.9;a.relevant=true;a.category=0;
    a.inPath=a.opticalClosing=urgent||std::isfinite(tti);a.tti=urgent?2:tti;a.x=.4f;a.y=.3f;a.w=.1f;a.h=.4f;w.actors[w.actorCount++]=a;
    // Every camera road user is also an optical track (what the risk coach reasons over).
    {ActorObservation o=a;o.identity={IdentitySpace::Optical,id+1000,0};w.actors[w.actorCount++]=o;}

    Candidate c;c.prompt=Prompt::pedestrianSpace;c.origin=4;c.identity=a.identity;c.kind=urgent?EventKind::Approach:EventKind::Attention;
    c.captureTime=w.now;c.expiresAt=w.now+.35;c.supported=true;c.confidence=.9;c.urgent=urgent;c.tti=a.tti;c.actorCategory=0;c.evidenceKind=urgent?2:1;
    w.candidates[w.candidateCount++]=c;}
void hill(WorldState& w){w.telemetry.pitchDegrees=3;w.gradeBand=1;Candidate c;c.prompt=Prompt::gentleUp;c.origin=3;
    c.identity={IdentitySpace::Slope,6,0};c.kind=EventKind::Slope;c.captureTime=w.now;c.expiresAt=w.now+.35;c.supported=true;w.candidates[w.candidateCount++]=c;}
void bend(WorldState& w){w.roadValid=true;w.roadCaptureTime=w.now;w.roadBend=-1;Candidate c;c.prompt=Prompt::bendLeft;c.origin=3;
    c.identity={IdentitySpace::Bend,1,0};c.kind=EventKind::Bend;c.captureTime=w.now;c.expiresAt=w.now+.35;c.supported=true;w.candidates[w.candidateCount++]=c;}
using Script=std::function<Truth(WorldState&,double)>;
struct Scenario {const char* name;const char* purpose;double start,end;Script script;};
struct Metrics {
    int planning{},approach{},planningDelivered{},approachDelivered{},entry{},control{},routine{},warnings{},routineInExecution{},reserved{},interrupted{},completed{},maxSameEncounter{};
    double routineSpeechInExecution{};std::vector<std::pair<double,int>> spoken;
};
double duration(Prompt p){return p==Prompt::junctionApproach?3.8:3.0;}
Metrics run(const Scenario& s,int profile,bool scheduling){
    auto e=std::make_unique<CoachingEngine>();e->configure(profile,true,1);e->beginSource(1);e->enableAutomaticCoaching(true);e->enableTaskScheduling(scheduling);
    Metrics m;uint64_t seq=0,playing=0;int playingPrompt=0;double playEnd=-1;bool playingRoutine=false;
    std::vector<std::pair<uint64_t,int>> encounterCounts;
    for(int i=int(std::lround(s.start*10));i<=int(std::lround(s.end*10));++i){const double t=i/10.;auto w=base(t);const Truth truth=s.script(w,t);
        const bool execution=truth.junction&&truth.lead>=0&&truth.lead<executionQuietLead;
        if(playing){
            if(!e->continuationValid(playing,playingPrompt,w,t,1)){e->deliveryEvent(playing,playingPrompt,DeliveryStage::Interrupted,t);++m.interrupted;playing=0;}
            else if(t>=playEnd){e->deliveryEvent(playing,playingPrompt,DeliveryStage::Completed,t);++m.completed;playing=0;}
            else if(execution&&playingRoutine)m.routineSpeechInExecution+=.1;
        }
        const auto d=e->decide(w,++seq);
        if(!d.eligible)continue;
        const auto& c=d.ticket.candidate;
        if(playing&&!c.urgent)continue; // Busy worker: a routine cue is not submitted.
        if(!e->accept(seq,int(c.prompt),w,t,true))continue;
        if(playing)++m.interrupted; // Accepting preempts (engine marks the old ticket interrupted).
        check(e->playbackStarted(seq,int(c.prompt),w,t,1),std::string(s.name)+": onset rejected for fresh ticket");
        playing=seq;playingPrompt=int(c.prompt);playEnd=t+duration(c.prompt);playingRoutine=!c.urgent&&!c.lessonStep;
        m.spoken.push_back({t,int(c.prompt)});
        if(c.lessonStep==1)++m.planning;else if(c.lessonStep==2)++m.approach;else if(c.lessonStep==3)++m.entry;else if(c.urgent)++m.warnings;
        else if(isControlPrompt(c.prompt))++m.control;else ++m.routine;
        if(!c.urgent&&!c.lessonStep&&execution)++m.routineInExecution;
        if(c.identity.space==IdentitySpace::Attention){bool found=false;for(auto& x:encounterCounts)if(x.first==c.identity.id){++x.second;found=true;m.maxSameEncounter=std::max(m.maxSameEncounter,x.second);}
            if(!found){encounterCounts.push_back({c.identity.id,1});m.maxSameEncounter=std::max(m.maxSameEncounter,1);}}
    }
    m.reserved=int(e->stats().reservedForTask);m.planningDelivered=int(e->task().progress().planningCompletions);
    m.approachDelivered=int(e->task().progress().approachCompletions);return m;
}
// Junction ahead at constant 30 km/h, lead reaching zero at time `at`; after
// passing, navigation advances to a distant straight-on event.
Truth junctionAt(WorldState& w,double t,double at){const double lead=at-t;
    if(lead>=0){route(w,lead);return {lead,true};}
    route(w,120,10,0);return {-1,false};}
std::vector<Scenario> scenarios(){return {
    {"pre_window_road_user","routine presence cue just before the planning window",0,70,[](WorldState& w,double t){
        auto truth=junctionAt(w,t,60);if(t>=19&&t<30)user(w,7);return truth;}},
    {"pre_window_bend","camera bend cue just before the planning window",0,70,[](WorldState& w,double t){
        auto truth=junctionAt(w,t,60);if(t>=19&&t<21)bend(w);return truth;}},
    {"late_planning","junction appears at 24 s lead (e.g. after a reroute)",0,40,[](WorldState& w,double t){return junctionAt(w,t,24);}},
    {"routine_after_planning","road-user cue between planning and approach windows",0,50,[](WorldState& w,double t){
        auto truth=junctionAt(w,t,40);if(t>=16&&t<18.5)user(w,7);return truth;}},
    {"routine_after_planning_persistent","road user stays visible between planning and approach windows",0,50,[](WorldState& w,double t){
        auto truth=junctionAt(w,t,40);if(t>=16&&t<21)user(w,7);return truth;}},
    {"gps_dropout_final_approach","route evidence freezes at 6.5 s lead; road user appears inside execution",0,45,[](WorldState& w,double t){
        if(t<23.5)return junctionAt(w,t,30);
        if(t<33){route(w,30-23.5,9,4,23.5);if(t>=26.5&&t<28)user(w,7);return Truth{30-t,t<=30};}
        route(w,120,10,0);if(t>=36)hill(w);return Truth{};}},
    {"route_lost_in_execution","route disappears at 5 s lead; hill support must resume after a bounded hold",0,50,[](WorldState& w,double t){
        if(t<25)return junctionAt(w,t,30);
        if(t>=26&&t<27.5)user(w,7);if(t>=30)hill(w);return Truth{30-t,t<=30};}},
    {"urgent_in_execution","urgent closing pedestrian at 5 s lead",0,35,[](WorldState& w,double t){
        auto truth=junctionAt(w,t,30);if(t>=25&&t<26)user(w,8,2,true);return truth;}},
    {"closing_conflict_pre_window","developing closing conflict (tti 9 s) just before the planning window",0,70,[](WorldState& w,double t){
        auto truth=junctionAt(w,t,60);if(t>=19&&t<19.9)user(w,8,9);return truth;}},
    {"slowing_after_planning","measured slowing after planning makes the approach reminder redundant",0,40,[](WorldState& w,double t){
        const double speed=t<12?30:std::max(18.,30-(t-12)*2);w.telemetry.speedKmh=speed;const double lead=35-t;
        if(lead>=0){route(w,lead);w.route.distanceM=lead*speed/3.6;return Truth{lead,true};}route(w,120,10,0);return Truth{};}},
    {"route_free_mix","no destination: road user, bend and hill",0,60,[](WorldState& w,double t){
        if(t<5)user(w,7);else if(t>=15&&t<20)bend(w);else if(t>=30&&t<35)hill(w);return Truth{};}},
};}
}
int main(int argc,char** argv){try{
    std::ofstream out;if(argc>1){out.open(argv[1]);check(out.good(),"output");}
    const char* profiles[]={"minimal","balanced","guided"};int failures=0;
    auto expect=[&](bool ok,const std::string& what){if(!ok){++failures;std::cerr<<"EXPECTATION FAILED: "<<what<<'\n';}};
    for(const auto& s:scenarios())for(int p=0;p<3;++p){
        const auto old=run(s,p,false),now=run(s,p,true);
        for(const auto* m:{&old,&now}){const bool isNew=m==&now;
            std::cout<<s.name<<' '<<profiles[p]<<' '<<(isNew?"task_schedule":"v0.7.1")<<" planning="<<m->planning<<"/"<<m->planningDelivered<<" approach="<<m->approach<<"/"<<m->approachDelivered
                <<" routine="<<m->routine<<" warnings="<<m->warnings<<" routine_in_execution="<<m->routineInExecution
                <<" routine_speech_s_in_execution="<<m->routineSpeechInExecution<<" reserved_ticks="<<m->reserved<<" interrupted="<<m->interrupted<<'\n';
            if(out){out<<"{\"scope\":\"synthetic_interval\",\"instructor_labels\":null,\"scenario\":\""<<s.name<<"\",\"purpose\":\""<<s.purpose<<"\",\"profile\":\""<<profiles[p]
                <<"\",\"policy\":\""<<(isNew?"task_schedule":"v0.7.1")<<"\",\"planning_started\":"<<m->planning<<",\"planning_delivered\":"<<m->planningDelivered<<",\"approach_started\":"<<m->approach<<",\"approach_delivered\":"<<m->approachDelivered<<",\"routine\":"<<m->routine
                <<",\"warnings\":"<<m->warnings<<",\"routine_in_execution\":"<<m->routineInExecution<<",\"routine_speech_seconds_in_execution\":"<<m->routineSpeechInExecution
                <<",\"reserved_ticks\":"<<m->reserved<<",\"interrupted\":"<<m->interrupted<<",\"completed\":"<<m->completed<<",\"spoken\":[";
                for(size_t i=0;i<m->spoken.size();++i)out<<(i?",":"")<<"["<<m->spoken[i].first<<','<<m->spoken[i].second<<']';out<<"]}\n";}
            // Invariants for both policies.
            expect(m->planning<=1&&m->approach<=1,std::string(s.name)+": a lesson step repeated");
            expect(m->maxSameEncounter<=1,std::string(s.name)+": same encounter spoken twice");
        }
        const std::string n=std::string(s.name)+"/"+profiles[p];
        // Invariants of the task schedule.
        expect(now.planningDelivered>=old.planningDelivered,n+": schedule lost a delivered planning step");
        expect(now.routineInExecution==0&&now.routineSpeechInExecution==0,n+": routine speech inside junction execution");
        expect(now.routine+now.warnings<=old.routine+old.warnings+1,n+": schedule added unrelated speech");
        expect(now.entry<=1&&now.control<=3,n+": control instructions repeated");
        const std::string name=s.name;
        if(name=="pre_window_road_user"||name=="pre_window_bend")expect(now.planningDelivered==1,n+": planning lost to a pre-window routine cue");
        if((name=="pre_window_road_user"||name=="pre_window_bend")&&p>0)expect(now.routine==old.routine,n+": routine cue reserved without need");
        if(name=="late_planning")expect(now.planningDelivered==1&&now.approachDelivered==1,n+": lesson truncated or follow-up removed by profile pacing");
        if(name=="routine_after_planning"||name=="routine_after_planning_persistent")expect(now.approachDelivered==1,n+": approach reminder lost to routine cue");
        if(name=="gps_dropout_final_approach")expect(now.routine>=1,n+": hill support did not resume after the junction");
        if(name=="route_lost_in_execution")expect(now.routine>=1,n+": bounded hold never released");
        if(name=="urgent_in_execution")expect(now.warnings==1&&old.warnings==1,n+": urgent warning lost");
        if(name=="closing_conflict_pre_window")expect(now.routine==1&&now.planning==old.planning,n+": closing conflict was reserved away");
        if(name=="slowing_after_planning"){bool brake=false;for(const auto& x:now.spoken)brake|=x.second==int(Prompt::junctionBrakeGearDown);
            expect(old.approach==0&&!brake,n+": brake instruction repeated despite measured slowing");}
        // Minimal no longer mentions a non-closing road user (risk level 1); other profiles keep timing.
        if(name=="route_free_mix"&&p>0){bool same=now.spoken.size()==old.spoken.size();
            // Road-user cues now wait for temporal confirmation (~0.3 s).
            for(size_t i=0;same&&i<now.spoken.size();++i)same=std::abs(now.spoken[i].first-old.spoken[i].first)<=.5;
            expect(same,n+": route-free timing changed");}
    }
    if(failures){std::cerr<<failures<<" teaching interval expectations failed\n";return 1;}
    std::cout<<"teaching interval benchmark: all expectations met (synthetic intervals; not instructor-validated)\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
