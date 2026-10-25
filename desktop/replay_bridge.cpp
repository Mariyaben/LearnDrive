// Headless video review uses the same model preprocessing, fusion and policies
// as the mobile apps. Camera calibration and telemetry must be supplied explicitly.
#include "learndrive/c_api.h"
#include "learndrive/perception/supercombo.hpp"
#include <cstring>
#include <string>
#include <stdexcept>
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
using namespace ld;
// Version 2 separates inference from publication and never re-dates old output
// when semantic labels arrive. Python rejects older bridge contracts explicitly.
EXPORT int replay_contract_version(){return 2;}
struct Replay {
    std::unique_ptr<ld_engine,decltype(&ld_destroy)> owner{ld_create(),ld_destroy};
    ld_engine* engine=owner.get();
    std::unique_ptr<perception::SupercomboRuntime> runtime;
    int width,height;
    perception::Matrix3 warp;
    double semanticTime=-infinity,hazard[3]{infinity,0,0};
    std::array<float,6472> output{};
    std::string error;
    ld_guidance_result guidance{};
    bool emergencies=false;
    bool spatialEnabled=false;
    Replay(const char* model,int w,int h,const double* geometry):width(w),height(h) {
        if(!engine)throw std::runtime_error("cannot allocate replay engine");
        if(!ld_guidance_begin(engine,1,LD_SOURCE_RECORDING,0))throw std::runtime_error("cannot start replay source");
        if(!geometry) {
            if(model&&*model)throw std::runtime_error("Supercombo requires explicit camera geometry");
            return; // Match Android MP4: semantic coaching without original optics.
        }
        spatialEnabled=true;
        if(!ld_spatial_geometry(engine,geometry))throw std::runtime_error("invalid replay camera geometry");
        warp=perception::supercomboWarp({geometry[0],geometry[1],geometry[2],geometry[3],w,h},
            {geometry[6],geometry[7],geometry[8]});
        if(model&&*model)runtime=std::make_unique<perception::SupercomboRuntime>(model,perception::SpatialRuntimeOptions{false,{}});
    }
    YuvFrame frame(const uint8_t* p) const {
        const size_t pixels=size_t(width)*height;
        return {width,height,0,{p,pixels,width,1},{p+pixels,pixels/4,width/2,1},
            {p+pixels+pixels/4,pixels/4,width/2,1},false};
    }
};
EXPORT void* replay_create(const char* model,int width,int height,const double* geometry) {
    try{return new Replay(model,width,height,geometry);}catch(...){return nullptr;}
}
EXPORT void replay_destroy(Replay* r){delete r;}
EXPORT const char* replay_error(Replay* r){return r->error.empty()?ld_error(r->engine):r->error.c_str();}
EXPORT int replay_frame(Replay* r,const uint8_t* yuv,double time,float* out) {
    try {
        if(!r->runtime)throw std::runtime_error("spatial inference is disabled for this source");
        r->runtime->run(r->frame(yuv),r->warp,time);
        std::copy_n(r->runtime->rawOutput(),6472,r->output.data());
        std::copy_n(r->output.data(),6472,out);
        return 1; // Inference cache generation does not publish policy evidence.
    }catch(const std::exception& ex){r->error=ex.what();return 0;}
}
EXPORT int replay_cached_frame(Replay* r,double time,const float* out) {
    if(!r->spatialEnabled)return 0;
    std::copy_n(out,6472,r->output.data());return ld_spatial_publish_source(r->engine,1,time,out,6472);
}
EXPORT int replay_prepare(Replay* r,const uint8_t* yuv,float* tensor) {
    const auto f=r->frame(yuv);
    return ld_preprocess(r->engine,f.y.data,f.y.size,f.y.rowStride,1,f.u.data,f.u.size,f.u.rowStride,1,
        f.v.data,f.v.size,f.v.rowStride,1,r->width,r->height,0,320,tensor,3*320*320*sizeof(float),0,1,1,0,0,1);
}
EXPORT int replay_labels(Replay* r,double time,const float* detections,size_t count,int candidates) {
    r->semanticTime=time;
    return ld_spatial_semantics_source(r->engine,1,time,detections,count,candidates,1,r->hazard);
}
EXPORT size_t replay_boxes(Replay* r,float* out,size_t n){return ld_boxes(r->engine,out,n);}
EXPORT int replay_road(Replay* r,double* out){return ld_road_shape(r->engine,out,13);}
// out: selected, dual, legacy tactical, terrain, camera, suppression,
// audio-gate permission, interrupt permission. No sensor state is fabricated.
EXPORT void replay_tick(Replay* r,double time,double speed,int speedValid,int playing,double* out) {
    ld_telemetry t{time,speed,0,std::numeric_limits<double>::quiet_NaN(),0,0,speedValid,0,0,0,0};
    ld_guidance_request q{};q.source_id=1;q.telemetry=t;q.now=time;q.active=1;q.audio_ready=1;q.manual=1;
    q.allow_emergency_commands=r->emergencies;
    ld_guidance_decide(r->engine,&q,&r->guidance);const auto& d=r->guidance;const int selected=d.prompt;
    const bool emergency=d.eligible&&d.origin==LD_GUIDANCE_REFLEX&&(selected==4||selected==42||selected==43);
    out[0]=selected;out[1]=d.reflex;out[2]=d.tactical;out[3]=d.terrain;out[4]=d.camera;out[5]=d.suppress;
    out[6]=d.eligible;
    out[7]=ld_prompt_may_interrupt(selected,emergency?0:playing);
}
EXPORT void replay_accepted(Replay* r,int id,double time,int isDual) {
    (void)isDual;ld_guidance_accepted(r->engine,r->guidance.sequence,id,time);
}
EXPORT void replay_emergencies(Replay* r,int enabled){r->emergencies=enabled!=0;}
EXPORT const char* replay_guidance_reason(Replay* r){return ld_guidance_reason(r->engine);}
EXPORT void replay_trace(Replay* r,double* out) {
    const auto& d=r->guidance;const double values[]={double(d.sequence),double(d.source_id),double(d.prompt),double(d.origin),
        double(d.tactical),double(d.reflex),double(d.terrain),double(d.camera),double(d.suppress),double(d.eligible),double(d.blocked),
        d.capture_time,d.scene_age,double(d.semantic_available),double(d.spatial_available),
        double(d.actor_id),double(d.actor_category),double(d.evidence_kind),d.actor_x,d.actor_y,d.actor_w,d.actor_h};
    std::copy_n(values,22,out);
}
EXPORT const char* replay_prompt(int id){return ld_prompt_text(id);}
EXPORT int replay_prompt_count(){return ld_prompt_count();}
EXPORT const char* replay_reason(Replay* r,int which) {
    return which==0?ld_dual_policy_reason(r->engine):which==1?ld_camera_reason(r->engine):ld_driving_reason(r->engine);
}
