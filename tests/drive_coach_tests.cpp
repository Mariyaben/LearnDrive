#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/c_api.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <string>
#include <tuple>
using namespace ld;using namespace ld::coaching;
namespace {
int passed=0;
void check(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
int failed=0;
// Every group runs: one failure no longer hides the rest of the suite.
template<class F>void test(const char* name,F f){try{f();++passed;std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
WorldState road(double t){WorldState w;w.sourceId=1;w.frameSequence=uint64_t(t*1000)+1;w.now=w.captureTime=t;
    w.active=w.audioReady=true;w.telemetry={t,30,0,0,1,true,true,true,false,false,false};return w;}
// Act-by schedule at 30 km/h: preparation 6.5-14.5 s, "brake, change down" 6.5-10.5 s,
// entry 2-6 s at or below 25 km/h; routine cues quiet below 8 s.
void junction(WorldState& w,double lead=13){w.route={17,9,2,w.now,lead*30/3.6,lead,w.now,4,true,true};}
// The same junction found on the map by the app itself (no Google route): steps by time, not by
// Google's distance.
void scoutJunction(WorldState& w,double lead=13){junction(w,lead);w.route.provider=false;}
void user(WorldState& w,uint64_t id=7,bool urgent=false,double tti=infinity){
    ActorObservation a;a.identity={IdentitySpace::Attention,id,0};a.captureTime=w.now;a.confidence=.9;a.relevant=true;a.category=0;
    a.inPath=a.opticalClosing=urgent||std::isfinite(tti);a.tti=urgent?2:tti;a.x=.4f;a.y=.3f;a.w=.1f;a.h=.4f;
    w.actors[w.actorCount++]=a;
    // Every camera road user is also an optical track (what the risk coach reasons over).
    {ActorObservation o=a;o.identity={IdentitySpace::Optical,id+1000,0};w.actors[w.actorCount++]=o;}
Candidate c;c.prompt=Prompt::pedestrianSpace;c.origin=4;c.identity=a.identity;c.kind=urgent?EventKind::Approach:EventKind::Attention;
    c.captureTime=w.now;c.expiresAt=w.now+.35;c.supported=true;c.confidence=.9;c.urgent=urgent;c.tti=a.tti;c.actorCategory=0;c.evidenceKind=urgent?2:1;
    w.candidates[w.candidateCount++]=c;
}
void hill(WorldState& w){w.telemetry.pitchDegrees=3;w.gradeBand=1;Candidate c;c.prompt=Prompt::gentleUp;c.origin=3;
    c.identity={IdentitySpace::Slope,6,0};c.kind=EventKind::Slope;c.captureTime=w.now;c.expiresAt=w.now+.35;c.supported=true;w.candidates[w.candidateCount++]=c;}
void bend(WorldState& w){w.roadValid=true;w.roadCaptureTime=w.now;w.roadBend=-1;Candidate c;c.prompt=Prompt::bendLeft;c.origin=3;
    c.identity={IdentitySpace::Bend,1,0};c.kind=EventKind::Bend;c.captureTime=w.now;c.expiresAt=w.now+.35;c.supported=true;w.candidates[w.candidateCount++]=c;}
struct Fixture {
    std::unique_ptr<CoachingEngine> e=std::make_unique<CoachingEngine>();uint64_t seq{};
    Fixture(int profile=1,bool voice=false){e->configure(profile,voice,1);e->beginSource(1);e->enableAutomaticCoaching(true);}
    double last=-1;uint64_t warmSeq=1000000;
    // A road user "present at t" has been visible for the preceding 0.4 s: the risk
    // coach needs temporal confirmation (single-frame flickers never instruct).
    ld::coaching::Decision decide(const WorldState& w){
        bool optical=false;for(size_t i=0;i<w.actorCount;++i)optical|=w.actors[i].identity.space==IdentitySpace::Optical;
        if(optical&&w.now-last>.45)for(int k=4;k>=1;--k){auto p=w;const double dt=.1*k;p.now-=dt;p.captureTime-=dt;p.telemetry.time-=dt;
            p.frameSequence=uint64_t(p.now*1000)+1;if(p.route.valid){p.route.captureTime-=dt;p.route.gpsTime-=dt;p.route.etaSeconds+=dt;}
            if(p.roadValid)p.roadCaptureTime-=dt;for(size_t i=0;i<p.actorCount;++i)p.actors[i].captureTime-=dt;
            for(size_t i=0;i<p.candidateCount;++i){p.candidates[i].captureTime-=dt;p.candidates[i].expiresAt-=dt;}
            if(p.now>last)e->decide(p,++warmSeq);}
        last=w.now;return e->decide(w,++seq);}
    int accept(const WorldState& w,bool voice=false){auto d=decide(w);if(!d.eligible)return 0;
        const int p=int(d.ticket.candidate.prompt);check(e->accept(seq,p,w,w.now,voice),"accept");return p;}
};
}
int main(int argc,char** argv){try {
    test("automatic session activates junction coaching without lesson selection",[]{Fixture f;auto w=road(1);scoutJunction(w);check(f.accept(w)==56,"automatic planning missing");
        w=road(14);scoutJunction(w,9);check(f.accept(w)==60,"automatic approach missing");});
    test("route-free driving keeps road-user bend and hill support",[]{Fixture f;auto w=road(1);user(w);check(f.accept(w)==64,"route-free road user");
        w=road(14);bend(w);check(f.accept(w)==int(Prompt::bendLeft),"route-free bend");w=road(27);hill(w);check(f.accept(w)==int(Prompt::gentleUp),"route-free hill");
        const auto s=f.e->drive().json(f.e->task());check(s.find("\"road_user_deliveries\":1")!=std::string::npos&&s.find("\"bend_deliveries\":1")!=std::string::npos&&s.find("\"hill_deliveries\":1")!=std::string::npos,"mixed record");});
    test("an urgent pedestrian interrupts preparation and only fresh useful preparation resumes",[]{Fixture f(1,true);auto w=road(1);junction(w);check(f.accept(w,true)==56,"prepare");
        check(f.e->playbackStarted(1,56,w,1,1),"planning onset");
        {auto p=road(1.1);junction(p,12.9);user(p,8,true);f.e->decide(p,999);} // Seen on the previous frame too (urgent needs 2 detections).
        w=road(1.2);junction(w,12.8);user(w,8,true);
        // The risk coach decides what to do about the urgent pedestrian: brake, be ready to stop.
        check(f.accept(w,true)==int(Prompt::brakeReadyStop),"hazard did not win");check(!f.e->playbackValid(1,56,w,1.2,1),"old task remained playable");
        check(f.e->playbackStarted(2,67,w,1.2,1)&&f.e->deliveryEvent(2,67,DeliveryStage::Completed,3),"warning completion");
        w=road(3.1);junction(w,11.1);check(f.accept(w,true)==56,"fresh task did not recover after interrupt");
        check(!f.e->task().progress().planningDelivered,"interrupted preparation counted as delivered");});
    test("developing measured conflict ranks ahead of a routine lesson without warning authority",[]{Fixture f;auto w=road(1);junction(w);user(w,8,false,9);
        auto d=f.decide(w);check(d.eligible&&d.mode==Mode::COACH&&d.ticket.candidate.prompt==Prompt::personEaseCover,"closing conflict starved by lesson");});
    test("routine road-user presence cannot displace timely task preparation",[]{Fixture f;auto w=road(1);junction(w);user(w);check(f.accept(w)==56,"presence displaced planning");});
    test("semantic variants do not repeat an unchanged continuous encounter after pacing",[]{Fixture f;auto w=road(1);user(w);check(f.accept(w)==64,"first space cue");
        for(int i=1;i<=150;++i){w=road(1+i*.1);user(w);auto& c=w.candidates[0];c.prompt=Prompt::roadUserQuiet;c.kind=EventKind::Approach;
            check(!f.decide(w).eligible,"semantic variant repeated same encounter");}
        w=road(17);user(w,99);check(f.accept(w)==64,"new encounter suppressed as duplicate");});
    test("already-informed high-ranked cue cannot starve another task",[]{Fixture f;auto w=road(1);user(w);check(f.accept(w)==64,"first");
        w=road(14);user(w);w.candidates[0].prompt=Prompt::roadUserQuiet;w.candidates[0].kind=EventKind::Approach;hill(w);
        check(f.accept(w)==int(Prompt::gentleUp),"duplicate starved hill preparation");});
    test("quiet execution retains urgent evidence and resumes other tasks afterwards",[]{Fixture f;auto w=road(1);scoutJunction(w,5.5);bend(w);hill(w);user(w);check(!f.decide(w).eligible,"routine speech during execution");
        w=road(1.1);scoutJunction(w,5.4);user(w,8,true);auto d=f.decide(w);check(d.eligible&&d.mode==Mode::WARN,"urgent hidden in execution");
        // Navigation moved on to a different fresh event: the junction ended and other tasks resume.
        w=road(2);scoutJunction(w,60);w.route.eventId=10;w.route.feature=0;hill(w);check(f.accept(w)==int(Prompt::gentleUp),"junction quiet leaked into later road context");
        // Route lost inside execution: routine silence is held (bounded), then released.
        Fixture g;w=road(1);scoutJunction(w,5.5);g.decide(w);w=road(2);hill(w);check(!g.decide(w).eligible,"route dropout un-quieted execution");
        check(g.e->drive().json(g.e->task()).find("EXECUTION_QUIET")!=std::string::npos,"held quiet not visible");
        w=road(11.2);hill(w);check(g.accept(w)==int(Prompt::gentleUp),"bounded execution hold never released");
        Fixture h;h.e->enableTaskScheduling(false);w=road(1);scoutJunction(w,6);h.decide(w);w=road(2);hill(w);check(h.accept(w)==int(Prompt::gentleUp),"0.7.1 comparison mode changed");});
    test("a routine cue yields to a known upcoming lesson window but a closing conflict does not",[]{Fixture f(0);auto w=road(1);junction(w,25);bend(w);
        check(!f.decide(w).eligible&&std::string(f.e->reasonCode())=="RESERVED_FOR_UPCOMING_TASK","routine cue consumed the lesson's interval");
        w=road(1.6);junction(w,24.4);user(w,8,false,9);auto d=f.decide(w);check(d.eligible&&d.ticket.candidate.prompt==Prompt::personEaseCover,"closing conflict reserved away");
        Fixture b(1);w=road(1);junction(w,25);user(w);check(b.decide(w).eligible,"Balanced reserved although the window stays reachable");});
    test("an episode follow-up step has its own spacing in every profile",[]{for(int profile=0;profile<3;++profile){Fixture f(profile);auto w=road(1);scoutJunction(w,13);check(f.accept(w)==56,"plan");
        w=road(4);scoutJunction(w,10);check(!f.decide(w).eligible,"follow-up crammed after planning");
        w=road(6);scoutJunction(w,8);check(f.accept(w)==60,"profile pacing removed the follow-up step");}});
    test("lesson speech started late in its window is not truncated by its onset window",[]{Fixture f(1,true);auto w=road(1);junction(w,7);check(f.accept(w,true)==56,"plan");
        check(f.e->playbackStarted(1,56,w,1,1),"onset");w=road(2.5);junction(w,6.2);check(f.e->continuationValid(1,56,w,2.5,1),"planning truncated below its onset window");
        w=road(2.6);junction(w,1.5);check(!f.e->continuationValid(1,56,w,2.6,1),"lesson continued into the turn itself");
        Fixture old(1,true);old.e->enableTaskScheduling(false);w=road(1);junction(w,23);check(old.accept(w,true)==56&&old.e->playbackStarted(1,56,w,1,1),"old onset");
        w=road(2.5);junction(w,21.5);check(!old.e->continuationValid(1,56,w,2.5,1),"0.7.1 comparison mode changed");});
    test("instructor control: gear changes follow measured acceleration",[]{Fixture f(2);std::vector<int> said;
        for(int i=10;i<=300;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=4+1.5*t;const int p=f.accept(w);if(p)said.push_back(p);}
        auto has=[&](Prompt p){for(int x:said)if(x==int(p))return true;return false;};
        check(has(Prompt::gearUpSecond)&&has(Prompt::gearUpThird)&&has(Prompt::gearUpFourth),"gear-up instructions missing");
        check(f.e->control().advisedGear()==4,"advised gear does not follow speed");
        check(f.e->control().json().find("\"selected_gear\":\"UNOBSERVED\"")!=std::string::npos,"selected gear claimed");});
    test("instructor control: slowing to a stop then moving-off preparation",[]{Fixture f(2);std::vector<int> said;
        for(int i=10;i<=250;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=std::max(0.,40-3*(t-1));const int p=f.accept(w);if(p)said.push_back(p);}
        auto has=[&](Prompt p){for(int x:said)if(x==int(p))return true;return false;};
        check(has(Prompt::gearDownSecond),"change-down instruction missing");check(has(Prompt::stopClutch),"clutch instruction missing at stop");
        check(has(Prompt::moveOffPrep),"moving-off preparation missing");
        for(int x:said)check(x!=int(Prompt::gearUpSecond)&&x!=int(Prompt::gearUpThird),"gear-up instruction while slowing");
        // The same stop where navigation says the destination is here: no moving-off cue.
        Fixture end(2);said.clear();
        for(int i=10;i<=250;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=std::max(0.,40-3*(t-1));w.arriving=t>8;
            const int p=end.accept(w);if(p)said.push_back(p);}
        check(has(Prompt::stopClutch),"clutch instruction missing at the destination stop");
        check(!has(Prompt::moveOffPrep),"moving-off preparation at the destination");});
    test("instructor control: road user escalates from ease-off to brake when closing",[]{Fixture f(1);auto w=road(1);user(w);
        check(f.accept(w)==int(Prompt::personEaseCover),"announcement instead of instruction");
        w=road(4);user(w,7,false,4);check(f.accept(w)==int(Prompt::brakeReadyStop),"closing road user did not escalate to brake");
        // No measured response (speed unchanged) 3 s later: escalate once to an urgent warning.
        w=road(7);user(w,7,false,3);auto d=f.decide(w);check(d.eligible&&d.mode==Mode::WARN,"unanswered conflict did not escalate");
        // Measured slowing after the instruction: the learner is responding, so stay quiet.
        Fixture g(1);w=road(1);user(w);check(g.accept(w)==int(Prompt::personEaseCover),"ease-off fixture");
        w=road(4);user(w,7,false,4);check(g.accept(w)==int(Prompt::brakeReadyStop),"brake fixture");
        for(int i=41;i<=70;++i){w=road(i/10.);w.telemetry.speedKmh=30-(i-40)*.5;user(w,7,false,3);
            check(!g.decide(w).eligible,"escalated although the learner was slowing");}});
    test("instructor control: low speed road user means clutch and brake; unknown speed stays an announcement",[]{
        Fixture f(1);auto w=road(1);w.telemetry.speedKmh=8;user(w);check(f.accept(w)==int(Prompt::clutchHoldBrake),"low-speed instruction missing");
        Fixture u(1);w=road(1);w.telemetry.speedValid=false;user(w);check(u.accept(w)==int(Prompt::pedestrianSpace),"control instruction from unknown speed");
        Fixture old(1);old.e->enableTaskScheduling(false);w=road(1);user(w);check(old.accept(w)==int(Prompt::pedestrianSpace),"0.7.1 comparison mode changed");});
    test("instructor control: downhill gear from measured calibrated pitch only",[]{Fixture f(1);auto w=road(1);w.telemetry.pitchDegrees=-6;w.gradeBand=-2;
        check(f.accept(w)==int(Prompt::downhillThird),"downhill gear instruction missing");
        Fixture g(1);w=road(1);w.telemetry.pitchDegrees=-6;w.gradeBand=-2;w.telemetry.calibrated=false;check(!g.decide(w).eligible,"slope instruction without calibration");});
    test("instructor control: junction sequence signals, brakes, changes down, enters and exits",[]{Fixture f(1);std::vector<int> said;
        for(int i=10;i<=500;++i){const double t=i/10.;auto w=road(t);const double lead=40-t;
            w.telemetry.speedKmh=t<32?30:std::max(8.,30-(t-32)*4); // Steady, then slowing into the turn.
            if(lead>=0){junction(w,lead);w.route.distanceM=lead*w.telemetry.speedKmh/3.6;}else{junction(w,60);w.route.eventId=10;w.route.feature=0;}
            const int p=f.accept(w);if(p)said.push_back(p);}
        auto at=[&](Prompt p){for(size_t i=0;i<said.size();++i)if(said[i]==int(p))return int(i);return -1;};
        const int plan=at(Prompt::junctionPlanLeft),approach=std::max(at(Prompt::junctionBrakeGearDown),at(Prompt::junctionGearDown)),entry=at(Prompt::junctionEntry),exit=at(Prompt::turnExit);
        if(!(plan>=0&&approach>plan&&entry>approach&&exit>entry)){for(int x:said)std::cerr<<x<<' ';std::cerr<<std::endl;}
        check(plan>=0&&approach>plan&&entry>approach&&exit>entry,"instructor junction sequence incomplete or out of order");});
    test("a started junction entry finishes inside the junction through braking, a jolt and a late frame",[]{Fixture f(1,true);
        uint64_t entry=0;double start=0;
        for(int i=10;i<=420&&!entry;++i){const double t=i/10.;auto w=road(t);const double lead=40-t;
            w.telemetry.speedKmh=t<32?30:std::max(8.,30-(t-32)*4);junction(w,lead);w.route.distanceM=lead*w.telemetry.speedKmh/3.6;
            const auto d=f.decide(w);if(!d.eligible)continue;const int p=int(d.ticket.candidate.prompt);
            if(!f.e->accept(f.seq,p,w,t,true))continue;check(f.e->playbackStarted(f.seq,p,w,t,1),"onset");
            if(p==int(Prompt::junctionEntry)){entry=f.seq;start=t;}else check(f.e->deliveryEvent(f.seq,p,DeliveryStage::Completed,t),"completion");}
        check(entry!=0,"entry step missing");
        // 2.5 s later the car is in the junction: Google moved to its next step, the learner brakes,
        // the road jolts and the camera frame is late. The sentence finishes.
        auto w=road(start+2.5);junction(w,60);w.route.eventId=10;w.route.feature=0;w.telemetry.speedKmh=10;
        w.telemetry.braking=w.telemetry.shock=true;w.captureTime=start+1.9;
        check(f.e->continuationValid(entry,int(Prompt::junctionEntry),w,start+2.5,1),"entry instruction cut off inside the junction");
        w.route.routeId=99;check(!f.e->continuationValid(entry,int(Prompt::junctionEntry),w,start+2.5,1),"lesson continued after Google replaced the route");});
    test("a Google turn is not coached from the start again after the camera restarts",[]{Fixture f(1,true);
        auto w=road(1);junction(w,13);check(f.accept(w,true)==56&&f.e->playbackStarted(f.seq,56,w,1,1)&&f.e->deliveryEvent(f.seq,56,DeliveryStage::Completed,5),"preparation");
        // The camera restarts (a hot phone): a new source, the same Google turn 92 m ahead.
        f.e->beginSource(2);
        w=road(6);w.sourceId=2;junction(w,11);check(!f.decide(w).eligible,"the turn preparation was said again after the camera restarted");
        w=road(9);w.sourceId=2;junction(w,7);check(f.accept(w,true)==60,"the turn's next step was lost after the camera restarted");
        // A different turn after a restart starts from its preparation as usual.
        f.e->beginSource(3);w=road(40);w.sourceId=3;junction(w,13);w.route.eventId=10;check(f.accept(w,true)==56,"a new turn after a restart was not prepared");
        // A sentence the restart itself cut off after its key words is not started over either.
        Fixture g(1,true);w=road(1);junction(w,13);check(g.accept(w,true)==56&&g.e->playbackStarted(g.seq,56,w,1,1),"onset");
        w=road(3.5);junction(w,10.5);g.decide(w);g.e->beginSource(2);
        w=road(4.5);w.sourceId=2;junction(w,9.5);check(!g.decide(w).eligible,"an instruction the restart cut off after 2.5 s was said again");});
    test("a rerouting blink of Google's feed lets the turn sentence finish and starts nothing new",[]{Fixture f(1,true);
        auto w=road(1);junction(w,13);check(f.accept(w,true)==56&&f.e->playbackStarted(f.seq,56,w,1,1),"preparation onset");
        w=road(2);junction(w,12);w.route.held=true;
        check(f.e->continuationValid(f.seq,56,w,2,1),"the sentence was cut by a rerouting blink");
        Fixture g(1,true);w=road(1);junction(w,13);w.route.held=true;check(!g.decide(w).eligible,"an instruction started on a turn carried through a blink");});
    test("a turn instruction cut off after its key words is not started over; one cut at once is",[]{
        for(double heard:{2.6,.8}){Fixture f(1,true);auto w=road(1);junction(w,13);
            check(f.accept(w,true)==56&&f.e->playbackStarted(f.seq,56,w,1,1),"preparation onset");
            check(f.e->deliveryEvent(f.seq,56,DeliveryStage::Interrupted,1+heard),"interrupt");
            w=road(4);junction(w,10);const auto d=f.decide(w);
            if(heard>=2)check(!d.eligible,"an instruction heard for 2.6 s was said again from the start");
            else check(d.eligible&&d.ticket.candidate.lessonStep==1,"an instruction cut after 0.8 s was not retried");}});
    test("a started turn sentence finishes through a moment without a speed reading",[]{Fixture f(1,true);
        auto w=road(1);junction(w,13);check(f.accept(w,true)==56&&f.e->playbackStarted(f.seq,56,w,1,1),"preparation onset");
        w=road(2.5);junction(w,11.5);w.telemetry.speedValid=false;
        check(f.e->continuationValid(f.seq,56,w,2.5,1),"a gap in the speed reading cut the turn instruction (Nov 8, 3:59)");
        // ...but none is started without a speed reading.
        Fixture g(1,true);w=road(1);junction(w,13);w.telemetry.speedValid=false;check(!g.decide(w).eligible,"a turn instruction started without a speed reading");});
    test("risk coach reasons over each road user's own motion history",[]{Fixture f(2);
        // A car crossing from the left toward the path at a junction, growing as it nears.
        for(int i=10;i<=26;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=12;
            ActorObservation a;a.identity={IdentitySpace::Optical,501,0};a.captureTime=t;a.confidence=.9;a.relevant=true;a.category=2;
            a.w=float(.12+.02*(t-1));a.h=float(.12+.02*(t-1));a.x=float(.05+.16*(t-1));a.y=float(.62-a.h*.5);
            w.actors[w.actorCount++]=a;f.decide(w);}
        const auto& top=f.e->risk().top();
        if(!(top.expansion>0&&top.inwardRate>.05))std::cerr<<f.e->risk().json()<<std::endl;
        check(top.expansion>0&&top.inwardRate>.05,"risk coach has no motion history");
        check(top.kind==RiskKind::Crossing,"crossing road user not recognised");});
    test("one picture of the moment: uphill, oncoming truck and a pedestrian become one instruction",[]{
        Fixture f(2);std::string spoken;bool composed=false;
        // A quiet lead-in (10 km/h on the flat: the truck still out of reach, the pedestrian below
        // roadside speed) gives both road users real motion history before the uphill begins.
        for(int i=5;i<=40;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=t<1?10:25;w.telemetry.pitchDegrees=t<1?0:7;w.gradeBand=slopeBand(w.telemetry.pitchDegrees);
            // Truck coming down toward us in its own lane, 2.4 m right of our path, closing at 15 m/s
            // (ours 6.9 + its 8): its box grows, sits on the road and drifts outward like a real one.
            const double gap=std::max(6.,40-15*(t-1));
            ActorObservation k;k.identity={IdentitySpace::Optical,701,0};k.captureTime=t;k.confidence=.9;k.relevant=true;k.category=7;
            const float s=float(.96*3/gap);k.h=s;k.w=s*.9f;k.x=float(.5+.72*2.4/gap)-k.w*.5f;k.y=float(.5+.96*1.25/gap)-s;w.actors[w.actorCount++]=k;
            // Pedestrian standing at the left edge of the road.
            ActorObservation p;p.identity={IdentitySpace::Optical,702,0};p.captureTime=t;p.confidence=.9;p.relevant=true;p.category=0;
            p.x=.22f;p.y=.45f;p.w=.06f;p.h=.25f;w.actors[w.actorCount++]=p;
            auto d=f.decide(w);
            if(d.eligible){const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,t,false),"accept composed");
                if(const auto* u=f.e->utterance(c.utterance)){composed=true;spoken.clear();
                    for(size_t q=0;q<u->count;++q){spoken+=fragmentText(static_cast<Fragment>(u->fragments[q]));spoken+=' ';}
                    std::cout<<"    t="<<t<<" says: "<<spoken<<std::endl;}}}
        check(composed,"no composed instruction");
        check(spoken.find("Uphill.")!=std::string::npos&&spoken.find("Oncoming truck.")!=std::string::npos&&spoken.find("Person on the left.")!=std::string::npos,
            "the instruction does not name the slope, the truck and the pedestrian together");
        check(spoken.find("Keep left.")!=std::string::npos,"no keep-left plan for an oncoming truck");});
    // Ground-consistent synthetic road users (focal ~0.96 image heights = 0.72 image widths,
    // camera 1.25 m up, horizon mid-image): a box at forward distance z and lateral offset X.
    auto place=[](WorldState& w,uint64_t id,int category,double z,double X,double tall,double wide,double imageShift=0){
        ActorObservation a;a.identity={IdentitySpace::Optical,id,0};a.captureTime=w.now;a.confidence=.9;a.relevant=true;a.category=category;
        a.h=float(.96*tall/z);a.w=float(.72*wide/z);a.x=float(.5+.72*X/z+imageShift)-a.w*.5f;a.y=float(.5+.96*1.25/z)-a.h;w.actors[w.actorCount++]=a;};
    auto said=[](Fixture& f,const WorldState& w,std::vector<int>& prompts){const auto d=f.decide(w);if(!d.eligible)return;
        const auto& c=d.ticket.candidate;prompts.push_back(int(c.prompt));
        check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");};
    auto has=[](const std::vector<int>& v,Prompt p){return std::find(v.begin(),v.end(),int(p))!=v.end();};
    test("turning is not crossing: a parked car on a left bend (Nov 4/6), with the turn rate known",[&]{
        // The learner drives a left arc (20 km/h, 15 deg/s); a car is parked 2.6 m inside the bend
        // 22 m ahead. In the turning car's frame it stays at the road edge, but turning slides it
        // across the image toward the path.
        auto run=[&](bool yaw){Fixture f(2);std::vector<int> p;const double v=5.56,omega=.26,R=v/omega,rin=R-2.6,phi=22/R;
            const double ox=rin*std::sin(phi),oy=R-rin*std::cos(phi);
            for(int i=5;i<=40;++i){const double t=i/10.,psi=omega*(t-.5),dx=ox-R*std::sin(psi),dy=oy-(R-R*std::cos(psi));
                const double z=dx*std::cos(psi)+dy*std::sin(psi),left=-dx*std::sin(psi)+dy*std::cos(psi);if(z<4)break;
                auto w=road(t);w.telemetry.speedKmh=20;w.yawRate=omega;w.yawKnown=yaw;place(w,801,2,z,-left,1.5,1.8);said(f,w,p);}
            return p;};
        check(has(run(false),Prompt::vehicleCrossingWait),"control: without the turn rate the bend should read as crossing");
        check(!has(run(true),Prompt::vehicleCrossingWait),"a parked car on a bend was called crossing");});
    test("a figure on a billboard is not a road user; a person standing there is",[&]{
        auto run=[&](bool ground){Fixture f(2);std::vector<int> p;
            // The phone measures its horizon (here mid-image). The billboard's printed figure is
            // 2.4 m tall with its feet 1 m above the road (just under the camera's 1.25 m).
            for(int i=5;i<=30;++i){const double t=i/10.,z=std::max(4.,18-7.8*(t-.5));auto w=road(t);w.telemetry.speedKmh=28;w.horizonMeasured=true;
                if(ground)place(w,802,0,z,-2.4,1.7,.5);
                else{ActorObservation a;a.identity={IdentitySpace::Optical,802,0};a.captureTime=t;a.confidence=.9;a.relevant=true;a.category=0;
                    a.h=float(.96*2.4/z);a.w=a.h*.3f;a.x=float(.5-.72*2.4/z)-a.w*.5f;a.y=float(.5+.96*.25/z)-a.h;w.actors[w.actorCount++]=a;}
                said(f,w,p);}
            return p;};
        const auto real=run(true),billboard=run(false);
        check(has(real,Prompt::personLeftSpace)||has(real,Prompt::personEaseCover)||has(real,Prompt::pedestrianSpace),"control: a person at the roadside got no instruction");
        for(Prompt q:{Prompt::personLeftSpace,Prompt::personRightSpace,Prompt::personEaseCover,Prompt::pedestrianSpace,Prompt::personCrossingStop,Prompt::brakeReadyStop,Prompt::clutchHoldBrake})
            check(!has(billboard,q),"a billboard figure produced a road-user instruction");});
    test("a person half hidden behind a car is still a road user, not a billboard",[&]{Fixture f(1);
        bool person=false;
        for(int i=5;i<=30;++i){const double t=i/10.,zc=std::max(6.,18-5.56*(t-.5));auto w=road(t);w.telemetry.speedKmh=20;w.horizonMeasured=true;
            place(w,805,2,zc,-1.2,1.2,1.7);  // a low car stopped ahead-left
            const auto car=w.actors[w.actorCount-1];
            // A person standing just beyond it: only the part above its roof shows, so the box bottom
            // is the car's roof line, rising in the image as the learner approaches.
            const double zp=zc+1;ActorObservation a;a.identity={IdentitySpace::Optical,806,0};a.captureTime=t;a.confidence=.9;a.relevant=true;a.category=0;
            a.y=float(.5+.96*(1.25-1.75)/zp);a.h=car.y-a.y;a.w=float(.72*.5/zp);a.x=float(.5+.72*-1.2/zp)-a.w*.5f;w.actors[w.actorCount++]=a;
            f.decide(w);for(size_t k=0;k<f.e->risk().riskCount();++k)person|=f.e->risk().riskAt(k).category==0;}
        check(person,"a person half hidden behind a car was dropped");});
    test("a person walking along the road edge ahead never gets 'Brake now'",[&]{Fixture f(1);std::vector<int> p;
        for(int i=5;i<=30;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=20;
            place(w,803,0,std::max(5.,24-5.56*(t-.5)+1.2*(t-.5)),1.9,1.7,.5);said(f,w,p);} // walking away from us at 1.2 m/s
        check(!has(p,Prompt::brakeReadyStop)&&!has(p,Prompt::clutchHoldBrake),"roadside pedestrian got a braking instruction");});
    test("a rider head-on in the middle of the road is oncoming, not 'Brake now' (Nov 6)",[&]{Fixture f(1);std::vector<int> p;
        for(int i=5;i<=25;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=16;
            place(w,804,3,std::max(6.,32-(4.44+7)*(t-.5)),.2,1.6,.7);said(f,w,p);}
        if(!has(p,Prompt::oncomingKeepLeft)){for(int x:p)std::cerr<<x<<' ';std::cerr<<std::endl;}
        check(has(p,Prompt::oncomingKeepLeft),"head-on rider not called oncoming");
        check(!has(p,Prompt::brakeReadyStop),"head-on rider got 'Brake now'");});
    test("a cropped, pitched-up camera (0.8.1 preview) sees a person in the lane as far as an uncropped level one",[&]{
        // One scene, two images: the uncropped 4:3 frame from a level camera, and the 0.8.1 preview
        // crop (the bottom 72 % of the frame: 0.665 widths and 1.228 heights per radian, optical
        // centre 0.31 down) from a phone pitched up 17 degrees, as on the Nov 8 drive. A person
        // stands in the lane 45 m ahead; the learner approaches at 40 km/h.
        // `horizon` is where it really is in the image; `believed` is what the app computes.
        struct Cam {CameraModel model;double horizon,believed;bool known;};
        auto run=[&](const Cam& c){Fixture f(1);std::vector<int> p;double firstZ=-1;
            for(int i=5;i<=40;++i){const double t=i/10.,z=45-11.1*(t-.5);if(z<5)break;auto w=road(t);w.telemetry.speedKmh=40;
                w.horizon=c.believed;w.horizonMeasured=true;if(c.known)w.camera=c.model;
                ActorObservation a;a.identity={IdentitySpace::Optical,807,0};a.captureTime=t;a.confidence=.9;a.relevant=true;a.category=0;
                a.h=float(c.model.fy*1.7/z);a.w=float(c.model.fx*.5/z);a.x=float(c.model.cx+c.model.fx*.3/z)-a.w*.5f;
                a.y=float(c.horizon+c.model.fy*1.25/z)-a.h;w.actors[w.actorCount++]=a;
                const size_t before=p.size();said(f,w,p);if(p.size()>before&&firstZ<0)firstZ=z;}
            return firstZ;};
        const CameraModel level{},crop{.665,1.228,.5,.306,true};
        const double up=std::tan(17*3.14159265358979/180);
        const double truth=.306+1.228*up;
        const double level4x3=run({level,.5,.5,true}),known=run({crop,truth,truth,true});
        // 0.8.1: the crop read as an uncropped frame, horizon from the 4:3 formula (0.5 + 0.96 tan).
        const double raw=run({crop,truth,.5+.96*up,false});
        std::cout<<"    first instruction: level 4:3 at "<<level4x3<<" m, cropped with its optics at "<<known<<" m, cropped read as 4:3 (0.8.1) at "<<raw<<" m"<<std::endl;
        check(level4x3>30&&known>30,"no early instruction for a person standing in the lane");
        check(std::abs(level4x3-known)<3,"the cropped camera finds the person at a different distance");
        check(raw<15,"control: 0.8.1's raw reading should only find the person close up");});
    // The camera's aim. A phone turned towards the driver looks to one side of where the car goes:
    // everything is shifted across the image (here by 6 % of its width, about 5 degrees).
    const double turned=.06;
    // One approach at 30 km/h to someone standing lateralM from the car's line, driving straight.
    // Returns whether anything urgent ("Brake now") was said, and where the coach placed the road
    // user sideways when it was 12 m ahead (0.5 = on the car's line; 0.06 per metre at that range).
    struct Passed {bool urgent=false;double sideAt12=-1;};
    auto pass=[&](Fixture& f,double& t,uint64_t id,int category,double lateralM,std::vector<int>& p,double yaw=0){
        Passed r;
        for(double z=60;z>6;z-=8.33*.1,t+=.1){auto w=road(t);w.yawKnown=true;w.yawRate=yaw;w.horizonMeasured=true;w.horizon=.5;
            place(w,id,category,z,lateralM,category==0?1.7:1.5,category==0?.5:1.7,turned);
            const auto d=f.decide(w);
            if(std::getenv("LD_TRACE")&&id==2101){const auto& q=f.e->risk().top();std::cout<<"      z="<<z<<" kind="<<int(q.kind)<<" L"<<q.level<<" d="<<q.distanceM<<" ttc="<<q.ttc<<" exp="<<q.expansion<<(d.eligible?" SPOKE ":" ")<<(d.eligible?int(d.ticket.candidate.prompt):0)<<(d.eligible&&d.ticket.candidate.urgent?" urgent":"")<<" blk="<<int(d.blocked)<<std::endl;}
            if(r.sideAt12<0&&z<=12&&f.e->risk().riskCount())r.sideAt12=f.e->risk().riskAt(0).center;
            if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;p.push_back(int(c.prompt));r.urgent|=c.urgent;
            check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
        t+=3;return r;};
    test("the car's heading in the image is learned from approaching road users (a phone turned in its mount)",[&]{
        Fixture f(1);double t=1;std::vector<int> p;
        check(!f.e->risk().headingKnown()&&f.e->risk().headingColumn(.5)==.5,"a heading was claimed without evidence");
        // Parked cars and people along both sides of a straight road, one after another. Two
        // cues must agree before the heading is taken: how they move as they come near, and where
        // they stood when still far away.
        const double sides[]={-3.5,3.,-2.5,4.,-3.,2.5,-4.,3.5};
        for(int k=0;k<4;++k)pass(f,t,2000+uint64_t(k),k%2?2:0,sides[k],p);
        check(!f.e->risk().headingKnown(),"four road users were taken as enough evidence");
        for(int k=4;k<12;++k)pass(f,t,2000+uint64_t(k),k%2?2:0,sides[k%8],p);
        check(f.e->risk().headingKnown(),"the heading was not learned from twelve road users passed on a straight road");
        const double learned=f.e->risk().headingColumn(.5);
        std::cout<<"    camera turned by "<<turned<<" of the image width; learned heading column "<<learned<<std::endl;
        check(std::abs(learned-(.5+turned))<.02,"the learned heading is off by more than 2 % of the image width");
        // Someone standing 1.5 m left of the car's line (the road edge) is placed there, to within
        // a quarter of a metre at 12 m, and is never "Brake now".
        const double truth=.5-.72*1.5/12;
        p.clear();const auto edge=pass(f,t,2100,0,-1.5,p);
        check(!p.empty()&&!edge.urgent&&std::abs(edge.sideAt12-truth)<.015,"with the heading learned, a person at the road edge was misplaced or got 'Brake now'");
        // ...and someone standing in the car's line is still braked for, urgently when it comes to it.
        p.clear();const auto line=pass(f,t,2101,0,0,p);
        if(std::getenv("LD_TRACE")){std::cout<<"      in the line:";for(int q:p)std::cout<<' '<<q;std::cout<<" urgent="<<line.urgent<<" side="<<line.sideAt12<<std::endl;}
        check(has(p,Prompt::brakeReadyStop)&&line.urgent&&std::abs(line.sideAt12-.5)<.015,"a person in the car's line was not braked for with the learned heading");
        // Control, as on Nov 8: with nothing learned, the turned camera puts the person at the edge
        // a metre nearer the car's line than they are (0.5 m from it instead of 1.5 m).
        Fixture g(1);double tg=1;std::vector<int> q;const auto control=pass(g,tg,2100,0,-1.5,q);
        std::cout<<"    person 1.5 m left of the car's line, at 12 m: true "<<truth<<", learned heading "<<edge.sideAt12<<", nothing learned "<<control.sideAt12<<std::endl;
        check(std::abs(control.sideAt12-(truth+turned))<.015,"control: the turned camera should misplace the edge person by the amount it is turned");});
    test("the heading is not learned in a bend, and one road user is not enough evidence",[&]{
        Fixture f(1);double t=1;std::vector<int> p;
        for(int k=0;k<8;++k)pass(f,t,2200+uint64_t(k),0,k%2?3.:-3.,p,.15);
        check(!f.e->risk().headingKnown(),"a heading was learned while turning");
        Fixture g(1);t=1;pass(g,t,2300,0,3.,p);
        check(!g.e->risk().headingKnown(),"one road user was taken as enough evidence");});
    // "You first" (0.8.5). A drive towards one road user: the learner's speed decides the distance.
    // towardMps: the road user's own speed towards the learner (oncoming traffic).
    struct Told {std::vector<int> prompts,fragments;std::vector<double> atM;bool brief=false,urgent=false,held=false;};
    auto approach=[&](Fixture& f,int category,double startM,double lateralM,double (*speedAt)(double),bool speedKnown=true,double appearM=1e9,double towardMps=0){
        Told r;double z=startM;const bool big=category==5||category==7;
        for(int i=5;i<=140;++i){const double t=i/10.,v=speedAt(t);z-=(v/3.6+towardMps)*.1;
            auto w=road(t);w.telemetry.speedKmh=v;w.telemetry.speedValid=speedKnown;
            if(z>4&&z<=appearM)place(w,900,category,z,lateralM,category==0?1.7:big?3.:1.5,category==0?.5:big?2.5:1.7);
            const auto d=f.decide(w);r.held|=f.e->risk().hold().skill>=0;if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;r.prompts.push_back(int(c.prompt));r.atM.push_back(z);r.urgent|=c.urgent;
            // What was said about the road user, fragment by fragment (composed sentences only).
            if(riskMayUse(c.prompt)){r.brief|=c.brief;if(const auto* u=f.e->utterance(CoachingEngine::spokenId(c)))for(size_t k=0;k<u->count;++k)r.fragments.push_back(u->fragments[k]);}
            check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
        return r;};
    // A record over five drives: level 1 slowed when told; level 2 mostly slowed before being told.
    auto trusted=[](Fixture& f,int skill,int level){std::vector<int> o(30,2);if(level>=2)for(int i=0;i<20;++i)o[size_t(i)]=1;
        check(f.e->learner().restore(skill,5,o.data(),o.size())&&f.e->learner().waitLevel(skill)==level,"seeded record has the wrong wait level");};
    // Instructions about road users (gear advice while slowing is not one).
    auto about=[](const Told& r){int n=0;for(int p:r.prompts)n+=riskMayUse(static_cast<Prompt>(p));return n;};
    auto says=[](const Told& r,Fragment f){return std::find(r.fragments.begin(),r.fragments.end(),int(f))!=r.fragments.end();};
    auto words=[](const Told& r){std::string s;for(int f:r.fragments){s+=fragmentText(static_cast<Fragment>(f));s+=' ';}return s;};
    const auto steady=+[](double){return 30.;};
    // Eases off from 30 to 18 km/h between 2 s and 5.3 s: well before the roadside person is reached.
    const auto easing=+[](double t){return t<2?30.:std::max(18.,30-3.6*(t-2));};
    test("you first: a new learner is told at once and in full, even when already slowing",[&]{
        Fixture f(1);const auto r=approach(f,0,70,-1.9,easing);
        std::cout<<"    new learner, already slowing: "<<words(r)<<std::endl;
        check(has(r.prompts,Prompt::personLeftSpace)&&!r.brief&&!r.held,"a new learner was not told about the person at the roadside");
        check(says(r,Fragment::personLeft)&&says(r,Fragment::easeOff)&&says(r,Fragment::giveSpace),"a new learner must get the whole instruction");
        check(f.e->learner().drive(0).before==1&&f.e->learner().drive(0).total()==1,"slowing before the instruction was not recorded once");
        check(f.e->learner().waitLevel(0)==0,"one drive must not earn any waiting");});
    test("you first: a learner who slows when told hears the instruction without the slowing already done",[&]{
        Fixture f(1);trusted(f,0,1);f.e->learner().newDrive();const auto r=approach(f,0,70,-1.9,easing);
        std::cout<<"    slows when told, already slowing: "<<words(r)<<std::endl;
        check(about(r)==1&&r.brief&&!r.held,"the shortened instruction was not given exactly once");
        check(says(r,Fragment::personLeft)&&says(r,Fragment::giveSpace),"the shortened instruction must still name the person and the space to give");
        check(!says(r,Fragment::easeOff)&&!says(r,Fragment::brakeGently),"the learner was told to slow down after slowing down");
        check(f.e->learner().drive(0).before==1,"acting first was not credited");});
    test("you first: a learner who usually slows before being told is left alone once they have slowed",[&]{
        Fixture f(1);trusted(f,0,2);f.e->learner().newDrive();const auto r=approach(f,0,70,-1.9,easing);
        check(about(r)==0,"the coach spoke although the learner had already slowed for the person");
        check(r.held,"the unsaid instruction was not reported for the screen");
        check(f.e->learner().drive(0).before==1,"acting first was not credited");});
    test("you first: any learner who has not slowed is told exactly as a new learner is",[&]{
        Fixture plain(1);const auto a=approach(plain,0,70,-1.9,steady);
        check(!a.prompts.empty()&&says(a,Fragment::easeOff),"no full instruction for a person near the path");
        for(int level:{1,2}){Fixture f(1);trusted(f,0,level);f.e->learner().newDrive();const auto b=approach(f,0,70,-1.9,steady);
            check(!b.prompts.empty()&&a.prompts[0]==b.prompts[0]&&std::abs(a.atM[0]-b.atM[0])<.5,"a trusted learner who did not slow was told something else, or later");
            check(!b.brief&&!b.held&&a.fragments==b.fragments,"a trusted learner who did not slow was told less");
            check(f.e->learner().drive(0).none==1&&f.e->learner().drive(0).before==0,"no slowing after the instruction was not recorded");}});
    test("you first: easing off and then speeding up again is not acting first",[&]{
        // Eases from 30 to 24 km/h, then accelerates past 30 again towards the person at the roadside.
        for(int level:{1,2}){Fixture f(1);trusted(f,0,level);f.e->learner().newDrive();
            const auto r=approach(f,0,70,-1.9,+[](double t){return t<2?30.:t<3.67?30-3.6*(t-2):std::min(34.,24+3.6*(t-3.67));});
            check(about(r)>0&&!r.brief&&!r.held&&says(r,Fragment::easeOff),"the learner sped up again and was not told in full");
            check(f.e->learner().drive(0).before==0,"speeding up again was credited as acting first");}});
    test("you first: Brake now is never held or shortened, whatever the record",[&]{
        // A person appears in the lane 13 m ahead at 30 km/h while the learner is only easing off.
        Fixture f(1);trusted(f,0,2);
        const auto r=approach(f,0,70,.2,+[](double t){return t<2?30.:std::max(24.,30-1.8*(t-2));},true,13);
        check(!r.prompts.empty()&&r.urgent&&!r.brief&&!r.held,"an urgent instruction was held or shortened");});
    test("you first: braking for someone in the lane, early, late, and not enough",[&]{
        // Early: brakes from 30 km/h at 1.2 m/s^2 from 50 m and stops ~21 m short of a person standing
        // in the lane. No instruction ever becomes due, for anyone; the record still credits it.
        const auto early=+[](double t){return t<1.94?30.:std::max(0.,30-4.32*(t-1.94));};
        for(int level:{0,2}){Fixture f(1);if(level)trusted(f,0,level);f.e->learner().newDrive();const auto r=approach(f,0,62,.2,early);
            check(about(r)==0,"the coach spoke about the person although the learner was already stopping well short");
            check(f.e->learner().drive(0).before==1,"stopping in time was not credited");}
        // Late: keeps 30 km/h past "ease off, cover the brake" (43 m), then brakes at 1.0 m/s^2 from
        // 41 m: enough to stop 6 m short of the person.
        const auto late=+[](double t){return t<3?30.:std::max(0.,30-3.6*(t-3));};
        Fixture n(1);const auto full=approach(n,0,62,.2,late);
        std::cout<<"    new learner, braking late: "<<words(full)<<std::endl;
        check(says(full,Fragment::easeOff)&&says(full,Fragment::brakeGently)&&says(full,Fragment::readyToStop)&&!full.urgent,"a new learner braking late was not told both instructions in full");
        Fixture h(1);trusted(h,0,1);const auto named=approach(h,0,62,.2,late);
        std::cout<<"    slows when told, braking late: "<<words(named)<<std::endl;
        check(says(named,Fragment::easeOff),"not having slowed yet, the learner must hear the first instruction in full");
        check(named.brief&&says(named,Fragment::readyToStop)&&!says(named,Fragment::brakeGently)&&!named.urgent,"a learner already braking enough was told to brake");
        Fixture q(1);trusted(q,0,2);const auto quiet=approach(q,0,62,.2,late);
        check(says(quiet,Fragment::easeOff)&&quiet.held&&!says(quiet,Fragment::brakeGently)&&!says(quiet,Fragment::readyToStop)&&!quiet.urgent,"a strong record braking enough still got the braking instruction");
        // Not enough: slows early to 14 km/h but keeps rolling at the person. Told in full, whatever the record.
        Fixture g(1);trusted(g,0,2);
        const auto roll=approach(g,0,62,.2,+[](double t){return t<2?30.:std::max(14.,30-7.2*(t-2));});
        std::cout<<"    strong record, rolling on: "<<words(roll)<<std::endl;
        check(about(roll)>0&&says(roll,Fragment::brakeGently)&&says(roll,Fragment::readyToStop),"rolling up to a person in the lane was left without 'brake gently, be ready to stop'");});
    test("you first: oncoming traffic is judged by easing off, and a bus is always spoken about",[&]{
        // Head-on at 25 km/h on a narrow road. The learner eases off from 30 to 18 km/h either long
        // before the coach could have noticed it (no credit), or once it is in view.
        const auto before=+[](double t){return std::max(18.,30-3.6*(t-.5));};
        const auto inView=+[](double t){return t<5?30.:std::max(18.,30-3.6*(t-5));};
        Fixture plain(1);const auto full=approach(plain,5,150,2.2,inView,true,1e9,7);
        check(about(full)==1&&says(full,Fragment::oncomingBus)&&says(full,Fragment::keepLeft)&&says(full,Fragment::easeOff),"a new learner was not told in full about the bus");
        for(int level:{1,2}){
            Fixture f(1);trusted(f,3,level);f.e->learner().newDrive();const auto bus=approach(f,5,150,2.2,inView,true,1e9,7);
            std::cout<<"    record level "<<level<<", eased off for the bus: "<<words(bus)<<std::endl;
            check(about(bus)==1&&!bus.held&&bus.brief&&says(bus,Fragment::oncomingBus)&&says(bus,Fragment::keepLeft)&&!says(bus,Fragment::easeOff),"the bus must get 'keep left' without the easing off already done");
            check(std::abs(bus.atM[0]-full.atM[0])<1,"the bus instruction came later than a new learner's");
            Fixture g(1);trusted(g,3,level);const auto far=approach(g,5,150,2.2,before,true,1e9,7);
            check(about(far)==1&&!far.brief&&!far.held&&says(far,Fragment::easeOff),"slowing before the bus was in view was taken as slowing for it");
            Fixture c(1);trusted(c,3,level);const auto car=approach(c,2,150,2.2,inView,true,1e9,7);
            if(level==1)check(about(car)==1&&car.brief&&says(car,Fragment::keepLeft)&&!says(car,Fragment::easeOff),"an oncoming car: the shortened instruction was not given");
            else check(about(car)==0&&car.held,"an oncoming car, strong record, eased off: the instruction was not left unsaid, or came late");}});
    test("a rider seen from behind is a two-wheeler ahead; someone standing in the lane stays a person",[&]{
        // The detector shows only the rider's body ("person"). Riding the learner's way at 20 km/h
        // while the learner does 30: the box grows a third as fast as a standing person's would.
        // (Beyond ~18 m the difference is too small to measure, so the first, far instruction may
        // still say "person".)
        Fixture f(1);const auto rider=approach(f,0,34,.2,steady,true,1e9,-5.56);
        std::cout<<"    rider seen as 'person', keeping pace: "<<words(rider)<<std::endl;
        check(about(rider)>=2&&says(rider,Fragment::twoWheelerAhead),"a rider keeping pace ahead was never called a two-wheeler");
        Fixture g(1);const auto standing=approach(g,0,60,.2,steady);
        check(says(standing,Fragment::personAhead)&&!says(standing,Fragment::twoWheelerAhead),"someone standing in the lane was called a two-wheeler");
        // Someone walking the learner's way at 5 km/h is a person too.
        Fixture h(1);const auto walker=approach(h,0,50,.2,steady,true,1e9,-1.4);
        check(says(walker,Fragment::personAhead)&&!says(walker,Fragment::twoWheelerAhead),"a pedestrian walking ahead was called a two-wheeler");});
    test("a three-wheeler the detector calls a truck is a vehicle; a bus keeps its name however small it reads",[&]{
        auto oncoming=[&](int category,double tall,double wide){Fixture f(1);std::string s;
            for(int i=5;i<=60;++i){const double t=i/10.,z=90-(5.56+7)*(t-.5);if(z<6)break;auto w=road(t);w.telemetry.speedKmh=20;w.horizonMeasured=true;
                place(w,950,category,z,2.2,tall,wide);const auto d=f.decide(w);if(!d.eligible)continue;
                const auto& c=d.ticket.candidate;if(const auto* u=f.e->utterance(CoachingEngine::spokenId(c)))for(size_t k=0;k<u->count;++k){s+=fragmentText(static_cast<Fragment>(u->fragments[k]));s+=' ';}
                check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
            return s;};
        // A real bus on a slope, or with the horizon a little off, reads as small as this.
        const auto small=oncoming(7,1.7,1.4),low=oncoming(5,1.7,1.4),bus=oncoming(5,3.1,2.5);
        std::cout<<"    1.7 m tall 'truck': "<<small<<"\n    'bus' reading 1.7 m: "<<low<<"\n    3.1 m tall bus: "<<bus<<std::endl;
        check(small.find("Oncoming vehicle")!=std::string::npos&&small.find("truck")==std::string::npos,"a 1.7 m tall three-wheeler was called a truck");
        check(low.find("Oncoming bus")!=std::string::npos,"a bus that reads small lost its name");
        check(bus.find("Oncoming bus")!=std::string::npos,"a real bus lost its name");});
    test("a car overtaking and cutting in from the edge of the frame is a vehicle ahead, not 'crossing'",[&]{
        // Nov 8, 11:26, as the detector saw it: the car comes into view on the right, cut off by the
        // frame, pulls in ahead and draws away. The learner holds 27 km/h.
        const double box[][4]={{.791,1.,.530,.993},{.757,.999,.531,.993},{.718,.999,.538,.989},{.684,1.,.553,.928},{.657,.997,.562,.930},
            {.640,.985,.573,.911},{.615,.940,.585,.890},{.590,.895,.595,.870},{.570,.850,.603,.852},{.552,.812,.610,.836},{.538,.780,.615,.822},{.526,.752,.620,.810}};
        Fixture f(1);std::vector<int> p;bool urgent=false;
        for(int i=0;i<20;++i){auto w=road(1+i*.125);w.telemetry.speedKmh=27;
            if(i>=4&&i<16){const auto& b=box[i-4];ActorObservation a;a.identity={IdentitySpace::Optical,940,0};a.captureTime=w.now;a.confidence=.8;a.relevant=true;a.category=2;
                a.x=float(b[0]);a.w=float(b[1]-b[0]);a.y=float(b[2]);a.h=float(b[3]-b[2]);w.actors[w.actorCount++]=a;}
            const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;p.push_back(int(c.prompt));urgent|=c.urgent;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
        check(!has(p,Prompt::vehicleCrossingWait)&&!urgent,"a car overtaking and cutting in was called crossing, or got 'Brake now'");});
    test("traffic coming round a left-hand bend is oncoming, not 'crossing'",[&]{
        // The learner follows a left-hand bend of 40 m radius at 28 km/h on the inside; a car comes
        // the other way on the outside at 36 km/h. It first shows left of where the learner's car
        // points and slides right as it comes. The phone's gyroscope gives the turn rate.
        Fixture f(1);std::vector<int> p;bool urgent=false;const double R=40,R2=43.2,v=7.78,omega=v/R,omega2=10/R2,phi0=60/R;
        for(int i=5;i<=60;++i){const double t=i/10.,psi=omega*(t-.5),phi=phi0-omega2*(t-.5);
            const double dx=R2*std::sin(phi)-R*std::sin(psi),dy=(R-R2*std::cos(phi))-(R-R*std::cos(psi));
            const double z=dx*std::cos(psi)+dy*std::sin(psi),left=-dx*std::sin(psi)+dy*std::cos(psi);if(z<5)break;
            auto w=road(t);w.telemetry.speedKmh=28;w.yawRate=omega;w.yawKnown=true;place(w,941,2,z,-left,1.5,1.8);
            const auto d=f.decide(w);
            if(std::getenv("LD_TRACE")){const auto& r=f.e->risk().top();std::cout<<"      t="<<t<<" z="<<z<<" left="<<left<<" kind="<<int(r.kind)<<" L"<<r.level<<" c="<<r.center<<" inw="<<r.inwardRate<<" raw="<<r.rawInward<<" exp="<<r.expansion<<" d="<<r.distanceM<<" ttc="<<r.ttc<<(d.eligible?" SPOKE ":" ")<<(d.eligible?int(d.ticket.candidate.prompt):0)<<(d.eligible&&d.ticket.candidate.urgent?" urgent":"")<<std::endl;}
            if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;p.push_back(int(c.prompt));urgent|=c.urgent&&c.prompt!=Prompt::oncomingKeepLeft;
            check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
        check(!has(p,Prompt::vehicleCrossingWait)&&!has(p,Prompt::brakeReadyStop)&&!urgent,"a car coming round a bend was called crossing, or braked for");
        check(has(p,Prompt::oncomingKeepLeft),"a car coming round a bend got no 'keep left'");});
    test("the tracker going from one parked vehicle to the next is not a vehicle crossing",[&]{
        // Two vehicles parked on the left verge, 3 m and 1.9 m from the learner's line, 9 m apart.
        // The detector reports only the nearer one, edging out a little, until it misses it; then
        // the same track continues on the one beyond (Nov 8, 8:45). 25 km/h.
        Fixture f(1);std::vector<int> p;bool urgent=false;
        for(int i=5;i<=40;++i){const double t=i/10.,z=30-6.94*(t-.5);if(z<6)break;auto w=road(t);w.telemetry.speedKmh=25;
            if(t<2.2)place(w,942,7,z,-3.+.5*(t-.5),1.9,1.9);else place(w,942,2,z+9,-1.9,1.5,1.7);
            const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;p.push_back(int(c.prompt));urgent|=c.urgent;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
        check(!has(p,Prompt::vehicleCrossingWait)&&!urgent,"a tracker jump between parked vehicles was called crossing");});
    test("a vehicle really crossing close ahead is still 'Vehicle crossing'",[&]{
        // Nov 4, 4:46: an autorickshaw crosses right to left 9 m ahead at 3 m/s while the learner
        // comes up to the junction at 18 km/h.
        Fixture f(1);std::vector<int> p;
        for(int i=5;i<=30;++i){const double t=i/10.,z=16-5.*(t-.5);if(z<5)break;auto w=road(t);w.telemetry.speedKmh=18;
            place(w,943,2,z,4.5-3.*(t-.5),1.7,2.6);said(f,w,p);}
        check(has(p,Prompt::vehicleCrossingWait),"a vehicle crossing close ahead was not called crossing");});
    test("a truck coming out of a side road, first seen cut off by the frame edge, is 'Vehicle crossing'",[&]{
        // Nov 4, 0:40 and 3:54: the learner comes up to a T-junction at 10 km/h; a small truck
        // comes from the left along the other road at 3 m/s and crosses a few metres ahead. The
        // detector's first boxes of it are cut off by the left edge of the frame.
        Fixture f(1);std::vector<int> p;double at=-1,side=0;
        for(int i=5;i<=40;++i){const double t=i/10.,z=std::max(4.5,9-2.78*(t-.5)),X=-7+3.*(t-.5);if(X>3)break;auto w=road(t);w.telemetry.speedKmh=10;
            place(w,953,7,z,X,1.9,3.5);auto& a=w.actors[w.actorCount-1];
            if(a.x<0){a.w+=a.x;a.x=0;}
            if(a.w<.03f){--w.actorCount;}
            const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;p.push_back(int(c.prompt));if(c.prompt==Prompt::vehicleCrossingWait&&at<0){at=t;side=X;}
            check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
        std::cout<<"    'Vehicle crossing' at "<<at<<" s, the truck "<<-side<<" m left of the learner's line"<<std::endl;
        check(has(p,Prompt::vehicleCrossingWait)&&side<0,"a truck crossing from a side road was not called crossing before it reached the learner's line");});
    test("long range with the phone leaning back: hazards 70-100 m ahead are acted on early",[&]{
        // The Nov 12 mount: leaning back about 22 degrees, the 0.8.1 preview crop (focal 0.665 image
        // widths and 1.228 heights, optical centre a third of the way down), horizon at 0.82. The
        // learner holds 40 km/h. Each hazard is placed with that camera's own geometry.
        const double fx=.665,fy=1.228,H=.82;
        auto put=[&](WorldState& w,uint64_t id,int category,double z,double X,double tall,double wide){
            ActorObservation a;a.identity={IdentitySpace::Optical,id,0};a.captureTime=w.now;a.confidence=.8;a.relevant=true;a.category=category;
            a.h=float(fy*tall/z);a.w=float(fx*wide/z);a.x=float(.5+fx*X/z)-a.w*.5f;a.y=float(H+fy*1.25/z)-a.h;w.actors[w.actorCount++]=a;};
        // When the first road-user sentence about it was said, and how far away it was then.
        auto first=[&](int category,double z0,double X,double tall,double wide,double ownMps,const char* words){
            Fixture f(1);double at=-1;std::string what;
            for(int i=5;i<=200&&at<0;++i){const double t=i/10.,z=z0-(11.1+ownMps)*(t-.5);if(z<4)break;
                auto w=road(t);w.telemetry.speedKmh=40;w.horizon=H;w.horizonMeasured=true;w.camera=CameraModel{fx,fy,.5,.306,true};
                put(w,960+uint64_t(category),category,z,X,tall,wide);
                const auto d=f.decide(w);if(!d.eligible)continue;
                const auto& c=d.ticket.candidate;std::string s;
                if(const auto* u=f.e->utterance(CoachingEngine::spokenId(c)))for(size_t k=0;k<u->count;++k){s+=fragmentText(static_cast<Fragment>(u->fragments[k]));s+=' ';}
                else s=promptText(c.prompt);
                check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");
                if(riskMayUse(c.prompt)){at=z;what=s;}}
            std::cout<<"    "<<words<<": first said at "<<at<<" m: "<<what<<std::endl;
            return std::make_pair(at,what);};
        // A bus coming the other way in its own lane (2.8 m right of the learner's line) at 36 km/h.
        const auto bus=first(5,100,2.8,3.1,2.5,10,"oncoming bus");
        check(bus.first>=35&&bus.second.find("Oncoming bus")!=std::string::npos,"an oncoming bus seen from 100 m was not named while still 35 m away");
        // A person standing in the learner's lane.
        const auto person=first(0,90,-.3,1.7,.5,0,"person in the lane");
        check(person.first>=40&&person.second.find("Person ahead")!=std::string::npos,"a person standing in the lane 90 m ahead was not spoken about while still 40 m away");
        // A lorry stopped in the learner's lane.
        const auto lorry=first(7,100,0,3.2,2.5,0,"stopped lorry in the lane");
        check(lorry.first>=35&&(lorry.second.find("Truck ahead")!=std::string::npos||lorry.second.find("Vehicle ahead")!=std::string::npos),
            "a lorry stopped in the lane 100 m ahead was not spoken about while still 35 m away");
        // A motorcycle stopped in the lane (waiting to turn).
        const auto bike=first(3,80,.2,1.5,.8,0,"stopped two-wheeler in the lane");
        check(bike.first>=30&&bike.second.find("Two-wheeler ahead")!=std::string::npos,"a two-wheeler stopped in the lane 80 m ahead was not spoken about while still 30 m away");});
    test("the speech budget never holds back a person in the path, however busy the road",[&]{
        // Three buses coming the other way have just been spoken about, one every 21 s (the
        // minute's budget is full); then a person is standing in the lane 60 m ahead at 40 km/h.
        Fixture f(1);int keepLeft=0;double personAt=-1;
        for(int i=5;i<=600&&personAt<0;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=40;
            for(int k=0;k<3;++k){const double z=90-(11.1+10)*(t-(1+21*k));if(t>=1+21*k&&z>6)place(w,970+uint64_t(k),5,z,2.8,3.1,2.5);}
            if(t>=50){const double z=60-11.1*(t-50);if(z>5)place(w,975,0,z,.1,1.7,.5);}
            const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");
            if(c.prompt==Prompt::oncomingKeepLeft)++keepLeft;
            else if(t>=50&&(c.prompt==Prompt::personEaseCover||c.prompt==Prompt::brakeReadyStop))personAt=60-11.1*(t-50);}
        std::cout<<"    'keep left' "<<keepLeft<<" time(s); then the person in the lane told about at "<<personAt<<" m"<<std::endl;
        check(keepLeft>=3,"control: the three buses were not all spoken about, so the budget was not full");
        check(personAt>=45,"after a busy minute the person in the lane was told about late, or not at all");});
    test("something coming toward the learner on their left is never told 'keep left'",[&]{
        // A motorcycle comes toward the learner at 25 km/h on a straight road, 2 m to the LEFT of
        // their line (riding the wrong way along the edge, or a man walking there taken for one);
        // the learner holds 30 km/h. Control: the same 2 m to the right is a close pass, "keep left".
        auto run=[&](double side){Fixture f(1);std::vector<int> p;
            for(int i=5;i<=80;++i){const double t=i/10.,z=55-(8.33+6.94)*(t-.5);if(z<5)break;auto w=road(t);
                place(w,980,3,z,side,1.6,.7);said(f,w,p);}
            return p;};
        const auto left=run(-2.),right=run(2.);
        std::cout<<"    on the left:";for(int q:left)std::cout<<' '<<q;std::cout<<"\n    on the right:";for(int q:right)std::cout<<' '<<q;std::cout<<std::endl;
        check(has(right,Prompt::oncomingKeepLeft),"control: a two-wheeler passing close on the right got no 'keep left'");
        check(!has(left,Prompt::oncomingKeepLeft),"'keep left' was said about a road user on the learner's left");});
    test("a person and a rider standing together are told about once",[&]{
        // Someone standing at the edge of the lane and a rider stopped beside them, 3 m further on.
        Fixture f(1);int braking=0,urgent=0;double last=-1,gap=1e9;
        for(int i=5;i<=70;++i){const double t=i/10.,z=50-5.56*(t-.5);if(z<8)break;auto w=road(t);w.telemetry.speedKmh=20;
            place(w,944,0,z,-.4,1.7,.5);place(w,945,3,z+3,.5,1.6,.7);
            const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");
            if(c.prompt==Prompt::brakeReadyStop&&!c.urgent){++braking;if(last>=0)gap=std::min(gap,t-last);last=t;}
            urgent+=c.urgent;}
        std::cout<<"    'brake gently' for the two: "<<braking<<" time(s); shortest gap "<<(gap<1e8?gap:0)<<" s; urgent "<<urgent<<std::endl;
        check(braking>=1&&gap>=8,"the same instruction was repeated for a rider beside the person within 8 s");});
    test("'Brake now' is said once: not again for the next person while it is being said",[&]{
        // Nov 5, 5:15: two people on either side of a narrow lane come into view 9 m ahead at
        // 15 km/h; the detector loses the first of them after half a second.
        Fixture f(1);std::vector<double> urgent;
        for(int i=5;i<=30;++i){const double t=i/10.,z=9-4.17*(t-.5);if(z<2.5)break;auto w=road(t);w.telemetry.speedKmh=15;
            if(t<1.1)place(w,946,0,z,-.5,1.7,.5);
            place(w,947,0,z+.5,.6,1.7,.5);
            const auto d=f.decide(w);
            if(std::getenv("LD_TRACE")){const auto& r=f.e->risk().top();std::cout<<"      t="<<t<<" z="<<z<<" top id="<<r.id<<" kind="<<int(r.kind)<<" L"<<r.level<<" d="<<r.distanceM<<" ttc="<<r.ttc<<" exp="<<r.expansion<<" n="<<f.e->risk().riskCount()<<(d.eligible?" SPOKE":"")<<(d.eligible&&d.ticket.candidate.urgent?" urgent":"")<<std::endl;}
            if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");
            if(c.urgent)urgent.push_back(t);}
        std::cout<<"    'Brake now' at:";for(double t:urgent)std::cout<<' '<<t;std::cout<<std::endl;
        check(!urgent.empty(),"control: nobody was braked for");
        check(urgent.size()==1||urgent[1]-urgent[0]>=3,"'Brake now' was said again within the sentence for the next person");});
    test("a pedestrian first seen as the car is about to pass them gets no late sentence",[&]{
        // Nov 8, 8:38: a man walking on the verge is first seen 7.5 m ahead at 25 km/h, 1.05 m left
        // of the learner's line: passed in about a second, and not on a collision course.
        Fixture f(1);std::vector<int> p;
        for(int i=5;i<=30;++i){const double t=i/10.,z=7.5-6.94*(t-1.5);auto w=road(t);w.telemetry.speedKmh=25;
            if(t>=1.5&&z>2.5)place(w,948,0,z,-1.05,1.7,.5);
            said(f,w,p);}
        check(p.empty(),"a sentence was started about a pedestrian who was about to be passed");
        // The same man seen in good time is spoken about.
        Fixture g(1);std::vector<int> q;
        for(int i=5;i<=60;++i){const double t=i/10.,z=40-6.94*(t-.5);if(z<2.5)break;auto w=road(t);w.telemetry.speedKmh=25;place(w,949,0,z,-1.05,1.7,.5);said(g,w,q);}
        check(!q.empty(),"control: a pedestrian at the edge of the path seen 40 m ahead got no instruction");});
    test("traffic coming the other way is told once: a steady speed past it is not 'unanswered'",[&]{
        // A car passes on the other side of a two-lane road while the learner holds 30 km/h.
        Fixture f(1);int said=0;bool urgent=false;
        for(int i=5;i<=120;++i){const double t=i/10.,z=110-(8.33+8)*(t-.5);if(z<5)break;auto w=road(t);
            place(w,970,2,z,2.6,1.5,1.7);const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");
            if(riskMayUse(c.prompt)){++said;urgent|=c.urgent;}}
        check(said==1&&!urgent,"a passing oncoming car was spoken about more than once, or urgently");});
    test("a pedestrian in front of a distant motorcycle is on foot; a rider on a bike is a two-wheeler",[&]{
        // What is said about the person-shaped box, with a two-wheeler box behind its legs.
        auto drive=[&](bool riding){Fixture f(1);std::string s;
            for(int i=5;i<=60;++i){const double t=i/10.,z=45-5.56*(t-.5);if(z<6)break;auto w=road(t);w.telemetry.speedKmh=20;
                place(w,980,0,z,.2,1.7,.5);
                // On the bike: a box as wide as the bike, reaching the rider's feet. Far behind: a small
                // box 40 m further up the road that happens to sit behind the pedestrian's legs.
                if(riding)place(w,981,3,z,.2,1.1,.7);else place(w,981,3,z+40,.2+.9*(z+40)/z*0+.0,1.1,.7);
                const auto d=f.decide(w);if(!d.eligible)continue;
                const auto& c=d.ticket.candidate;if(const auto* u=f.e->utterance(CoachingEngine::spokenId(c)))for(size_t k=0;k<u->count;++k){s+=fragmentText(static_cast<Fragment>(u->fragments[k]));s+=' ';}
                check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
            return s;};
        const auto walking=drive(false),riding=drive(true);
        std::cout<<"    on foot, a motorcycle far behind: "<<walking<<"\n    on the bike: "<<riding<<std::endl;
        check(walking.find("Person ahead")!=std::string::npos&&walking.find("wo-wheeler")==std::string::npos,"a pedestrian in front of a distant motorcycle was taken for its rider");
        check(riding.find("wo-wheeler")!=std::string::npos&&riding.find("Person")==std::string::npos,"a rider on a bike was called a person");});
    test("people standing together are told about once, not once each",[&]{
        // Three people at the edge of the lane, 2 m apart along the road; the learner holds 20 km/h.
        Fixture f(1);int braking=0,urgent=0;double last=-1,gap=1e9;
        for(int i=5;i<=70;++i){const double t=i/10.,z=50-5.56*(t-.5);if(z<8)break;auto w=road(t);w.telemetry.speedKmh=20;
            place(w,990,0,z,.5,1.7,.5);place(w,991,0,z+2,.9,1.7,.5);place(w,992,0,z+4,.1,1.7,.5);
            const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");
            if(c.prompt==Prompt::brakeReadyStop&&!c.urgent){++braking;if(last>=0)gap=std::min(gap,t-last);last=t;}
            urgent+=c.urgent;}
        std::cout<<"    'brake gently' for the group: "<<braking<<" time(s); shortest gap "<<(gap<1e8?gap:0)<<" s; urgent "<<urgent<<std::endl;
        check(braking>=1&&gap>=8,"the same instruction was repeated for the next person of a group within 8 s");});
    test("a road user spoken about as passing by who is then in the way gets the other instruction",[&]{
        // A rider comes the other way in their own lane ("keep left"), then pulls across into the
        // learner's lane and stops there, about 20 m ahead.
        Fixture f(1);std::vector<std::string> said;
        for(int i=5;i<=110;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=15;
            // Approaching at 7 m/s until 3.5 s, then standing still while the learner rolls on at 15 km/h.
            const double z=t<3.5?55-(4.17+7)*(t-.5):55-(4.17+7)*3-4.17*(t-3.5);if(z<5)break;
            const double side=t<3.5?1.9:std::max(.2,1.9-1.7*(t-3.5));
            place(w,995,3,z,side,1.6,.7);const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;std::string s;
            if(const auto* u=f.e->utterance(CoachingEngine::spokenId(c)))for(size_t k=0;k<u->count;++k){s+=fragmentText(static_cast<Fragment>(u->fragments[k]));s+=' ';}
            said.push_back(s);check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");}
        for(const auto& s:said)std::cout<<"    "<<s<<std::endl;
        int keepLeft=-1,ahead=-1;
        for(size_t k=0;k<said.size();++k){if(keepLeft<0&&said[k].find("Keep left")!=std::string::npos)keepLeft=int(k);
            if(said[k].find("Two-wheeler ahead")!=std::string::npos)ahead=int(k);}
        check(keepLeft>=0&&ahead>keepLeft,"a rider who stopped in the lane after 'keep left' was not spoken about again");});
    test("someone in the path is not kept waiting behind 'keep left' about someone passing by",[&]{
        // A rider comes the other way and gets "keep left"; 1.6 s later a person is standing in the
        // lane 22 m ahead. At 20 km/h that is under four seconds away.
        Fixture f(1);double keepLeftAt=-1,personAt=-1;bool urgent=false;
        for(int i=5;i<=120&&personAt<0;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=20;
            const double rider=60-(5.56+7)*(t-.5);if(rider>4)place(w,960,3,rider,2.2,1.6,.7);
            if(keepLeftAt>=0&&t>=keepLeftAt+1.6){const double z=22-5.56*(t-keepLeftAt-1.6);if(z>4)place(w,961,0,z,.2,1.7,.5);}
            const auto d=f.decide(w);if(!d.eligible)continue;
            const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept");
            if(c.prompt==Prompt::oncomingKeepLeft&&keepLeftAt<0)keepLeftAt=t;
            else if(c.prompt==Prompt::brakeReadyStop||c.prompt==Prompt::personEaseCover){personAt=t;urgent=c.urgent;}}
        check(keepLeftAt>=0,"the oncoming rider was not coached");
        std::cout<<"    keep left at "<<keepLeftAt<<" s; person in the lane coached "<<personAt-keepLeftAt<<" s later"<<std::endl;
        check(personAt>=0&&personAt-keepLeftAt<2.6&&!urgent,"the person in the lane waited behind 'keep left' until it was urgent");});
    test("you first: without a measured speed nothing is held and nothing is recorded",[&]{
        Fixture f(1);trusted(f,0,2);f.e->learner().newDrive();const auto r=approach(f,0,70,.2,steady,false);
        check(!r.prompts.empty()&&!r.brief&&!r.held,"unknown speed: the coach waited or shortened");
        check(f.e->learner().drive(0).total()==0,"an outcome was recorded without a measured speed");});
    test("you first: the record earns waiting slowly, survives storage and can be forgotten",[]{
        LearnerRecord r;check(r.waitLevel(0)==0,"an empty record waits");
        for(int i=0;i<30;++i)r.note(0,LearnerOutcome::Before);
        check(r.waitLevel(0)==0,"one drive earned waiting");                      // all in one drive
        int stored[31]{};check(r.outcomes(0,stored,31)==31&&stored[0]==1&&stored[1]==1,"outcomes not exported");
        LearnerRecord s;check(s.restore(0,4,stored+1,30)&&s.waitLevel(0)==2,"restored record lost its level");
        check(s.restore(1,2,stored+1,12)&&s.waitLevel(1)==1,"twelve responses over two drives should earn level 1");
        int mixed[30];for(int i=0;i<30;++i)mixed[i]=i%2?3:2;                      // half the time no slowing
        check(s.restore(2,6,mixed,30)&&s.waitLevel(2)==0,"a mixed record earned waiting");
        const int bad[1]{7};check(!s.restore(3,1,bad,1),"an invalid outcome was restored");
        s.enableWaiting(false);check(s.waitLevel(0)==0,"waiting switched off still waits");s.enableWaiting(true);
        // A test video or a simulation (manual speed): the record is neither used nor added to.
        s.measuredDriving(false);s.note(0,LearnerOutcome::NoResponse);
        check(s.waitLevel(0)==0&&s.recent(0).none==0&&s.drive(0).total()==0,"a test video used or changed the learner's record");
        s.measuredDriving(true);check(s.waitLevel(0)==2,"the record did not come back for real driving");
        s.newDrive();s.note(0,LearnerOutcome::NoResponse);check(s.drive(0).none==1&&s.drives(0)==5,"this drive's tally or drive count wrong");
        // This drive's outcomes count from the next drive: the coach never changes how it speaks mid-drive.
        LearnerRecord m;int told[30];for(int& o:told)o=2;check(m.restore(0,5,told,30)&&m.waitLevel(0)==1,"a record of slowing when told is level 1");
        m.newDrive();for(int i=0;i<12;++i)m.note(0,LearnerOutcome::Before);
        check(m.waitLevel(0)==1&&m.earnedLevel(0)==2,"the level changed in the middle of a drive");
        m.newDrive();check(m.waitLevel(0)==2,"the next drive did not start at the earned level");
        check(s.json().find("\"wait_level\":2")!=std::string::npos,"record JSON lacks the wait level");
        s.forget();check(s.waitLevel(0)==0&&s.recent(0).total()==0,"forgetting kept the record");});
    test("downhill adds engine braking to the plan for the same pedestrian",[]{
        auto run=[](double pitch){Fixture f(2);std::string said;
            for(int i=10;i<=30;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=30;w.telemetry.pitchDegrees=pitch;w.gradeBand=slopeBand(pitch);
                ActorObservation p;p.identity={IdentitySpace::Optical,703,0};p.captureTime=t;p.confidence=.9;p.relevant=true;p.category=0;
                const float s=float(.10*std::exp(.22*(t-1)));p.w=s*.4f;p.h=s;p.x=float(.5-s*.2);p.y=float(.80-s);w.actors[w.actorCount++]=p;
                auto d=f.decide(w);if(d.eligible&&said.empty()){const auto& c=d.ticket.candidate;f.e->accept(f.seq,CoachingEngine::spokenId(c),w,t,false);
                    if(const auto* u=f.e->utterance(c.utterance))for(size_t q=0;q<u->count;++q){said+=fragmentText(static_cast<Fragment>(u->fragments[q]));said+=' ';}
                    else said=promptText(c.prompt);said+=d.mode==Mode::WARN?"[urgent]":"";}}
            return said;};
        const auto flat=run(0),down=run(-6);
        std::cout<<"    flat: "<<flat<<"\n    downhill: "<<down<<std::endl;
        check(down.rfind("Downhill.",0)==0&&down.find("Change down")!=std::string::npos,"downhill plan does not set the scene or lacks engine braking");});
    // What the coach actually says this tick (composed sentence or recorded prompt), accepted.
    auto speak=[](Fixture& f,const WorldState& w)->std::string{auto d=f.decide(w);if(!d.eligible)return "";
        const auto& c=d.ticket.candidate;check(f.e->accept(f.seq,CoachingEngine::spokenId(c),w,w.now,false),"accept spoken");
        if(const auto* u=f.e->utterance(c.utterance)){std::string s;for(size_t q=0;q<u->count;++q){s+=fragmentText(static_cast<Fragment>(u->fragments[q]));s+=' ';}return s;}
        return promptText(c.prompt);};
    test("map knowledge: a speed bump ahead and a sharp bend (horn on Indian roads) are named once, a few seconds before",[&]{
        auto run=[&](bool horn){Fixture f(1);std::string said;int bump=0,bend=0;
            for(int i=10;i<=250;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=20;w.hornRules=horn;w.mapTime=t;
                // A bump 60 m ahead closes at 20 km/h; a sharp bend comes after it.
                w.bumpId=77;w.bumpAheadM=std::max(0.,60-(t-1)*5.6);w.bendId=88;w.bendAheadM=std::max(0.,110-(t-1)*5.6);w.bendRight=true;
                const auto text=speak(f,w);if(text.empty())continue;
                if(text.find("bump")!=std::string::npos)++bump;if(text.find("bend")!=std::string::npos)++bend;said+=text+"|";}
            return std::make_tuple(bump,bend,said);};
        auto [bump,bend,said]=run(true);std::cout<<"    "<<said<<std::endl;
        check(bump==1,"speed bump not named exactly once");check(bend==1,"sharp bend not named exactly once");
        check(said.find("horn")!=std::string::npos,"no horn at a sharp bend on Indian roads");
        // Without the horn rule the bend is still named: it is the horn that is left out.
        auto [bump2,bend2,said2]=run(false);std::cout<<"    "<<said2<<std::endl;
        check(bump2==1&&bend2==1&&said2.find("horn")==std::string::npos&&said2.find("Keep left")!=std::string::npos,"horn advice without horn rules, or no bend without them");});
    test("bends from the map: which way, how sharp, and what this speed needs; a bend taken at the right speed gets no words",[&]{
        // A bend 150 m ahead, approached at a steady speed: everything said about it.
        auto drive=[&](bool sharp,bool right,double advise,double kmh,bool horn=true){Fixture f(1);std::string said;
            for(int i=10;i<=400;++i){const double t=i/10.,ahead=150-(t-1)*kmh/3.6;if(ahead<-10)break;auto w=road(t);w.telemetry.speedKmh=kmh;w.hornRules=horn;w.mapTime=t;
                if(ahead>0){w.bendId=91;w.bendAheadM=ahead;w.bendRight=right;w.bendSharp=sharp;w.bendAdviseKmh=advise;}
                const auto text=speak(f,w);if(!text.empty()&&text.find("bend")!=std::string::npos)said+=text+"|";}
            return said;};
        const auto slowSharp=drive(true,false,20,18),fastSharp=drive(true,true,20,40),easeSharp=drive(true,false,20,26);
        const auto rightSpeed=drive(false,true,32,30),easeGentle=drive(false,false,32,38),fastGentle=drive(false,true,30,45),noHorn=drive(true,true,20,18,false);
        std::cout<<"    sharp at 18 km/h: "<<slowSharp<<"\n    sharp at 40: "<<fastSharp<<"\n    sharp at 26: "<<easeSharp<<"\n    32 km/h bend at 30: "<<rightSpeed
                 <<"\n    32 km/h bend at 38: "<<easeGentle<<"\n    30 km/h bend at 45: "<<fastGentle<<"\n    sharp, no horn rule: "<<noHorn<<std::endl;
        const auto has=[](const std::string& s,const char* words){return s.find(words)!=std::string::npos;};
        check(has(slowSharp,"Sharp left bend ahead.")&&has(slowSharp,"Sound the horn.")&&has(slowSharp,"Keep left.")&&!has(slowSharp,"Brake")&&!has(slowSharp,"Ease"),
            "a sharp bend at the right speed: direction, horn and keep left, and nothing about speed");
        check(has(fastSharp,"Sharp right bend ahead.")&&has(fastSharp,"Brake gently.")&&has(fastSharp,"Change down to second.")&&has(fastSharp,"Sound the horn."),
            "a sharp bend at 40 km/h: brake, second gear and the horn");
        check(has(easeSharp,"Ease off the accelerator.")&&has(easeSharp,"Change down to second.")&&has(easeSharp,"Sound the horn."),"a sharp bend a little too fast: ease off, second gear, horn");
        check(rightSpeed.empty(),"a bend taken at the right speed was announced");
        check(has(easeGentle,"Left bend ahead.")&&has(easeGentle,"Ease off the accelerator.")&&!has(easeGentle,"horn")&&!has(easeGentle,"Sharp"),"a bend a little too fast: its direction and ease off, no horn");
        check(has(fastGentle,"Right bend ahead.")&&has(fastGentle,"Brake gently.")&&has(fastGentle,"Change down to third."),"a bend far too fast: brake gently and third gear");
        check(has(noHorn,"Sharp right bend ahead.")&&!has(noHorn,"horn")&&has(noHorn,"Keep left."),"without the horn rule a sharp bend lost more than the horn");
        for(const auto* s:{&slowSharp,&fastSharp,&easeSharp,&easeGentle,&fastGentle,&noHorn})check(std::count(s->begin(),s->end(),'|')==1,"a bend was named twice");});
    test("map knowledge: a bend is named about 7 s ahead, once even as its map point moves, never mid-bend, no change-up into it",[&]{
        Fixture f(2);std::vector<std::pair<double,std::string>> said;
        for(int i=10;i<=200;++i){const double t=i/10.;auto w=road(t);w.mapTime=t;
            w.telemetry.speedKmh=std::min(40.,20+4*t); // Accelerating towards the bend: fourth-gear speed.
            const double ahead=110-(t-1)*10;          // About 10 m/s.
            if(ahead>-20){w.bendAheadM=std::max(0.,ahead);w.bendId=ahead>20?88:89;w.bendRight=true;} // The bend node shifts at 20 m.
            const auto text=speak(f,w);if(!text.empty())said.push_back({t,text});}
        int bends=0;double at=-1;bool upNear=false,upAfter=false;
        for(auto& [t,s]:said){std::cout<<"    "<<t<<": "<<s<<std::endl;
            if(s.find("bend")!=std::string::npos){++bends;at=t;}
            const double ahead=110-(t-1)*10;if(s.find("Change up")!=std::string::npos){if(ahead>0)upNear=true;else upAfter=true;}}
        check(bends==1,"bend not named exactly once as its map point moved");
        // (9 s ahead rather than 7 when the learner must brake for it: here 40 km/h into a second-gear bend.)
        const double lead=(110-(at-1)*10)/10.;check(lead>=3&&lead<=9.5,"bend named too late or too early");
        check(!upNear,"change-up advice into a sharp bend");check(upAfter,"held change-up never given after the bend");});
    // A climb or descent from the route's elevation, `start` m ahead at t=1, approached at a
    // steady speed; the mount has sagged so the phone's tilt reads `pitch` all the way.
    auto hillDrive=[&](double grade,double kmh,double start=150,double pitch=0,double length=250){
        Fixture f(1);std::vector<std::tuple<double,double,std::string>> said;
        for(int i=10;i<=450;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=kmh;
            w.telemetry.pitchDegrees=pitch;w.gradeBand=slopeBand(pitch);
            const double ahead=start-(t-1)*kmh/3.6;
            if(ahead+length>0){w.slopeId=501;w.slopeAheadM=ahead;w.slopeGrade=grade;w.slopeLengthM=std::min(length,length+ahead);}
            w.mapTime=t;w.mapGradeKnown=true;w.mapGradePercent=ahead<=0&&ahead+length>0?grade:0;
            w.gradeBand=slopeBand(std::atan(w.mapGradePercent/100)*180/3.14159265358979323846);
            const auto text=speak(f,w);if(!text.empty())said.push_back({t,ahead,text});}
        return said;};
    test("map hills: a descent is named once about 7 s ahead with the gear, from the map; a sagging mount's tilt says nothing",[&]{
        const auto said=hillDrive(-11,30,150,9);int hills=0;double lead=-1;std::string text;
        for(auto& [t,ahead,s]:said){std::cout<<"    "<<t<<" ("<<ahead<<" m): "<<s<<std::endl;
            if(s.find("ownhill")!=std::string::npos){++hills;lead=ahead/(30/3.6);text=s;}
            check(s.find("phill")==std::string::npos,"the fallen mount's tilt became an uphill instruction");}
        check(hills==1,"descent not named exactly once");check(lead>=4&&lead<=7.5,"descent named too late or too early");
        check(text.rfind("Steep downhill ahead. Change down to second. Control your speed with the brake.",0)==0,"wrong words for a steep descent at 30 km/h");});
    test("map hills: the words depend on the speed (slow down first, which gear, nothing to change)",[&]{
        auto words=[&](double grade,double kmh){for(auto& [t,ahead,s]:hillDrive(grade,kmh,260))if(s.find("hill")!=std::string::npos)return std::make_pair(ahead/(kmh/3.6),s);
            return std::make_pair(-1.,std::string());};
        const std::tuple<double,double,const char*> cases[]={
            {-11,50,"Steep downhill ahead. Brake gently. Change down to second. "},
            {-7,45,"Downhill ahead. Change down to third. Control your speed with the brake. "},
            {-7,20,"Downhill ahead. Do not coast in neutral. Control your speed with the brake. "},
            {7,45,"Uphill ahead. Change down to third. Steady accelerator. "},
            {7,60,"Uphill ahead. Change down a gear. Steady accelerator. "},
            {12,30,"Steep uphill ahead. Change down to second. Steady accelerator. "},
            {7,20,"Uphill ahead. Steady accelerator. "}};
        for(const auto& [grade,kmh,want]:cases){const auto [lead,s]=words(grade,kmh);
            std::cout<<"    "<<grade<<" % at "<<kmh<<" km/h, "<<lead<<" s ahead: "<<s<<std::endl;
            check(s==want,"hill sentence wrong for the speed");
            check(lead>=4&&lead<=(std::string(want).find("Brake gently")!=std::string::npos?9.5:7.5),"hill named at the wrong time");}});
    test("map hills: first seen on the slope it is said once if enough is left; a flat map silences the tilt",[&]{
        int n=0;for(auto& [t,ahead,s]:hillDrive(-11,30,-40,0,260))if(s.find("ownhill")!=std::string::npos){++n;
            check(s.rfind("Steep downhill. Change down to second.",0)==0,"on the slope: not said as on it");}
        check(n==1,"slope under the car not said once");
        for(auto& [t,ahead,s]:hillDrive(-11,30,-200,0,240))check(s.find("ownhill")==std::string::npos,"said with 40 m of it left");
        // No climb or descent on the map; the phone's tilt reads a steep descent (knocked mount).
        for(auto& [t,ahead,s]:hillDrive(0,30,1e9,-9))check(s.find("ownhill")==std::string::npos&&s.find("Change down")==std::string::npos,"tilt spoke over a flat map");});
    test("map hills: a ghat road's next stretch the same way is the same hill (named again only if steeper)",[&]{
        // Climbs of 300 m with 80 m hairpin flats between, at 25 km/h: 7 %, 7 %, then 12 %, then a descent.
        Fixture f(1);std::vector<std::string> said;const double v=25/3.6;
        const double starts[]={100,480,860,1240},grades[]={7,7.5,12,-8};
        for(int i=10;i<=2400;++i){const double t=i/10.,d=(t-1)*v;auto w=road(t);w.telemetry.speedKmh=25;w.mapTime=t;w.mapGradeKnown=true;
            for(int k=0;k<4;++k)if(d<starts[k]+300){w.slopeId=900+k;w.slopeAheadM=starts[k]-d;w.slopeGrade=grades[k];w.slopeLengthM=std::min(300.,starts[k]+300-d);
                w.mapGradePercent=d>=starts[k]?grades[k]:0;break;}
            w.gradeBand=slopeBand(std::atan(w.mapGradePercent/100)*180/3.14159265358979323846);
            const auto s=speak(f,w);if(s.find("hill")!=std::string::npos){said.push_back(s);std::cout<<"    "<<t<<" ("<<d<<" m): "<<s<<std::endl;}}
        check(said.size()==3,"not one cue per hill (climb, steeper climb, descent)");
        check(said[0].rfind("Uphill ahead.",0)==0&&said[1].rfind("Steep uphill ahead.",0)==0&&said[2].rfind("Downhill ahead.",0)==0,"wrong hills named");});
    test("junction entry on Indian roads adds the horn to clutch down and look both ways",[&]{
        auto entry=[&](bool horn){Fixture f(2);std::string text;
            for(int i=10;i<=400;++i){const double t=i/10.;auto w=road(t);const double lead=std::max(.5,40-t);junction(w,lead);w.telemetry.speedKmh=lead<6?8:20;w.hornRules=horn;
                const auto s=speak(f,w);if(s.find("ook both ways")!=std::string::npos)text=s;}
            return text;};
        const auto india=entry(true),other=entry(false);std::cout<<"    entry: "<<india<<" / without horn rules: "<<other<<std::endl;
        check(india.find("Look both ways.")!=std::string::npos&&india.find("Sound the horn.")!=std::string::npos,"junction entry without the horn");
        check(!other.empty()&&other.find("horn")==std::string::npos,"horn at junction entry without horn rules");});
    test("lane keeping: sustained drift right on a marked lane means keep left; no lane lines, no lane advice",[]{
        Fixture f(1);int said=0;
        for(int i=10;i<=40;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=30;
            w.laneValid=true;w.laneTime=t;w.laneLeft=.05f;w.laneRight=.55f; // car (image centre) well right of the lane centre
            const int p=f.accept(w);if(p)said=p;}
        const auto* u=f.e->utterance(said);std::string text=u?"":promptText(static_cast<Prompt>(said));
        if(u)for(size_t q=0;q<u->count;++q){text+=fragmentText(static_cast<Fragment>(u->fragments[q]));text+=' ';}
        check(text.find("Keep left")!=std::string::npos,"drift right did not produce keep-left advice");
        Fixture g(1);for(int i=10;i<=40;++i){const double t=i/10.;auto w=road(t);w.telemetry.speedKmh=30;check(!g.decide(w).eligible,"lane advice without lane lines");}});
    test("route loss does not disable supported camera or hill tasks",[]{Fixture f;auto w=road(1);junction(w);check(f.accept(w)==56,"plan");
        w=road(14);user(w,99);check(f.accept(w)==64,"route loss disabled camera");w=road(27);hill(w);check(f.accept(w)==int(Prompt::gentleUp),"route loss disabled terrain");});
    test("stationary or unknown speed does not trigger power or bend instruction",[]{for(bool unknown:{false,true}){Fixture f;auto w=road(1);hill(w);bend(w);w.telemetry.speedKmh=0;w.telemetry.speedValid=!unknown;
        check(!f.decide(w).eligible,"power/bend instruction without known motion");}});
    test("camera loss silences routine teaching and invalidates queued hill onset",[]{Fixture f(1,true);auto w=road(1);hill(w);const int p=f.accept(w,true);check(p!=0,"hill fixture");
        auto n=w;n.now=n.telemetry.time=1.36;check(!f.e->playbackValid(1,p,n,1.36,1),"stale camera hill onset");
        n=road(2);hill(n);n.captureTime=1;check(!f.decide(n).eligible,"routine hill despite camera loss");});
    test("voice failure is not delivery and completion is counted once",[]{Fixture f(1,true);auto w=road(1);user(w);auto d=f.decide(w);
        check(d.eligible&&!f.e->accept(1,64,w,1,false),"refusal counted");w=road(1.1);user(w);check(f.accept(w,true)==64,"retry");
        check(f.e->playbackStarted(2,64,w,1.1,1)&&f.e->deliveryEvent(2,64,DeliveryStage::Completed,3),"complete");
        check(f.e->deliveryEvent(2,64,DeliveryStage::Completed,3.1),"idempotent completion");
        check(f.e->drive().json(f.e->task()).find("\"road_user_deliveries\":1")!=std::string::npos,"completion double counted");});
    test("started speech follows fresh evidence while queued onset keeps its original expiry",[]{Fixture f(1,true);auto w=road(1);hill(w);const int p=f.accept(w,true);
        check(p!=0&&f.e->playbackStarted(1,p,w,1,1),"onset");w=road(1.5);hill(w);
        check(!f.e->playbackValid(1,p,w,1.5,1)&&f.e->continuationValid(1,p,w,1.5,1),"continuation/onset clocks conflated");
        w.telemetry.pitchValid=false;check(!f.e->continuationValid(1,p,w,1.5,1),"lost terrain evidence continued speech");});
    test("brief actor dropout respects original freshness then stops playback",[]{Fixture f(1,true);auto w=road(1);user(w);check(f.accept(w,true)==64&&f.e->playbackStarted(1,64,w,1,1),"onset");
        w=road(1.1);check(f.e->continuationValid(1,64,w,1.1,1),"brief loss cut off fresh observed encounter");
        // A routine advisory validated at onset finishes while perception is live.
        w=road(1.36);check(f.e->continuationValid(1,64,w,1.36,1),"detector dropout truncated routine speech");
        w=road(2.05);auto gone=w;user(gone);gone.actors[0].relevant=false;check(f.e->continuationValid(1,64,gone,2.05,1),"moved-aside actor truncated routine speech");
        // Started speech finishes its sentence through a late camera frame, a jolt or braking
        // (Nov 6: those cut nearly every instruction mid-word); it stops when perception stops.
        w=road(2.1);w.captureTime=1.5;check(f.e->continuationValid(1,64,w,2.1,1),"a late camera frame cut routine speech off");
        w=road(2.1);w.telemetry.shock=w.telemetry.braking=true;check(f.e->continuationValid(1,64,w,2.1,1),"a jolt or braking cut routine speech off");
        w=road(2.1);w.active=false;check(!f.e->continuationValid(1,64,w,2.1,1),"routine speech continued after perception stopped");
        // Urgent speech and the 0.7.1 comparison mode keep the original 350 ms budget.
        Fixture u(1,true);w=road(1);user(w,8,true);check(u.accept(w,true)==67&&u.e->playbackStarted(1,67,w,1,1),"urgent onset");
        // An urgent risk instruction finishes its sentence while perception is live
        // (road user seen within the time the sentence takes to say), and stops when perception
        // goes blind.
        w=road(1.36);check(u.e->continuationValid(1,67,w,1.36,1),"urgent instruction truncated by a brief detector dropout");
        w=road(4.6);check(u.e->continuationValid(1,67,w,4.6,1),"the last words of an urgent sentence were cut off once its road user had passed");
        w=road(7.2);check(!u.e->continuationValid(1,67,w,7.2,1),"urgent speech outlived its evidence");
        w=road(1.5);w.captureTime=1.0;check(u.e->continuationValid(1,67,w,1.5,1),"a late camera frame cut an urgent instruction off");
        w=road(1.5);w.active=false;check(!u.e->continuationValid(1,67,w,1.5,1),"urgent speech continued after perception stopped");
        Fixture old(1,true);old.e->enableTaskScheduling(false);w=road(1);user(w);check(old.accept(w,true)==44&&old.e->playbackStarted(1,44,w,1,1),"old onset");
        w=road(1.36);check(!old.e->continuationValid(1,44,w,1.36,1),"0.7.1 comparison mode changed");
        user(w,99);check(!old.e->continuationValid(1,44,w,1.36,1),"replacement actor continued old cue");});
    test("a warning about a road user who has gone gives way to a turn step that is due",[]{
        // The preparation has been said. An urgent warning starts 6 s before the turn; its road user
        // is gone a moment later, and the turn itself becomes due while the warning is still playing.
        auto run=[](bool turn,double at){Fixture f(1,true);auto w=road(1);if(turn)junction(w);
            if(turn){check(f.accept(w,true)==56&&f.e->playbackStarted(1,56,w,1,1)&&f.e->deliveryEvent(1,56,DeliveryStage::Completed,4),"preparation");}
            for(int i=45;i<=79;++i){auto q=road(i/10.);q.telemetry.speedKmh=20;if(turn)junction(q,14-i/10.);f.decide(q);}
            {auto p=road(7.9);p.telemetry.speedKmh=20;if(turn)junction(p,6.1);user(p,8,true);f.e->decide(p,9999);}
            w=road(8);w.telemetry.speedKmh=20;if(turn)junction(w,6);user(w,8,true);
            const auto d=f.decide(w);check(d.eligible&&d.ticket.candidate.urgent,"the urgent warning was not offered");
            const int p=int(d.ticket.candidate.prompt);check(f.e->accept(f.seq,p,w,8,true)&&f.e->playbackStarted(f.seq,p,w,8,1),"warning onset");
            const uint64_t playing=f.seq;
            for(double t=8.1;t<at-.05;t+=.1){auto q=road(t);q.telemetry.speedKmh=20;if(turn)junction(q,14-t);f.e->decide(q,20000+uint64_t(t*10));}
            auto q=road(at);q.telemetry.speedKmh=20;if(turn)junction(q,14-at);
            return f.e->continuationValid(playing,p,q,at,1);};
        check(run(false,11.5),"control: with no turn due, the warning was cut before its end");
        check(run(true,9.2),"the warning gave way within 1.5 s of its road user being seen");
        check(!run(true,10.),"a warning about a road user gone for 2 s kept a due turn step waiting");});
    test("source change preserves aggregate counts but cancels current task",[]{Fixture f;auto w=road(1);hill(w);f.accept(w);f.e->beginSource(2);
        const auto s=f.e->drive().json(f.e->task());check(s.find("\"hill_deliveries\":1")!=std::string::npos&&s.find("WATCHING_ROAD")!=std::string::npos,"source reset erased history or retained old phase");});
    test("no gear or control observation is fabricated by automatic mode",[]{Fixture f;auto w=road(1);hill(w);f.accept(w);const auto s=f.e->drive().json(f.e->task());
        check(s.find("\"controls\":\"UNOBSERVED\"")!=std::string::npos&&s.find("\"driving_outcome\":\"UNASSESSED\"")!=std::string::npos,"invented control/mastery");});
    test("C API exposes shared automatic state without provider content",[]{std::unique_ptr<ld_engine,decltype(&ld_destroy)> e(ld_create(),ld_destroy);ld_coaching_automatic(e.get(),1);
        const std::string s=ld_coaching_drive_json(e.get());check(s.find("\"automatic\":true")!=std::string::npos&&s.find("routeId")==std::string::npos,"automatic bridge");});
    test("profile and route feature matrix keeps urgency and pacing contracts",[]{
        for(int profile=0;profile<3;++profile)for(int feature=0;feature<=5;++feature)for(int fault=0;fault<5;++fault){
            Fixture f(profile);auto w=road(1);if(feature>=3){junction(w);w.route.feature=feature;}bend(w);hill(w);user(w,7,fault==0);
            if(fault==1)w.captureTime=0;if(fault==2)w.telemetry.time=0;if(fault==3)w.active=false;if(fault==4)w.telemetry.shock=true;
            const auto d=f.decide(w);if(fault==0)check(d.eligible&&d.mode==Mode::WARN,"matrix lost urgent priority");
            if(fault>=1)check(!d.eligible,"matrix admitted unsupported instruction");
        }
    });
    test("continuous mixed drive recovers across hazards route loss camera loss and source changes",[&]{
        std::ofstream timeline;if(argc>1){timeline.open(argv[1]);check(timeline.good(),"timeline output");}
        for(int profile=0;profile<3;++profile){
            Fixture f(profile);int warnings=0;std::string previous;
            for(int i=10;i<=1960;++i){
                const double t=i/10.;auto w=road(t);
                if(t<26)user(w);
                else if(t<51)bend(w);
                else if(t<76)hill(w);
                else if(t<114){
                    junction(w,36-(t-76));
                    w.telemetry.speedKmh=30-std::min(7.,std::max(0.,t-82));
                    w.route.distanceM=w.route.etaSeconds*w.telemetry.speedKmh/3.6;
                    if((t>=78&&t<79)||(t>=109&&t<110))user(w,t<100?99:100,true);
                }else if(t<146){hill(w);w.telemetry.pitchDegrees=-3;w.gradeBand=-1;w.candidates[0].identity.id=4;w.candidates[0].prompt=Prompt::gentleDown;
                    if(t>=141)w.captureTime=t-2;
                }else if(t<171){bend(w);w.roadBend=1;w.candidates[0].identity.id=3;w.candidates[0].prompt=Prompt::bendRight;}
                else {if(i==1710)f.e->beginSource(2);w.sourceId=2;hill(w);if(t>=195)w.active=false;}
                const auto d=f.decide(w);bool accepted=false;
                if(d.eligible){
                    check(!(t>=141&&t<146)&&t<195,"teaching during evidence loss or inactive session");
                    accepted=f.e->accept(f.seq,int(d.ticket.candidate.prompt),w,t,false);check(accepted,"mixed-drive acceptance");
                    if(d.mode==Mode::WARN)++warnings;
                }
                const auto state=f.e->drive().json(f.e->task());
                if(timeline&&(accepted||state!=previous))timeline<<"{\"scope\":\"synthetic_mixed_drive\",\"profile\":"<<profile<<",\"time\":"<<t
                    <<",\"visual_accepted\":"<<(accepted?"true":"false")<<",\"prompt\":"<<(accepted?int(d.ticket.candidate.prompt):0)
                    <<",\"instructor_labels\":null,\"drive\":"<<state<<"}\n";
                previous=state;
            }
            check(warnings==2,"mixed drive lost or repeated a supported warning");
            const auto state=f.e->drive().json(f.e->task());
            for(const char* key:{"road_user_deliveries","bend_deliveries","hill_deliveries","junction_deliveries"})
                check(state.find(std::string("\"")+key+"\":0")==std::string::npos,"mixed drive omitted a supported task family");
        }
    });
    std::cout<<passed<<" automatic driving groups passed (including 90 matrix cases and three continuous mixed drives)\n";if(failed)std::cerr<<failed<<" groups failed\n";return failed?1:0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
