#include "learndrive/coaching/CoachingEngine.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

// Count ordinary C++ allocations only, excluding setup/diagnostic serialization.
// This is a host synthetic policy benchmark, not phone or inference latency.
namespace {std::atomic<size_t> allocations{0};std::atomic<bool> counting{false};}
void* operator new(std::size_t n) {
    if(counting.load(std::memory_order_relaxed))++allocations;
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main() {
    using namespace ld;using namespace ld::coaching;
    constexpr size_t ticks=20000;
    auto engine=std::make_unique<CoachingEngine>();
    engine->configure(1,false,1);engine->beginSource(1);
    std::vector<double> samples(ticks);
    WorldState w;w.sourceId=1;w.active=true;w.actorCount=1;w.candidateCount=1;
    size_t accepted=0;
    counting=true;
    for(size_t i=0;i<ticks;++i) {
        w.now=1+i*.01;w.captureTime=w.now;w.frameSequence=i+1;
        w.telemetry={w.now,30,0,0,1,true,true,true,false,false,false};
        auto& a=w.actors[0];a.identity={IdentitySpace::Attention,1+i/1250,0};
        a.captureTime=w.now;a.confidence=.9;a.category=0;a.relevant=true;
        a.x=.4f;a.y=.3f;a.w=.1f;a.h=.4f;
        auto& c=w.candidates[0];c.prompt=Prompt::pedestrianSpace;c.origin=4;
        c.identity=a.identity;c.kind=EventKind::Attention;c.captureTime=w.now;
        c.expiresAt=w.now+.35;c.supported=true;c.confidence=.9;c.actorCategory=0;c.evidenceKind=1;
        const auto start=std::chrono::steady_clock::now();
        auto d=engine->decide(w,i+1);
        if(d.eligible&&engine->accept(i+1,int(d.ticket.candidate.prompt),w,w.now,false))++accepted;
        samples[i]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
    }
    counting=false;
    std::sort(samples.begin(),samples.end());
    std::cout<<"{\"scope\":\"synthetic_x64_policy_only\",\"ticks\":"<<ticks
        <<",\"engine_bytes\":"<<sizeof(CoachingEngine)<<",\"world_bytes\":"<<sizeof(WorldState)
        <<",\"ordinary_cpp_allocations\":"<<allocations.load()<<",\"logical_accepted\":"<<accepted
        <<",\"p50_us\":"<<samples[ticks/2]<<",\"p95_us\":"<<samples[ticks*95/100]
        <<",\"p99_us\":"<<samples[ticks*99/100]<<",\"max_us\":"<<samples.back()
        <<",\"phone_performance_measured\":false}\n";
    return allocations.load()==0&&accepted>0?0:1;
}
