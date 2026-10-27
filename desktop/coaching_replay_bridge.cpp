// Structured candidate-stage harness. No pixels, GPS acquisition, calibration,
// provider content, or instructor truth is inferred from these supplied fields.
#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/c_api.h"
#include "learndrive/coaching/HybridPolicy.hpp"
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#define COACH_EXPORT extern "C" __declspec(dllexport)
#else
#define COACH_EXPORT extern "C" __attribute__((visibility("default")))
#endif
using namespace ld;
using namespace ld::coaching;
namespace {
struct Replay {
    CoachingEngine engine;
    HybridPolicy hybrid;
    SafetyValidator validator;
    std::unique_ptr<ld_engine, decltype(&ld_destroy)> legacy{ld_create(), ld_destroy};
    WorldState world{};
    bool baseline{}, configured{}, voice{};
    uint64_t source{}, revision{}, sequence{};
    int selected{}, selectedIndex=-1;
    ld::coaching::Decision decision{};
    std::string json;
    explicit Replay(bool old):baseline(old) {if(!legacy)throw std::runtime_error("allocation");}
};
Identity identity(const double* p) {return {static_cast<IdentitySpace>(int(p[0])),uint64_t(p[1]),uint64_t(p[2])};}
std::string number(double v) {if(!std::isfinite(v))return "null";std::ostringstream s;s.precision(17);s<<v;return s.str();}
}
COACH_EXPORT int coaching_replay_contract_version() {return 1;}
COACH_EXPORT const char* coaching_replay_context(Replay* r) {if(!r)return nullptr;r->json=r->engine.interpretedSceneJson();return r->json.c_str();}
COACH_EXPORT void coaching_replay_use_hybrid(Replay* r) {if(r)r->engine.setPolicy(&r->hybrid);}
COACH_EXPORT int coaching_replay_proposal(Replay* r,int mode,int prompt,int space,uint64_t id,uint64_t scope,
    uint64_t source,uint64_t revision,uint64_t generation,double created,double expires) {
    if(!r)return 0;
    return r->hybrid.propose({static_cast<Mode>(mode),static_cast<Prompt>(prompt),{static_cast<IdentitySpace>(space),id,scope},source,revision,generation,created,expires,true});
}
COACH_EXPORT void* coaching_replay_create(int baseline) {try{return new Replay(baseline!=0);}catch(...){return nullptr;}}
COACH_EXPORT void coaching_replay_destroy(Replay* r) {delete r;}
COACH_EXPORT int coaching_replay_configure(Replay* r,int profile,int voice,uint64_t revision) {
    if(!r||!r->engine.configure(profile,voice!=0,revision))return 0;
    r->configured=true;r->voice=voice!=0;r->revision=revision;return 1;
}
// header[34], actors[N][15], candidates[N][21], all described by Python's
// coaching_contract.py. Null unknowns map to +/-infinity, never fabricated zero.
COACH_EXPORT int coaching_replay_world(Replay* r,const double* h,size_t hn,
                                      const double* actors,size_t an,const double* candidates,size_t cn) {
    if(!r||!h||hn!=34||an>64||cn>8||(an&&!actors)||(cn&&!candidates)||
       !std::isfinite(h[2])||h[2]<0||h[0]<1||h[0]<double(r->source))return 0;
    WorldState w{};w.sourceId=uint64_t(h[0]);w.frameSequence=uint64_t(h[1]);w.now=h[2];
    w.telemetry={h[3],h[4],h[5],h[6],0,bool(h[7]),bool(h[8]),bool(h[9]),bool(h[10]),bool(h[11]),bool(h[12])};
    w.active=h[13];w.audioReady=h[14];w.allowCritical=h[15];w.simulation=h[16];
    w.route={uint64_t(h[17]),uint64_t(h[18]),uint64_t(h[19]),h[20],h[21],h[22],h[26],int(h[23]),bool(h[24]),bool(h[25])};
    w.roadCaptureTime=h[27];w.engineCaptureTime=h[28];w.roadBend=int(h[29]);w.gradeBand=int(h[30]);w.roadValid=h[31];w.engineValid=h[32];
    w.captureTime=h[33];
    w.actorCount=an;w.candidateCount=cn;
    for(size_t i=0;i<an;++i) {const auto* p=actors+i*15;w.actors[i]={identity(p),p[3],p[4],p[5],int(p[6]),bool(p[7]),bool(p[8]),bool(p[9]),bool(p[10]),float(p[11]),float(p[12]),float(p[13]),float(p[14])};}
    for(size_t i=0;i<cn;++i) {
        const auto* p=candidates+i*21;auto& c=w.candidates[i];
        c.prompt=static_cast<Prompt>(int(p[0]));c.origin=int(p[1]);c.identity=identity(p+2);c.kind=static_cast<EventKind>(int(p[5]));
        c.captureTime=p[6];c.maxAge=p[7];c.expiresAt=p[8];c.confidence=p[9];c.tti=p[10];
        c.supported=p[11];c.urgent=p[12];c.critical=p[13];c.mechanicalMeasured=p[14];
        c.actorCategory=int(p[15]);c.evidenceKind=int(p[16]);c.x=float(p[17]);c.y=float(p[18]);c.w=float(p[19]);c.h=float(p[20]);
    }
    if(w.sourceId!=r->source) {
        r->source=w.sourceId;r->engine.beginSource(r->source);
        if(!ld_guidance_begin(r->legacy.get(),r->source,LD_SOURCE_SIMULATION,w.now))return 0;
    }
    r->world=w;return 1;
}
COACH_EXPORT const char* coaching_replay_decide(Replay* r,uint64_t sequence) {
    if(!r||!r->configured)return nullptr;
    r->sequence=sequence;r->selected=0;r->selectedIndex=-1;
    int mode=0,block=0;uint64_t event=0;double capture=-infinity;bool eligible=false;
    if(!r->baseline) {
        r->decision=r->engine.decide(r->world,sequence);const auto& d=r->decision;
        mode=int(d.mode);block=int(d.blocked);eligible=d.eligible;
        if(d.ticket.valid){r->selected=int(d.ticket.candidate.prompt);capture=d.ticket.candidate.captureTime;event=d.ticket.eventNumber;}
        for(size_t i=0;i<r->world.candidateCount;++i)if(r->world.candidates[i].identity==d.ticket.candidate.identity&&
            r->world.candidates[i].prompt==d.ticket.candidate.prompt)r->selectedIndex=int(i);
    } else {
        // Exact existing global/phrase audio gate after supplied candidate-stage
        // evidence validation. CameraCoach's detector-derived distinct-entry
        // exception is unavailable here; the report explicitly declares it.
        for(size_t i=0;i<r->world.candidateCount;++i) {
            const auto& c=r->world.candidates[i];const auto valid=r->validator.validate(c,r->world,r->world.now);
            if(valid!=Block::None){if(!r->selected)block=int(valid);continue;}
            if(priority(c.prompt)>priority(static_cast<Prompt>(r->selected))) {
                r->selected=int(c.prompt);r->selectedIndex=int(i);capture=c.captureTime;
            }
        }
        if(r->selected) {
            eligible=ld_audio_allowed(r->legacy.get(),r->selected,r->world.now)!=0;
            block=eligible?0:int(Block::Cooldown);
            mode=eligible?(r->world.candidates[size_t(r->selectedIndex)].urgent?2:1):3;
        }
    }
    std::ostringstream out;out.precision(17);
    out<<"{\"mode\":"<<mode<<",\"blocked\":"<<block<<",\"prompt\":"<<r->selected
       <<",\"eligible\":"<<(eligible?"true":"false")<<",\"sequence\":"<<sequence
       <<",\"source_id\":"<<r->source<<",\"revision\":"<<r->revision<<",\"event_number\":"<<event
       <<",\"capture_time\":"<<number(capture)<<",\"candidate_index\":"<<r->selectedIndex<<"}";
    r->json=out.str();return r->json.c_str();
}
COACH_EXPORT int coaching_replay_accept(Replay* r,uint64_t sequence,int prompt,double now,int voice) {
    if(!r)return 0;
    if(!r->baseline)return r->engine.accept(sequence,prompt,r->world,now,voice!=0);
    if(sequence!=r->sequence||prompt!=r->selected||r->selectedIndex<0||
       r->validator.validate(r->world.candidates[size_t(r->selectedIndex)],r->world,now)!=Block::None||
       !ld_audio_allowed(r->legacy.get(),prompt,now))return 0;
    ld_audio_accepted(r->legacy.get(),prompt,now);return 1;
}
COACH_EXPORT int coaching_replay_playback(Replay* r,uint64_t sequence,int prompt,double now,uint64_t revision) {
    if(!r)return 0;
    if(!r->baseline){const bool started=r->engine.playbackStarted(sequence,prompt,r->world,now,revision);
        if(!started)r->engine.deliveryEvent(sequence,prompt,DeliveryStage::Expired,now);return started;}
    return sequence==r->sequence&&prompt==r->selected&&revision==r->revision&&r->voice&&r->selectedIndex>=0&&
        r->validator.validate(r->world.candidates[size_t(r->selectedIndex)],r->world,now)==Block::None;
}
COACH_EXPORT int coaching_replay_delivery(Replay* r,uint64_t sequence,int prompt,double now,int stage) {
    return r&&!r->baseline&&r->engine.deliveryEvent(sequence,prompt,static_cast<DeliveryStage>(stage),now);
}
COACH_EXPORT int coaching_replay_validate(Replay* r,int candidate,double now) {
    if(!r||candidate<0||size_t(candidate)>=r->world.candidateCount)return int(Block::Unsupported);
    return int(r->validator.validate(r->world.candidates[size_t(candidate)],r->world,now));
}
COACH_EXPORT const char* coaching_replay_diagnostics(Replay* r) {
    if(!r)return nullptr;r->json=r->engine.diagnosticsJson(r->world.now);return r->json.c_str();
}
