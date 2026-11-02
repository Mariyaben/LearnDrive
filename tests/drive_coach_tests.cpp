#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/c_api.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <stdexcept>
using namespace ld;using namespace ld::coaching;
namespace {
int passed=0;
void check(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
template<class F>void test(const char* name,F f){f();++passed;std::cout<<"PASS "<<name<<'\n';}
WorldState road(double t){WorldState w;w.sourceId=1;w.frameSequence=uint64_t(t*1000)+1;w.now=w.captureTime=t;
    w.active=w.audioReady=true;w.telemetry={t,30,0,0,1,true,true,true,false,false,false};return w;}
void junction(WorldState& w,double lead=30){w.route={17,9,2,w.now,lead*30/3.6,lead,w.now,4,true,true};}
void user(WorldState& w,uint64_t id=7,bool urgent=false,double tti=infinity){
    ActorObservation a;a.identity={IdentitySpace::Attention,id,0};a.captureTime=w.now;a.confidence=.9;a.relevant=true;a.category=0;
    a.inPath=a.opticalClosing=urgent||std::isfinite(tti);a.tti=urgent?2:tti;a.x=.4f;a.y=.3f;a.w=.1f;a.h=.4f;
    w.actors[w.actorCount++]=a;Candidate c;c.prompt=Prompt::pedestrianSpace;c.origin=4;c.identity=a.identity;c.kind=urgent?EventKind::Approach:EventKind::Attention;
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
    ld::coaching::Decision decide(const WorldState& w){return e->decide(w,++seq);}
    int accept(const WorldState& w,bool voice=false){auto d=decide(w);if(!d.eligible)return 0;
        const int p=int(d.ticket.candidate.prompt);check(e->accept(seq,p,w,w.now,voice),"accept");return p;}
};
}
int main(int argc,char** argv){try {
    test("automatic session activates junction coaching without lesson selection",[]{Fixture f;auto w=road(1);junction(w);check(f.accept(w)==56,"automatic planning missing");
        w=road(14);junction(w,17);check(f.accept(w)==59,"automatic approach missing");});
    test("route-free driving keeps road-user bend and hill support",[]{Fixture f;auto w=road(1);user(w);check(f.accept(w)==44,"route-free road user");
        w=road(14);bend(w);check(f.accept(w)==int(Prompt::bendLeft),"route-free bend");w=road(27);hill(w);check(f.accept(w)==int(Prompt::gentleUp),"route-free hill");
        const auto s=f.e->drive().json(f.e->task());check(s.find("\"road_user_deliveries\":1")!=std::string::npos&&s.find("\"bend_deliveries\":1")!=std::string::npos&&s.find("\"hill_deliveries\":1")!=std::string::npos,"mixed record");});
    test("an urgent pedestrian interrupts preparation and only fresh useful preparation resumes",[]{Fixture f(1,true);auto w=road(1);junction(w);check(f.accept(w,true)==56,"prepare");
        check(f.e->playbackStarted(1,56,w,1,1),"planning onset");w=road(1.2);junction(w,29);user(w,8,true);
        check(f.accept(w,true)==44,"hazard did not win");check(!f.e->playbackValid(1,56,w,1.2,1),"old task remained playable");
        check(f.e->playbackStarted(2,44,w,1.2,1)&&f.e->deliveryEvent(2,44,DeliveryStage::Completed,3),"warning completion");
        w=road(3.1);junction(w,27);check(f.accept(w,true)==56,"fresh task did not recover after interrupt");
        check(!f.e->task().progress().planningDelivered,"interrupted preparation counted as delivered");});
    test("developing measured conflict ranks ahead of a routine lesson without warning authority",[]{Fixture f;auto w=road(1);junction(w);user(w,8,false,9);
        auto d=f.decide(w);check(d.eligible&&d.mode==Mode::COACH&&d.ticket.candidate.prompt==Prompt::pedestrianSpace,"closing conflict starved by lesson");});
    test("routine road-user presence cannot displace timely task preparation",[]{Fixture f;auto w=road(1);junction(w);user(w);check(f.accept(w)==56,"presence displaced planning");});
    test("semantic variants do not repeat an unchanged continuous encounter after pacing",[]{Fixture f;auto w=road(1);user(w);check(f.accept(w)==44,"first space cue");
        for(int i=1;i<=150;++i){w=road(1+i*.1);user(w);auto& c=w.candidates[0];c.prompt=Prompt::roadUserQuiet;c.kind=EventKind::Approach;
            check(!f.decide(w).eligible,"semantic variant repeated same encounter");}
        w=road(17);user(w,99);check(f.accept(w)==44,"new encounter suppressed as duplicate");});
    test("already-informed high-ranked cue cannot starve another task",[]{Fixture f;auto w=road(1);user(w);check(f.accept(w)==44,"first");
        w=road(14);user(w);w.candidates[0].prompt=Prompt::roadUserQuiet;w.candidates[0].kind=EventKind::Approach;hill(w);
        check(f.accept(w)==int(Prompt::gentleUp),"duplicate starved hill preparation");});
    test("quiet execution retains urgent evidence and resumes other tasks afterwards",[]{Fixture f;auto w=road(1);junction(w,7);bend(w);hill(w);user(w);check(!f.decide(w).eligible,"routine speech during execution");
        w=road(1.1);junction(w,6);user(w,8,true);auto d=f.decide(w);check(d.eligible&&d.mode==Mode::WARN,"urgent hidden in execution");
        w=road(2);hill(w);check(f.accept(w)==int(Prompt::gentleUp),"junction quiet leaked into later road context");});
    test("route loss does not disable supported camera or hill tasks",[]{Fixture f;auto w=road(1);junction(w);check(f.accept(w)==56,"plan");
        w=road(14);user(w,99);check(f.accept(w)==44,"route loss disabled camera");w=road(27);hill(w);check(f.accept(w)==int(Prompt::gentleUp),"route loss disabled terrain");});
    test("stationary or unknown speed does not trigger power or bend instruction",[]{for(bool unknown:{false,true}){Fixture f;auto w=road(1);hill(w);bend(w);w.telemetry.speedKmh=0;w.telemetry.speedValid=!unknown;
        check(!f.decide(w).eligible,"power/bend instruction without known motion");}});
    test("camera loss silences routine teaching and invalidates queued hill onset",[]{Fixture f(1,true);auto w=road(1);hill(w);const int p=f.accept(w,true);check(p!=0,"hill fixture");
        auto n=w;n.now=n.telemetry.time=1.36;check(!f.e->playbackValid(1,p,n,1.36,1),"stale camera hill onset");
        n=road(2);hill(n);n.captureTime=1;check(!f.decide(n).eligible,"routine hill despite camera loss");});
    test("voice failure is not delivery and completion is counted once",[]{Fixture f(1,true);auto w=road(1);user(w);auto d=f.decide(w);
        check(d.eligible&&!f.e->accept(1,44,w,1,false),"refusal counted");w=road(1.1);user(w);check(f.accept(w,true)==44,"retry");
        check(f.e->playbackStarted(2,44,w,1.1,1)&&f.e->deliveryEvent(2,44,DeliveryStage::Completed,3),"complete");
        check(f.e->deliveryEvent(2,44,DeliveryStage::Completed,3.1),"idempotent completion");
        check(f.e->drive().json(f.e->task()).find("\"road_user_deliveries\":1")!=std::string::npos,"completion double counted");});
    test("started speech follows fresh evidence while queued onset keeps its original expiry",[]{Fixture f(1,true);auto w=road(1);hill(w);const int p=f.accept(w,true);
        check(p!=0&&f.e->playbackStarted(1,p,w,1,1),"onset");w=road(1.5);hill(w);
        check(!f.e->playbackValid(1,p,w,1.5,1)&&f.e->continuationValid(1,p,w,1.5,1),"continuation/onset clocks conflated");
        w.telemetry.pitchValid=false;check(!f.e->continuationValid(1,p,w,1.5,1),"lost terrain evidence continued speech");});
    test("brief actor dropout respects original freshness then stops playback",[]{Fixture f(1,true);auto w=road(1);user(w);check(f.accept(w,true)==44&&f.e->playbackStarted(1,44,w,1,1),"onset");
        w=road(1.1);check(f.e->continuationValid(1,44,w,1.1,1),"brief loss cut off fresh observed encounter");
        w=road(1.36);check(!f.e->continuationValid(1,44,w,1.36,1),"expired actor became a ghost continuation");
        user(w,99);check(!f.e->continuationValid(1,44,w,1.36,1),"replacement actor continued old cue");});
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
    std::cout<<passed<<" automatic driving groups passed (including 90 matrix cases and three continuous mixed drives)\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
