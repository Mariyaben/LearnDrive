#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/c_api.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace ld;
using namespace ld::coaching;
namespace {
int passed=0;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void test(const char* name,F f){f();++passed;std::cout<<"PASS "<<name<<'\n';}
WorldState world(double now,uint64_t source=1) {
    WorldState w;w.now=now;w.sourceId=source;w.frameSequence=uint64_t(now*1000)+1;w.captureTime=now;
    w.telemetry={now,30,0,0,1,true,true,true,false,false,false};w.active=true;w.audioReady=true;return w;
}
Candidate attention(WorldState& w,uint64_t id=7,bool urgent=false,int origin=4,double capture=-1) {
    if(capture<0)capture=w.now;
    ActorObservation a;a.identity={IdentitySpace::Attention,id,0};a.captureTime=capture;a.confidence=.9;a.category=0;
    a.relevant=true;a.inPath=urgent;a.opticalClosing=urgent;a.tti=urgent?2.0:infinity;a.x=.4f;a.y=.3f;a.w=.1f;a.h=.4f;
    w.actors[w.actorCount++]=a;
    Candidate c;c.prompt=Prompt::pedestrianSpace;c.origin=origin;c.identity=a.identity;c.kind=urgent?EventKind::Approach:EventKind::Attention;
    c.captureTime=capture;c.expiresAt=capture+.35;c.supported=true;c.confidence=.9;c.tti=a.tti;c.urgent=urgent;c.actorCategory=0;c.evidenceKind=urgent?2:1;
    c.x=a.x;c.y=a.y;c.w=a.w;c.h=a.h;return c;
}
Candidate route(WorldState& w,uint64_t event=5,Prompt prompt=Prompt::mapLeftTurn) {
    w.route.routeId=99;w.route.eventId=event;w.route.epoch=2;w.route.captureTime=w.now;w.route.gpsTime=w.now;
    w.route.distanceM=40;w.route.feature=4;w.route.valid=true;w.route.provider=true;
    Candidate c;c.prompt=prompt;c.origin=5;c.identity={IdentitySpace::Route,event,99};c.kind=EventKind::Route;
    c.captureTime=w.now;c.maxAge=3;c.expiresAt=w.now+3;c.supported=true;return c;
}
void put(WorldState& w,const Candidate& c){w.candidates[w.candidateCount++]=c;}
struct EngineFixture {
    CoachingEngine e;
    EngineFixture(bool voice=true){check(e.configure(1,voice,1),"configure");e.beginSource(1);}
};
using Native=std::unique_ptr<ld_engine,decltype(&ld_destroy)>;
ld_guidance_request request(double now,uint64_t source=1) {
    ld_guidance_request q{};q.source_id=source;q.now=now;q.telemetry={now,30,0,0,1,1,1,1,0,0,0};q.active=1;return q;
}
struct Frames {
    std::vector<uint8_t> y=std::vector<uint8_t>(64*48,80),uv=std::vector<uint8_t>(32*24,128);
    std::vector<float> input=std::vector<float>(3*320*320);std::array<float,84> box{};
    void publish(ld_engine* e,double time,uint64_t source=1,bool person=true) {
        box={};if(person){box[0]=160;box[1]=160;box[2]=25.6f;box[3]=96;box[4]=.95f;}
        check(ld_preprocess(e,y.data(),y.size(),64,1,uv.data(),uv.size(),32,1,uv.data(),uv.size(),32,1,64,48,0,320,input.data(),input.size()*4,0,1,1,0,0,1),"prepare");
        double hazard[3];check(ld_spatial_semantics_source(e,source,time,box.data(),box.size(),1,1,hazard),"labels");
    }
};
}
int main(){try {
    test("configuration is monotonic and explicitly opt-in",[]{
        CoachingEngine e;check(!e.configured()&&e.voice()&&e.interval()==12,"default settings");
        check(!e.configure(1,true,0)&&!e.configure(3,true,1),"invalid configuration accepted");
        check(e.configure(1,true,1)&&e.configure(1,true,1),"idempotent configuration rejected");
        check(!e.configure(0,true,1)&&!e.configure(1,false,1),"same revision mutated settings");
        check(e.configure(0,false,2)&&e.interval()==20&&!e.voice(),"minimal configuration");
        check(!e.configure(1,true,1),"old configuration recovered");
    });
    test("muted logical acceptance is independent of PCM readiness",[]{
        EngineFixture f(false);auto w=world(1);w.audioReady=false;put(w,attention(w));auto d=f.e.decide(w,1);
        check(d.eligible&&!w.audioReady,"muted logical decision missing");
        check(!f.e.accept(1,44,w,1,true),"muted PCM accepted");check(f.e.accept(1,44,w,1,false),"logical accept failed");
        check(!f.e.accept(1,44,w,1,false),"ticket accepted twice");check(!f.e.playbackValid(1,44,w,1,1),"muted playback allowed");
        check(f.e.stats().logical==1&&f.e.stats().voiceSubmitted==0&&f.e.stats().playbackOnsets==0,"delivery states conflated");
    });
    test("accepted ticket survives later selection but never evidence expiry",[]{
        EngineFixture f;auto w=world(1);put(w,attention(w));auto d=f.e.decide(w,1);check(d.eligible,"fixture");
        check(f.e.accept(1,44,w,1,true),"accept");auto next=world(1.1);put(next,attention(next));f.e.decide(next,2);
        check(f.e.playbackValid(1,44,next,1.1,1),"later decision cancelled accepted ticket");
        check(f.e.playbackStarted(1,44,next,1.1,1)&&!f.e.playbackStarted(1,44,next,1.1,1),"onset not exactly once");
        check(!f.e.playbackValid(1,44,next,1.1,1),"already started ticket permitted another PCM onset");
        next.telemetry.time=1.36;next.now=1.36;check(!f.e.playbackValid(1,44,next,1.36,1),"old capture was refreshed by new decision");
        check(f.e.stats().voiceSubmitted==1&&f.e.stats().playbackOnsets==1,"submission/onset stats");
    });
    test("original tactical and reflex capture expires at every gate",[]{
        for(int origin:{2,1}) {
            EngineFixture f;auto w=world(1.30);w.captureTime=1.0;auto c=attention(w,7,true,origin,1.0);
            c.prompt=origin==2?Prompt::brake:Prompt::watchPedestrians;
            c.identity.space=IdentitySpace::Optical;w.actors[0].identity.space=IdentitySpace::Optical;put(w,c);const int prompt=int(c.prompt);
            auto d=f.e.decide(w,1);check(d.eligible,"fresh late-stage decision rejected");
            auto later=w;later.now=1.36;later.telemetry.time=1.36;
            check(!f.e.pending(1,prompt,later,1.36),"decision time extended original tactical/reflex capture");
            check(!f.e.accept(1,prompt,later,1.36,true),"expired capture accepted");
            check(f.e.accept(1,prompt,w,1.30,true),"fresh acceptance failed");
            check(!f.e.playbackValid(1,prompt,later,1.36,1),"expired accepted tactical/reflex capture played");
        }
    });
    test("routine intervals persist across settings and source changes",[]{
        EngineFixture f;auto w=world(1);put(w,attention(w));check(f.e.decide(w,1).eligible,"fixture");check(f.e.accept(1,44,w,1,false),"accept");
        auto n=world(5);put(n,attention(n,8));auto d=f.e.decide(n,2);check(d.mode==Mode::DEFER&&!d.eligible,"Balanced12 interval lost");
        check(f.e.configure(2,true,2),"Guided config");f.e.beginSource(2);n=world(5,2);put(n,attention(n,9));
        check(!f.e.decide(n,3).eligible,"settings/source reset routine cooldown");
        n=world(9,2);put(n,attention(n,9));check(f.e.decide(n,4).eligible,"Guided8 interval not applied");
        check(!f.e.playbackValid(1,44,n,9,1),"old source/revision ticket survived");
    });
    test("new independently supported urgent event bypasses routine cooldown",[]{
        EngineFixture f;auto w=world(1);put(w,attention(w));check(f.e.decide(w,1).eligible&&f.e.accept(1,44,w,1,false),"routine fixture");
        auto n=world(1.1);put(n,attention(n,8,true));auto d=f.e.decide(n,2);
        check(d.eligible&&d.mode==Mode::WARN,"new urgent event hidden by routine pacing");
        check(f.e.accept(2,44,n,1.1,false),"urgent acceptance");
        n=world(1.2);auto unsupported=attention(n,9,true);n.actors[0].opticalClosing=false;put(n,unsupported);
        check(!f.e.decide(n,3).eligible,"unconfirmed urgency bypassed validator");
    });
    test("same urgent encounter needs worsening evidence before another warning",[]{
        EngineFixture f;auto w=world(1);auto c=attention(w,7,true);c.tti=4;w.actors[0].tti=4;put(w,c);
        check(f.e.decide(w,1).eligible&&f.e.accept(1,44,w,1,false),"first urgent cue");
        auto n=world(4);c=attention(n,7,true);c.tti=4;n.actors[0].tti=4;put(n,c);
        check(!f.e.decide(n,2).eligible,"unchanged warning repeated after timer alone");
        n=world(4.1);c=attention(n,7,true);c.tti=2;n.actors[0].tti=2;put(n,c);
        check(f.e.decide(n,3).eligible,"materially worsening current evidence suppressed");
    });
    test("route phases share one maneuver ledger identity",[]{
        EngineFixture f;auto w=world(1);put(w,route(w));check(f.e.decide(w,1).eligible&&f.e.accept(1,50,w,1,false),"turn fixture");
        auto n=world(20);auto near=route(n,5,Prompt::mapLeftTurnNear);n.route.provider=false;near.maxAge=2.5;near.expiresAt=22.5;put(n,near);auto d=f.e.decide(n,2);
        check(!d.eligible&&d.blocked==Block::Duplicate,"distance phrase created a duplicate turn event");
        n=world(21);n.route.valid=false;f.e.decide(n,3);check(f.e.ledger().resolvedCount()>=1,"resolved route retained active");
        n=world(22);put(n,route(n,6));check(f.e.decide(n,4).eligible,"new route event suppressed as old turn");
    });
    test("navigation identity and independent GPS are validated at onset",[]{
        EngineFixture f;auto w=world(1);put(w,route(w));check(f.e.decide(w,1).eligible&&f.e.accept(1,50,w,1,true),"turn fixture");
        auto n=w;n.now=1.1;n.telemetry.time=1.1;n.route.eventId=6;check(!f.e.playbackValid(1,50,n,1.1,1),"different route event accepted");
        n=w;n.now=3.6;n.telemetry.time=3.6;n.route.captureTime=3.5;check(!f.e.playbackValid(1,50,n,3.6,1),"fresh feed hid stale independent GPS");
        n=world(4);auto c=route(n);n.route.feature=5;put(n,c);check(!f.e.decide(n,2).eligible,"contradictory turn direction accepted");
    });
    test("mechanical prompts fall back to bounded neutral recorded wording",[]{
        EngineFixture f;auto w=world(1);w.telemetry.pitchDegrees=6;w.gradeBand=2;
        Candidate c;c.prompt=Prompt::hill;c.origin=3;c.identity={IdentitySpace::Slope,7,0};c.kind=EventKind::Slope;c.captureTime=1;c.expiresAt=1.35;c.supported=true;put(w,c);
        auto d=f.e.decide(w,1);check(d.eligible&&d.ticket.candidate.prompt==Prompt::uphill,"unmeasured gear wording retained");
        check(f.e.accept(1,9,w,1,false),"neutral accept");w.now=1.1;w.telemetry.time=1.1;w.telemetry.pitchValid=false;
        check(!f.e.playbackValid(1,9,w,1.1,1),"invalid pitch supported old slope cue");
        check(SafetyValidator::neutral(Prompt::shiftDown,false)==Prompt::none,"unmeasured RPM shift fabricated");
    });
    test("critical warnings remain default off and require optical proof",[]{
        EngineFixture f;auto w=world(1);auto c=attention(w,7,true);c.prompt=Prompt::emergency;c.critical=true;c.kind=EventKind::Critical;put(w,c);
        check(!f.e.decide(w,1).eligible,"critical command on by default");w.allowCritical=true;
        check(f.e.decide(w,2).eligible,"explicit supported research command rejected");w.actors[0].opticalClosing=false;
        check(!f.e.decide(w,3).eligible,"class-width/model probability became collision proof");
    });
    test("memory and ID registry are bounded and do not refresh evidence",[]{
        EngineFixture f;
        for(int i=0;i<=150;++i){auto w=world(i*.1);w.telemetry.speedKmh=40-i*.1;put(w,attention(w));f.e.decide(w,uint64_t(i+1));}
        check(f.e.memory().size(15)<=101&&f.e.memory().size(26)==0,"ten-second memory unbounded");
        check(f.e.memory().speedTrend(15)==SpeedTrend::Decreasing,"measured speed trend missing");
        check(f.e.memory().speedTrend(16)==SpeedTrend::Unknown,"historical trend became current pedal response");
        check(f.e.tracks().count(15)==1,"temporal adapter did not retain native identity");
        const auto* h=f.e.tracks().find({int(IdentitySpace::Attention),1,7},15);check(h&&h->count>1&&h->count<=41,"tracked position history missing");
        auto old=world(15.1);old.captureTime=14;old.frameSequence=14001;put(old,attention(old));check(!f.e.decide(old,200).eligible,"out-of-order capture accepted");
    });
    test("imminent turn defers unrelated routine cues but not warnings",[]{
        EngineFixture f;auto w=world(1);route(w);w.route.distanceM=10;put(w,attention(w));
        auto d=f.e.decide(w,1);check(d.mode==Mode::DEFER&&!d.eligible,"routine interruption during turn");
        auto urgent=world(1.1);route(urgent);urgent.route.distanceM=10;put(urgent,attention(urgent,8,true));
        check(f.e.decide(urgent,2).eligible,"maneuver deferral hid urgent event");
        auto stale=world(4);route(stale);stale.route.distanceM=10;stale.route.gpsTime=1;put(stale,attention(stale,9));
        check(f.e.decide(stale,3).eligible,"stale route deferred valid current cue");
    });
    test("measured speed trend suppresses only redundant routine speed cues",[]{
        EngineFixture f;
        for(int i=0;i<=20;++i){auto w=world(i*.1);w.telemetry.speedKmh=40-i*.3;f.e.decide(w,uint64_t(i+1));}
        auto w=world(2.1);w.telemetry.speedKmh=33.7;auto c=attention(w,7,false,2);c.prompt=Prompt::ease;c.kind=EventKind::Approach;c.identity.space=IdentitySpace::Optical;
        c.tti=6;w.actors[0].identity.space=IdentitySpace::Optical;w.actors[0].opticalClosing=true;w.actors[0].inPath=true;w.actors[0].tti=6;put(w,c);
        check(!f.e.decide(w,30).eligible&&f.e.memory().speedTrend(2.1)==SpeedTrend::Decreasing,"decreasing speed did not quiet redundant speed cue");
        auto unknown=world(2.2);unknown.telemetry.speedValid=false;auto other=attention(unknown,8);put(unknown,other);
        check(f.e.decide(unknown,31).eligible&&f.e.memory().speedTrend(2.2)==SpeedTrend::Unknown,"unknown speed interpreted as correct response");
    });
    test("deferred slot never becomes a stale FIFO",[]{
        EngineFixture f;auto w=world(1);put(w,attention(w));check(f.e.decide(w,1).eligible&&f.e.accept(1,44,w,1,false),"fixture");
        auto delayed=world(5);put(delayed,attention(delayed,8));check(f.e.decide(delayed,2).mode==Mode::DEFER,"defer fixture");
        auto empty=world(14);check(!f.e.decide(empty,3).eligible,"expired deferred event replayed");
        auto fresh=world(14.1);put(fresh,attention(fresh,9));auto d=f.e.decide(fresh,4);check(d.eligible&&d.ticket.candidate.identity.id==9,"fresh event lost behind expired slot");
    });
    test("independent validator rejects forged policy envelopes",[]{
        SafetyValidator validator;auto w=world(1);auto c=attention(w);
        auto bad=c;bad.identity.space=IdentitySpace::None;check(validator.validate(bad,w,1)!=Block::None,"identity bypassed actor validation");
        bad=c;bad.origin=2;bad.identity.space=IdentitySpace::Tactical;check(validator.validate(bad,w,1)!=Block::None,"live tactical identity forged simulation");
        bad=c;bad.maxAge=infinity;check(validator.validate(bad,w,1)!=Block::None,"infinite freshness accepted");
        bad=c;bad.expiresAt=infinity;check(validator.validate(bad,w,1)!=Block::None,"unbounded expiry accepted");
        bad=c;bad.confidence=std::nan("");check(validator.validate(bad,w,1)!=Block::None,"NaN confidence accepted");
        bad=c;bad.origin=5;bad.identity.space=IdentitySpace::Route;check(validator.validate(bad,w,1)!=Block::None,"origin/kind mismatch accepted");
        w.actors[0].category=2;check(validator.validate(c,w,1)!=Block::None,"person wording applied to vehicle");
        check(SafetyValidator::neutral(Prompt::closeGear,false)==Prompt::close&&SafetyValidator::neutral(Prompt::imageCloseGear,false)==Prompt::imageClose,"neutral fallback invented road user");
    });
    test("replaceable policy outputs cannot change validation authority",[]{
        struct Policy final:DecisionPolicy {Mode mode;explicit Policy(Mode m):mode(m){}PolicyChoice choose(const PolicyContext&) const noexcept override{return {mode,0};}};
        EngineFixture f;auto w=world(1);put(w,attention(w));Policy invalid(static_cast<Mode>(99));f.e.setPolicy(&invalid);
        check(!f.e.decide(w,1).eligible,"invalid policy mode accepted");Policy warn(Mode::WARN);f.e.setPolicy(&warn);
        check(!f.e.decide(w,2).eligible,"policy promoted routine cue into warning");
        auto urgent=world(1.1);put(urgent,attention(urgent,8,true));Policy downgrade(Mode::COACH);f.e.setPolicy(&downgrade);
        check(!f.e.decide(urgent,3).eligible,"policy downgraded validated urgent semantics");
    });
    test("independent mechanical and bend evidence cannot be forged by a candidate",[]{
        SafetyValidator validator;auto w=world(1);w.gradeBand=2;w.telemetry.pitchDegrees=6;
        Candidate c;c.prompt=Prompt::hill;c.origin=3;c.identity={IdentitySpace::Slope,7,0};c.kind=EventKind::Slope;
        c.captureTime=1;c.expiresAt=1.35;c.supported=true;c.mechanicalMeasured=true;
        check(validator.validate(c,w,1)==Block::Unsupported,"candidate flag invented gear evidence");
        c.prompt=Prompt::shiftUp;c.kind=EventKind::Shift;c.identity={IdentitySpace::Engine,1,2};
        w.engineValid=true;w.engineCaptureTime=1;
        check(validator.validate(c,w,1)==Block::Unsupported,"engine availability flag invented RPM and gear");
        w.engineGear=2;w.engineMaxGear=6;w.engineRpm=3500;w.engineLowerRpm=1200;w.engineUpperRpm=3000;w.engineUnderLoad=true;
        check(validator.validate(c,w,1)==Block::None,"explicit qualified shift evidence rejected");
        w.engineRpm=2000;check(validator.validate(c,w,1)==Block::Unsupported,"in-band RPM justified shift");
        w.roadValid=true;w.roadCaptureTime=1;w.roadBend=1;
        c.prompt=Prompt::bendLeftQuiet;c.mechanicalMeasured=false;c.kind=EventKind::Bend;c.identity={IdentitySpace::Bend,3,0};
        check(validator.validate(c,w,1)==Block::Unsupported,"opposite bend direction justified phrase");
    });
    test("recording test speed never becomes measured response memory",[]{
        Native e(ld_create(),ld_destroy);check(ld_guidance_begin(e.get(),1,LD_SOURCE_RECORDING,0),"source");
        check(ld_coaching_configure(e.get(),1,0,1),"configure");ld_guidance_result d{};
        for(int i=0;i<25;++i){auto q=request(1+i*.1);q.telemetry.speed=40-i;check(ld_guidance_decide(e.get(),&q,&d),"tick");}
        double diag[20];check(ld_coaching_diagnostics(e.get(),3.4,diag,20)==20&&diag[18]==0,"manual recording setting claimed measured speed trend");
    });
    test("C API muted current-source actor acceptance and onset isolation",[]{
        Native e(ld_create(),ld_destroy);check(ld_guidance_begin(e.get(),1,LD_SOURCE_CAMERA,0),"source");check(ld_coaching_configure(e.get(),1,0,1),"configure");
        Frames frames;frames.publish(e.get(),1);auto q=request(1.02);ld_guidance_result d{};check(ld_guidance_decide(e.get(),&q,&d),"decision");
        frames.publish(e.get(),1.125);q=request(1.145);check(ld_guidance_decide(e.get(),&q,&d)&&d.eligible&&d.prompt==44,"logical cue requires audio");
        check(ld_coaching_accept(e.get(),d.sequence,d.prompt,1.145,0),"logical actor accept");
        check(!ld_coaching_playback_valid(e.get(),d.sequence,d.prompt,1.145,1),"muted accepted cue audible");
        double diag[20];check(ld_coaching_diagnostics(e.get(),1.145,diag,20)==20&&diag[12]==1&&diag[13]==0,"delivery diagnostics");
        check(ld_coaching_configure(e.get(),1,1,2),"revision");check(!ld_coaching_playback_valid(e.get(),d.sequence,d.prompt,1.145,1),"old revision accepted");
        check(ld_guidance_begin(e.get(),2,LD_SOURCE_CAMERA,2),"new source");q=request(2.1,1);check(ld_guidance_decide(e.get(),&q,&d)&&!d.eligible,"stale source became current");
    });
    test("C API simulation tactical uses original hazard capture at acceptance",[]{
        Native e(ld_create(),ld_destroy);check(ld_guidance_begin(e.get(),1,LD_SOURCE_SIMULATION,0),"source");check(ld_coaching_configure(e.get(),1,1,1),"configure");
        auto q=request(1.3);q.hazard_time=1;q.tti=2;q.confidence=.9;q.in_path=1;q.audio_ready=1;ld_guidance_result d{};
        check(ld_guidance_decide(e.get(),&q,&d)&&d.eligible&&d.origin==LD_GUIDANCE_TACTICAL,"tactical fixture");
        check(!ld_coaching_accept(e.get(),d.sequence,d.prompt,1.36,1),"native tactical decision timestamp extended evidence");
    });
    std::cout<<passed<<" coaching groups passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
