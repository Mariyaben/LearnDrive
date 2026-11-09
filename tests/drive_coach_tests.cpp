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
    test("automatic session activates junction coaching without lesson selection",[]{Fixture f;auto w=road(1);junction(w);check(f.accept(w)==56,"automatic planning missing");
        w=road(14);junction(w,9);check(f.accept(w)==60,"automatic approach missing");});
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
    test("quiet execution retains urgent evidence and resumes other tasks afterwards",[]{Fixture f;auto w=road(1);junction(w,5.5);bend(w);hill(w);user(w);check(!f.decide(w).eligible,"routine speech during execution");
        w=road(1.1);junction(w,5.4);user(w,8,true);auto d=f.decide(w);check(d.eligible&&d.mode==Mode::WARN,"urgent hidden in execution");
        // Navigation moved on to a different fresh event: the junction ended and other tasks resume.
        w=road(2);junction(w,60);w.route.eventId=10;w.route.feature=0;hill(w);check(f.accept(w)==int(Prompt::gentleUp),"junction quiet leaked into later road context");
        // Route lost inside execution: routine silence is held (bounded), then released.
        Fixture g;w=road(1);junction(w,5.5);g.decide(w);w=road(2);hill(w);check(!g.decide(w).eligible,"route dropout un-quieted execution");
        check(g.e->drive().json(g.e->task()).find("EXECUTION_QUIET")!=std::string::npos,"held quiet not visible");
        w=road(11.2);hill(w);check(g.accept(w)==int(Prompt::gentleUp),"bounded execution hold never released");
        Fixture h;h.e->enableTaskScheduling(false);w=road(1);junction(w,6);h.decide(w);w=road(2);hill(w);check(h.accept(w)==int(Prompt::gentleUp),"0.7.1 comparison mode changed");});
    test("a routine cue yields to a known upcoming lesson window but a closing conflict does not",[]{Fixture f(0);auto w=road(1);junction(w,25);bend(w);
        check(!f.decide(w).eligible&&std::string(f.e->reasonCode())=="RESERVED_FOR_UPCOMING_TASK","routine cue consumed the lesson's interval");
        w=road(1.6);junction(w,24.4);user(w,8,false,9);auto d=f.decide(w);check(d.eligible&&d.ticket.candidate.prompt==Prompt::personEaseCover,"closing conflict reserved away");
        Fixture b(1);w=road(1);junction(w,25);user(w);check(b.decide(w).eligible,"Balanced reserved although the window stays reachable");});
    test("an episode follow-up step has its own spacing in every profile",[]{for(int profile=0;profile<3;++profile){Fixture f(profile);auto w=road(1);junction(w,13);check(f.accept(w)==56,"plan");
        w=road(4);junction(w,10);check(!f.decide(w).eligible,"follow-up crammed after planning");
        w=road(6);junction(w,8);check(f.accept(w)==60,"profile pacing removed the follow-up step");}});
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
        auto [bump2,bend2,said2]=run(false);check(bump2==1&&bend2==0&&said2.find("horn")==std::string::npos,"horn advice without horn rules");});
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
        const double lead=(110-(at-1)*10)/10.;check(lead>=3&&lead<=7.5,"bend named too late or too early");
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
        // (road user seen within 3.5 s), and stops when perception goes blind.
        w=road(1.36);check(u.e->continuationValid(1,67,w,1.36,1),"urgent instruction truncated by a brief detector dropout");
        w=road(4.6);check(!u.e->continuationValid(1,67,w,4.6,1),"urgent speech outlived its evidence");
        w=road(1.5);w.captureTime=1.0;check(u.e->continuationValid(1,67,w,1.5,1),"a late camera frame cut an urgent instruction off");
        w=road(1.5);w.active=false;check(!u.e->continuationValid(1,67,w,1.5,1),"urgent speech continued after perception stopped");
        Fixture old(1,true);old.e->enableTaskScheduling(false);w=road(1);user(w);check(old.accept(w,true)==44&&old.e->playbackStarted(1,44,w,1,1),"old onset");
        w=road(1.36);check(!old.e->continuationValid(1,44,w,1.36,1),"0.7.1 comparison mode changed");
        user(w,99);check(!old.e->continuationValid(1,44,w,1.36,1),"replacement actor continued old cue");});
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
