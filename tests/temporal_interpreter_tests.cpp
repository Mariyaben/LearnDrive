#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/HybridPolicy.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ld;using namespace ld::coaching;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
WorldState world(double time,bool actor=true) {
    WorldState w;w.now=time;w.sourceId=1;w.frameSequence=uint64_t(time*1000)+1;w.captureTime=time;
    w.telemetry={time,30,0,0,1,true,true,true,false,false,false};w.active=true;w.audioReady=true;
    if(actor){w.actorCount=1;auto& a=w.actors[0];a.identity={IdentitySpace::Attention,7,0};a.captureTime=time;a.category=0;a.confidence=.9;a.relevant=true;a.x=.4f;a.y=.3f;a.w=.1f;a.h=.4f;}
    return w;
}
Candidate cue(const WorldState& w){Candidate c;c.prompt=Prompt::pedestrianSpace;c.origin=4;c.identity=w.actors[0].identity;c.kind=EventKind::Attention;c.captureTime=w.now;c.expiresAt=w.now+.35;c.supported=true;c.confidence=.9;c.actorCategory=0;c.evidenceKind=1;return c;}
struct F {
    ShortMemory memory;EventLedger ledger;TemporalSceneInterpreter interpreter;
    const InterpretedScene& observe(const WorldState& w,uint64_t rev=1){memory.observe(w);return interpreter.observe(w,memory,ledger,rev);}
};
}
int main(){try {
    {F f;auto w=world(1);f.observe(w);for(int i=0;i<20;++i)f.observe(w);
     require(f.interpreter.scene().encounters[0].observations==1,"duplicate ticks manufactured temporal evidence");
     f.observe(world(1.15));auto& s=f.observe(world(1.3));require(s.encounters[0].phase==EncounterPhase::PotentiallyRelevant,"qualified continuing encounter not interpreted");
     auto generation=s.generation;require(f.observe(world(1.45)).generation==generation,"unchanged scene invoked reasoning every frame");}
    std::cout<<"PASS observation continuity and stable generation\n";
    {F f;f.observe(world(1));f.observe(world(1.15));f.observe(world(1.3));
     require(f.observe(world(1.7,false)).encounters[0].phase==EncounterPhase::TemporarilyLost,"occlusion invented resolution");
     require(f.observe(world(1.8)).encounters[0].phase==EncounterPhase::Continuing,"brief loss created a new actor");
     auto outside=world(1.9);outside.actors[0].relevant=false;require(f.observe(outside).encounters[0].phase==EncounterPhase::Resolved,"observed region exit not resolved");}
    std::cout<<"PASS occlusion and supported region exit are distinct\n";
    {F f;f.observe(world(1));auto stale=world(2);stale.actors[0].captureTime=1;require(!f.observe(stale).encounters[0].fresh,"stale actor became current");
     auto invalid=world(2.1);invalid.actors[0].x=NAN;auto text=f.interpreter.encode(invalid,f.ledger);require(text.find("nan")==std::string::npos,"encoder emitted invalid JSON number");
     auto next=world(3,false);next.sourceId=2;require(f.observe(next).count==0,"old source leaked encounters");}
    std::cout<<"PASS stale source and invalid geometry fail conservatively\n";
    {F f;auto c=cue(world(0));c.prompt=Prompt::mapRightTurn;c.kind=EventKind::Route;c.identity={IdentitySpace::Route,12,2};
     auto& event=f.ledger.observe(1,c,0);event.acceptedAt=0;event.acceptedPrompt=c.prompt;event.visualOnly=true;
     for(int i=0;i<=20;++i){auto w=world(i*.1,false);w.telemetry.speedKmh=40-i*.2;f.observe(w);}
     require(f.interpreter.scene().response==ResponseEvidence::Observed,"measured reduction after cue not represented");
     auto w=world(2.1,false);w.telemetry.speedValid=false;require(f.observe(w).response==ResponseEvidence::Uncertain,"unknown motion claimed response");}
    std::cout<<"PASS response evidence is measured and can become uncertain\n";
    {F f;auto w=world(1);auto c=cue(w);w.candidates[0]=c;w.candidateCount=1;f.observe(w);
     auto previous=c;previous.prompt=Prompt::roadUserQuiet;previous.kind=EventKind::Approach;
     auto& event=f.ledger.observe(1,previous,.8);event.acceptedAt=.8;event.acceptedPrompt=previous.prompt;event.voiceSubmitted=true;
     TemporalPolicy policy;auto choice=policy.choose({w,f.memory,&f.interpreter.scene(),&f.ledger});require(choice.mode==Mode::COACH,"submission alone claimed informed learner");
     event.startedAt=.9;event.deliveryStage=DeliveryStage::Completed;choice=policy.choose({w,f.memory,&f.interpreter.scene(),&f.ledger});require(choice.mode==Mode::SILENCE,"rephrased equivalent advice repeated");
     w.candidates[0].urgent=true;require(policy.choose({w,f.memory,&f.interpreter.scene(),&f.ledger}).mode==Mode::WARN,"history hid important current event");}
    std::cout<<"PASS semantic repetition uses actual delivery and preserves urgency\n";
    {F f;auto w=world(1);w.route.provider=true;w.route.routeId=424242;w.route.eventId=989898;w.route.valid=true;w.route.captureTime=w.route.gpsTime=1;
     f.observe(w);auto text=f.interpreter.encode(w,f.ledger);require(text.find("\"model_eligible\":false")!=std::string::npos,"provider context admitted to model");
     require(text.find("424242")==std::string::npos&&text.find("989898")==std::string::npos,"provider route content retained in model input");}
    std::cout<<"PASS provider context exclusion\n";
    {F f;auto w=world(1);auto c=cue(w);c.identity={IdentitySpace::Route,99991,99992};
     auto& event=f.ledger.observe(1,c,.5);event.acceptedAt=.5;event.acceptedPrompt=Prompt::mapRightTurn;event.visualOnly=true;event.providerContent=true;
     f.observe(w);const auto text=f.interpreter.encode(w,f.ledger);
     require(text.find("99991")==std::string::npos&&text.find("99992")==std::string::npos,"previous provider route leaked after switching offline");}
    std::cout<<"PASS provider history stays excluded after provider switch\n";
    {F f;auto w=world(1);w.candidates[0]=cue(w);w.candidateCount=1;auto generation=f.observe(w).generation;
     auto changed=world(1.1);changed.candidates[0]=cue(changed);changed.candidateCount=1;changed.actors[0].inPath=true;
     require(f.observe(changed).generation>generation,"new supported path did not invalidate reasoning");generation=f.interpreter.scene().generation;
     changed=world(1.2);changed.candidateCount=0;require(f.observe(changed).generation>generation,"changed choice set did not invalidate reasoning");
     generation=f.interpreter.scene().generation;changed=world(2);changed.captureTime=1.2;
     require(f.observe(changed).generation>generation&&!f.interpreter.scene().cameraFresh,"camera expiry did not invalidate reasoning");}
    std::cout<<"PASS material path, choice and freshness changes invalidate cache\n";
    {F f;auto w=world(1);w.candidates[0]=cue(w);w.candidateCount=1;f.observe(w);
     HybridPolicy policy;auto& s=f.interpreter.scene();
     BoundedModelProposal p;p.mode=Mode::SILENCE;p.source=1;p.revision=1;p.generation=s.generation;p.createdAt=1;p.expiresAt=3;p.valid=true;
     require(policy.propose(p),"bounded proposal refused");require(policy.choose({w,f.memory,&s,&f.ledger}).mode==Mode::SILENCE,"model quiet proposal disconnected");
     w.candidates[0].urgent=true;require(policy.choose({w,f.memory,&s,&f.ledger}).mode==Mode::WARN,"model suppressed native important event");
     w.candidates[0].urgent=false;p.generation=s.generation+1;policy.propose(p);
     require(policy.choose({w,f.memory,&s,&f.ledger}).mode==Mode::COACH,"old context generation controlled current event");
     p.generation=s.generation;p.expiresAt=1.1;policy.propose(p);w.now=1.2;
     require(policy.choose({w,f.memory,&s,&f.ledger}).mode==Mode::COACH,"expired model result remained authority");}
    std::cout<<"PASS hybrid binding, expiry and independent urgent fallback\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
