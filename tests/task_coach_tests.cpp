#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/c_api.h"
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace ld;
using namespace ld::coaching;
namespace {
int passed=0;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void test(const char* name,F f){f();++passed;std::cout<<"PASS "<<name<<'\n';}
// Act-by schedule at 30 km/h: preparation 6.5-14.5 s before the junction, "brake, change
// down" 6.5-10.5 s, entry 2-6 s (only at or below 25 km/h); routine cues quiet below 8 s.
WorldState scene(double time,double seconds=13,double speed=30,int feature=4,uint64_t event=9){
    WorldState w;w.sourceId=1;w.now=w.captureTime=time;w.frameSequence=uint64_t(time*1000)+1;
    w.active=true;w.audioReady=true;w.telemetry={time,speed,0,0,1,true,true,true,false,false,false};
    w.route={17,event,2,time,seconds*speed/3.6,seconds,time,feature,true,true};return w;
}
void actor(WorldState& w,bool urgent){
    ActorObservation a;a.identity={IdentitySpace::Attention,7,0};a.captureTime=w.now;a.confidence=.9;
    a.category=0;a.relevant=true;a.inPath=a.opticalClosing=urgent;a.tti=urgent?2:infinity;
    a.x=.4f;a.y=.3f;a.w=.1f;a.h=.4f;w.actors[w.actorCount++]=a;
    Candidate c;c.prompt=Prompt::pedestrianSpace;c.origin=4;c.identity=a.identity;
    c.kind=urgent?EventKind::Approach:EventKind::Attention;c.captureTime=w.now;c.expiresAt=w.now+.35;
    c.supported=true;c.urgent=urgent;c.confidence=.9;c.tti=a.tti;c.actorCategory=0;c.evidenceKind=urgent?2:1;
    w.candidates[w.candidateCount++]=c;
}
struct Fixture {
    std::unique_ptr<CoachingEngine> p=std::make_unique<CoachingEngine>();
    uint64_t sequence{};
    Fixture(bool voice=false){check(p->configure(1,voice,1),"configure");p->beginSource(1);p->enableJunctionLesson(true);}
    ld::coaching::Decision decide(const WorldState& w){return p->decide(w,++sequence);}
    void visual(const WorldState& w){auto d=decide(w);check(d.eligible&&int(d.ticket.candidate.prompt)>=56,"planning missing");check(p->accept(sequence,int(d.ticket.candidate.prompt),w,w.now,false),"visual accept");}
};
}
int main(){try {
    test("default off and explicit junction directions",[]{
        CoachingEngine off;check(!off.task().enabled(),"lesson on by default");
        for(int feature=3;feature<=5;++feature){Fixture f;auto d=f.decide(scene(1,13,30,feature));
            check(d.eligible&&int(d.ticket.candidate.prompt)==(feature==4?56:feature==5?57:58),"wrong planning direction");}
    });
    test("native task creates a lesson without an object or phrase trigger",[]{
        Fixture f;auto w=scene(1);auto d=f.decide(w);check(d.eligible&&d.ticket.candidate.lessonStep==1,"task not produced");
        check(f.p->task().progress().opportunities==1,"opportunity not tracked");
        actor(w,false);d=f.decide(w);check(d.eligible&&d.ticket.candidate.lessonStep==1,"routine object announcement displaced goal");
    });
    test("reapplying the same lesson does not reset delivery or opportunity history",[]{
        Fixture f;f.visual(scene(1));f.decide(scene(2));f.p->enableJunctionLesson(true);
        auto d=f.decide(scene(14,9));check(d.eligible&&d.ticket.candidate.lessonStep==2,"idempotent setting reset lesson");
        check(f.p->task().progress().opportunities==1,"idempotent setting invented opportunity");
    });
    test("voice refusal does not advance preparation",[]{
        Fixture f(true);auto w=scene(1);auto d=f.decide(w);check(d.eligible,"fixture");
        check(!f.p->accept(f.sequence,56,w,1,false),"failed voice treated as visual");
        d=f.decide(scene(1.1));check(d.eligible&&d.ticket.candidate.lessonStep==1,"failed planning not retryable");
        check(!f.p->task().progress().planningDelivered,"refusal advanced task");
    });
    test("submitted and started audio cannot unlock the next step",[]{
        Fixture f(true);auto w=scene(1);check(f.decide(w).eligible&&f.p->accept(1,56,w,1,true),"submit");
        check(f.p->playbackStarted(1,56,w,1,1),"onset");
        auto d=f.decide(scene(14,9));check(!d.eligible&&!f.p->task().progress().planningDelivered,"onset became completed teaching");
    });
    test("completed voice delivery unlocks one approach reminder",[]{
        Fixture f(true);auto w=scene(1);check(f.decide(w).eligible&&f.p->accept(1,56,w,1,true),"submit");
        check(f.p->playbackStarted(1,56,w,1,1)&&f.p->deliveryEvent(1,56,DeliveryStage::Completed,6.8),"complete");
        check(!f.decide(scene(7,12)).eligible,"extra planning");
        w=scene(14,9);auto d=f.decide(w);check(d.eligible&&d.ticket.candidate.prompt==Prompt::junctionBrakeGearDown,"approach missing");
        check(f.p->accept(f.sequence,60,w,14,true)&&f.p->playbackStarted(f.sequence,60,w,14,1),"approach onset");
        check(f.p->deliveryEvent(f.sequence,60,DeliveryStage::Completed,18),"approach completion");
        check(!f.decide(scene(28,7)).eligible,"repeated approach");
        check(f.p->task().progress().planningCompletions==1&&f.p->task().progress().approachCompletions==1,"step counters");
    });
    test("intentional visual delivery advances but never proves learner action",[]{
        Fixture f;f.visual(scene(1));auto d=f.decide(scene(14,9));check(d.eligible&&d.ticket.candidate.lessonStep==2,"visual progression");
        auto json=f.p->task().json();check(json.find("\"mirror_check\":\"UNOBSERVED\"")!=std::string::npos&&json.find("\"driving_outcome\":\"UNASSESSED\"")!=std::string::npos,"invented skill result");
    });
    test("steps keep their own spacing, a missed preparation is still given late, never the approach first",[]{
        Fixture f;f.visual(scene(1));auto d=f.decide(scene(4,9));check(!d.eligible&&d.mode==Mode::DEFER,"approach crammed right after preparation");
        d=f.decide(scene(14,5));check(!d.eligible&&f.p->task().progress().phase==TaskPhase::ExecutionQuiet,"approach after its window at speed");
        Fixture late;check(!late.decide(scene(1,15)).eligible,"preparation before its act-by window");
        d=late.decide(scene(14,10));check(d.eligible&&d.ticket.candidate.lessonStep==1,"late preparation lost or approach taught first");
        Fixture early;early.visual(scene(1));check(!early.decide(scene(14,12)).eligible,"approach before its act-by window");
    });
    test("slowing after delivery suppresses redundant approach advice",[]{
        Fixture f;f.visual(scene(1,14,34));
        for(int i=0;i<=70;++i){const double time=1.1+i*.1;f.decide(scene(time,13.9-i*.05,34-i*.1));}
        check(f.p->task().progress().speedReductionObserved,"post-delivery slowing not observed");
        auto d=f.decide(scene(8.4,9.9,26.5));check(d.eligible&&d.ticket.candidate.prompt==Prompt::junctionGearDown,"approach not adapted after slowing");
    });
    test("pre-delivery slowing cannot be called a post-delivery response",[]{
        Fixture f;for(int i=0;i<=20;++i)f.decide(scene(1+i*.1,20,35-i*.15));
        auto w=scene(3.1,14,32);auto d=f.decide(w);check(d.eligible&&f.p->accept(f.sequence,56,w,3.1,false),"visual fixture");
        f.decide(scene(3.2,13.9,32));check(!f.p->task().progress().speedReductionObserved,"historical response fabricated");
        f.decide(scene(4.2,13,32));check(!f.p->task().progress().speedReductionObserved,"steady response called slowing");
    });
    test("stale and missing inputs suppress the task",[]{
        for(int fault=0;fault<10;++fault){Fixture f;auto w=scene(5);
            switch(fault){case 0:w.captureTime=4;break;case 1:w.route.gpsTime=2;break;case 2:w.route.captureTime=1;break;
            case 3:w.telemetry.time=4;break;case 4:w.telemetry.speedValid=false;break;case 5:w.route.valid=false;break;
            case 6:w.route.etaSeconds=infinity;break;case 7:w.telemetry.speedKmh=0;break;case 8:w.active=false;break;case 9:w.route.distanceM=-1;break;}
            check(!f.decide(w).eligible,"missing/stale evidence produced teaching");}
    });
    test("braking and shock suppress extra teaching",[]{
        for(int fault=0;fault<3;++fault){Fixture f;auto w=scene(1);w.telemetry.braking=fault==0;w.telemetry.hardBraking=fault==1;w.telemetry.shock=fault==2;check(!f.decide(w).eligible,"instruction during existing response/workload");}
    });
    test("final approach is quiet for routine coaching while urgent evidence wins",[]{
        Fixture f;auto w=scene(1,5.5);actor(w,false);check(!f.decide(w).eligible,"routine cue during final approach");
        w=scene(1.1,5.4);actor(w,true);auto d=f.decide(w);check(d.eligible&&d.mode==Mode::WARN&&d.ticket.candidate.lessonStep==0,"task hid urgent event");
        Fixture queued(true);w=scene(1,25);actor(w,false);d=queued.decide(w);
        check(d.eligible&&queued.p->accept(1,44,w,1,true),"routine queue");
        w.now=w.captureTime=w.telemetry.time=1.1;w.route.etaSeconds=7;w.route.distanceM=58;
        check(!queued.p->playbackValid(1,44,w,1.1,1),"routine onset crossed final-approach boundary");
    });
    test("queued task rechecks final approach and camera at onset",[]{
        for(int fault=0;fault<3;++fault){Fixture f(true);auto w=scene(1);check(f.decide(w).eligible&&f.p->accept(1,56,w,1,true),"queue");
            auto n=scene(1.1);if(fault==0){n.route.distanceM=20;n.route.etaSeconds=2;}if(fault==1)n.captureTime=0;if(fault==2)n.route.epoch++;
            check(!f.p->playbackValid(1,56,n,1.1,1),"invalid queued lesson played");}
    });
    test("reroute with reused event identity does not inherit delivery",[]{
        Fixture f;f.visual(scene(1));auto w=scene(14);w.route.epoch++;
        auto d=f.decide(w);check(d.eligible&&d.ticket.candidate.lessonStep==1,"reroute retained duplicate ledger or skipped preparation");
        check(!f.p->task().progress().planningDelivered&&f.p->task().progress().unassessedOutcomes==1,"reroute inferred completion");
    });
    test("source transition invalidates pending instruction and outcome remains unknown",[]{
        Fixture f(true);auto w=scene(1);check(f.decide(w).eligible&&f.p->accept(1,56,w,1,true),"fixture");
        f.p->beginSource(2);w.sourceId=2;check(!f.p->playbackValid(1,56,w,1,1),"old source played");
        check(f.p->task().progress().unassessedOutcomes==1,"interruption called success");
    });
    test("interrupted audio can be retried only with fresh planning evidence",[]{
        Fixture f(true);auto w=scene(1);check(f.decide(w).eligible&&f.p->accept(1,56,w,1,true),"fixture");
        check(f.p->playbackStarted(1,56,w,1,1)&&f.p->deliveryEvent(1,56,DeliveryStage::Interrupted,2),"interrupt");
        check(!f.p->deliveryEvent(1,56,DeliveryStage::Completed,3),"late callback restored completion");
        check(f.decide(scene(3,12)).eligible,"fresh retry lost");
        auto d=f.decide(scene(4,8));check(!d.eligible||d.ticket.candidate.lessonStep==1,"late retry crammed into approach");
    });
    test("out of order camera is not fresh task evidence",[]{
        Fixture f;f.decide(scene(1));auto w=scene(2);w.frameSequence=1;check(!f.decide(w).eligible,"old frame sequence allowed lesson");
        Fixture queued(true);w=scene(1);check(queued.decide(w).eligible&&queued.p->accept(1,56,w,1,true),"queue");
        w=scene(1.1);w.frameSequence=1;queued.decide(w);check(!queued.p->playbackValid(1,56,w,1.1,1),"queued lesson ignored rejected current camera");
    });
    test("route loss closes opportunity once without claiming manoeuvre success",[]{
        Fixture f;f.visual(scene(1));auto w=scene(2);w.route={};f.decide(w);w.now=w.captureTime=w.telemetry.time=3;f.decide(w);
        check(f.p->task().progress().unassessedOutcomes==1,"route loss count or invented success");
    });
    test("disabled task rejects forged lesson and preserves map bend advice",[]{
        Fixture f;f.p->enableJunctionLesson(false);auto w=scene(1);Candidate c;c.prompt=Prompt::junctionPlanLeft;c.lessonStep=1;c.lessonEpoch=2;c.origin=5;c.kind=EventKind::Route;
        c.identity={IdentitySpace::Route,9,17};c.captureTime=1;c.maxAge=3;c.expiresAt=4;c.supported=true;w.candidates[w.candidateCount++]=c;
        check(!f.decide(w).eligible,"forged disabled lesson");
        Fixture bend;w=scene(1,30,30,1);c={};c.prompt=Prompt::mapLeftBend;c.origin=5;c.kind=EventKind::Route;c.identity={IdentitySpace::Route,9,17};c.captureTime=1;c.maxAge=3;c.expiresAt=4;c.supported=true;w.candidates[0]=c;w.candidateCount=1;
        check(bend.decide(w).eligible,"junction pilot suppressed non-junction map context");
    });
    test("C API task status is explicit and aggregate only",[]{
        std::unique_ptr<ld_engine,decltype(&ld_destroy)> e(ld_create(),ld_destroy);
        check(!ld_coaching_task_speech_allowed(e.get()),"off speech");ld_coaching_junction_lesson(e.get(),1);
        const std::string s=ld_coaching_task_json(e.get());check(s.find("WAITING_FOR_ROUTE")!=std::string::npos&&s.find("routeId")==std::string::npos,"task status leaked route");
    });
    std::cout<<passed<<" task-coach groups passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
