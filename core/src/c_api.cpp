#include "learndrive/c_api.h"
#include "learndrive/fusion.hpp"
#include "learndrive/decision.hpp"
#include "learndrive/vision.hpp"
#include <exception>
struct ld_engine { ld::SensorFusion fusion;ld::DecisionEngine decisions;ld::CameraCoach coach;ld::DrivingCoach driving;ld::ApproachTracker tracker;ld::Transform transform;ld::Gray gray;std::vector<ld::Box> boxes;double visionTime=-1;std::string reason="waiting for telemetry",cameraReason="camera not running",drivingReason="watching for road conditions",error; };
extern "C" {
ld_engine* ld_create(void) { try {return new ld_engine;} catch(...) {return nullptr;} }
void ld_destroy(ld_engine* e) {delete e;}
void ld_reset(ld_engine* e) {if(e) {e->fusion.reset();e->driving.reset();e->tracker.reset();e->boxes.clear();e->visionTime=-1;e->error.clear();}} // Recalibration preserves tactical prompt cooldown.
void ld_imu(ld_engine* e,double t,const double a[3],const double g[3],const double l[3],int has) {if(e&&a&&g&&l) e->fusion.imu({t,{a[0],a[1],a[2]},{g[0],g[1],g[2]},{l[0],l[1],l[2]},has!=0});}
int ld_gps(ld_engine* e,double t,double v,double a) {return e&&e->fusion.gps(t,v,a);}
ld_telemetry ld_snapshot(ld_engine* e,double time) {
    if(!e) return {};
    auto t=e->fusion.snapshot(time);
    return {t.time,t.speedKmh,t.pitchDegrees,t.longitudinalAcceleration,t.calibrationProgress,t.calibrated,t.speedValid,t.pitchValid,t.braking,t.hardBraking,t.shock};
}
int ld_inclination(ld_engine* e,double time,double* out,size_t capacity) {
    if(!e||!out||capacity<5)return 0;
    const auto slope=e->fusion.inclination(time);
    out[0]=slope.valid;out[1]=slope.pitchDegrees;out[2]=slope.rollDegrees;out[3]=slope.gradePercent;out[4]=slope.progress;
    return 5;
}
const char* ld_calibration_reason(ld_engine* e) {return e?e->fusion.calibrationReason():"Motion sensors unavailable";}
ld_telemetry ld_advice_telemetry(ld_telemetry measured,int requireGps,double manualSpeed) {
    ld::Telemetry t;t.speedKmh=measured.speed;t.speedValid=measured.speed_valid;
    auto selected=ld::DecisionEngine::adviceTelemetry(t,requireGps!=0,manualSpeed);
    measured.speed=selected.speedKmh;measured.speed_valid=selected.speedValid;return measured;
}
ld_telemetry ld_video_telemetry(ld_telemetry a,double speed,double pitch) {
    ld::Telemetry t;t.time=a.time;t.shock=a.shock;
    auto v=ld::DecisionEngine::videoTelemetry(t,speed,pitch);
    return {v.time,v.speedKmh,v.pitchDegrees,v.longitudinalAcceleration,v.calibrationProgress,v.calibrated,v.speedValid,v.pitchValid,v.braking,v.hardBraking,v.shock};
}
ld_telemetry ld_video_speed_telemetry(ld_telemetry measured,double speed) {
    ld::Telemetry t;t.speedKmh=measured.speed;t.speedValid=measured.speed_valid;
    const auto selected=ld::DecisionEngine::videoSpeedTelemetry(t,speed);
    measured.speed=selected.speedKmh;measured.speed_valid=selected.speedValid;return measured;
}
int ld_decide(ld_engine* e,ld_telemetry a,double ht,double tti,double conf,int path,int ready,int gear) {
    if(!e) return 0;
    try { ld::Telemetry t;
        t.time=a.time;t.speedKmh=a.speed;t.pitchDegrees=a.pitch;t.longitudinalAcceleration=a.acceleration;t.calibrationProgress=a.progress;
        t.calibrated=a.calibrated;t.speedValid=a.speed_valid;t.pitchValid=a.pitch_valid;t.braking=a.braking;t.hardBraking=a.hard_braking;t.shock=a.shock;
        auto decision=e->decisions.evaluate(t,{ht,tti,conf,path!=0},ready!=0,gear!=0);e->reason=decision.reason;return int(decision.prompt);
    } catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
void ld_accepted(ld_engine* e,double time) {if(e&&std::isfinite(time)&&time>=0)e->decisions.accepted(time);}
void ld_camera_started(ld_engine* e,double time) {if(e) {e->coach.start(time);e->driving.reset();e->tracker.reset();e->boxes.clear();e->visionTime=-1;}}
int ld_camera_decide(ld_engine* e,double time,int ready,int braking,int shock) {
    if(!e)return 0;
    try {auto d=e->coach.evaluate(time,e->visionTime,e->boxes,ready!=0,braking!=0,shock!=0);
        e->cameraReason=d.reason;return int(d.prompt);}
    catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
void ld_camera_accepted(ld_engine* e,int prompt,double time) {if(e)e->coach.accepted(static_cast<ld::Prompt>(prompt),time);}
int ld_camera_decide_telemetry(ld_engine* e,ld_telemetry t,int ready,int manual) {
    if(!e)return 0;
    try {auto d=e->coach.evaluate(t.time,e->visionTime,e->boxes,ready!=0,t.braking||t.hard_braking,t.shock,t.speed,t.speed_valid,manual!=0);
        e->cameraReason=d.reason;return int(d.prompt);}
    catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
const char* ld_camera_reason(ld_engine* e) {return e?e->cameraReason.c_str():"camera not running";}
void ld_driving_reset(ld_engine* e) {if(e)e->driving.reset();}
void ld_engine_sample(ld_engine* e,double time,int gear,int maxGear,double rpm,double lower,double upper,int load) {
    if(e)e->driving.engine({time,rpm,lower,upper,gear,maxGear,load!=0});
}
int ld_driving_decide(ld_engine* e,ld_telemetry a,int ready,int manual,int slope) {
    if(!e)return 0;
    try {
        ld::Telemetry t{a.time,a.speed,a.pitch,a.acceleration,a.progress,bool(a.calibrated),bool(a.speed_valid),bool(a.pitch_valid),bool(a.braking),bool(a.hard_braking),bool(a.shock)};
        const double age=t.time-e->visionTime;
        const bool clearPath=e->visionTime>=0&&age>=0&&age<ld::maxVisionAgeSeconds&&e->boxes.empty();
        auto d=e->driving.evaluate(t,ready!=0,manual!=0,slope!=0,clearPath);e->drivingReason=d.reason;return int(d.prompt);
    }catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
void ld_driving_accepted(ld_engine* e,int prompt,double time) {if(e)e->driving.accepted(static_cast<ld::Prompt>(prompt),time);}
const char* ld_driving_reason(ld_engine* e) {return e?e->drivingReason.c_str():"engine unavailable";}
int ld_prompt_priority(int prompt) {return ld::priority(static_cast<ld::Prompt>(prompt));}
int ld_prompt_count(void) {return ld::promptCount;}
int ld_slope_band(double degrees) {return ld::slopeBand(degrees);}
int ld_prompt_may_interrupt(int next,int current) {return ld::mayInterrupt(static_cast<ld::Prompt>(next),static_cast<ld::Prompt>(current));}
const char* ld_prompt_text(int p) {return ld::promptText(static_cast<ld::Prompt>(p));}
int ld_road_shape(ld_engine* e,double* out,size_t capacity) {
    if(!e||!out||capacity<13)return 0;
    std::fill_n(out,13,0);auto& shape=e->driving.shape();out[0]=e->driving.roadTime();out[1]=shape.valid;out[2]=shape.bend;
    if(shape.points.size()==10)for(int i=0;i<10;++i)out[3+i]=shape.points[i];return 13;
}
const char* ld_error(ld_engine* e) {return e?e->error.c_str():"engine unavailable";}
const char* ld_decision_reason(ld_engine* e) {return e?e->reason.c_str():"engine unavailable";}
size_t ld_boxes(ld_engine* e,float* out,size_t capacity) {
    if(!e||!out)return 0;
    const size_t count=std::min(e->boxes.size(),capacity/6);
    for(size_t i=0;i<count;++i) {const auto& b=e->boxes[i];auto* v=out+i*6;v[0]=b.x;v[1]=b.y;v[2]=b.w;v[3]=b.h;v[4]=b.score;v[5]=float(b.category);}
    return count;
}
int ld_preprocess(ld_engine* e,const uint8_t* y,size_t yn,int yr,int yp,const uint8_t* u,size_t un,int ur,int up,const uint8_t* v,size_t vn,int vr,int vp,int w,int h,int rot,int size,void* tensor,size_t bytes,int dtype,int layout,float scale,int zp,int full,int save) {
    if(!e) return 0;
    try {e->transform=ld::preprocess({w,h,rot,{y,yn,yr,yp},{u,un,ur,up},{v,vn,vr,vp},full!=0},size,tensor,bytes,dtype,layout,scale,zp,save?&e->gray:nullptr);e->error.clear();return 1;}
    catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
int ld_vision(ld_engine* e,double t,const float* d,size_t n,int c,int candidates,int cf,int normalized,const float* road,const float* lane,int w,int h,double out[3]) {
    if(!e||!out) return 0;
    try {auto boxes=ld::decodeYolo(d,n,c,candidates,cf!=0,normalized!=0,e->transform);auto lanes=ld::decodeLanes(road,lane,w,h,e->transform);
        auto hazard=e->tracker.update(t,boxes,e->gray,lanes);e->driving.road(t,ld::roadShape(lanes));e->boxes=std::move(boxes);e->visionTime=t;out[0]=hazard.tti;out[1]=hazard.confidence;out[2]=hazard.inPath?1:0;e->error.clear();return 1;}
    catch(const std::exception& ex) {e->driving.road(t,{});e->boxes.clear();e->visionTime=-1;e->error=ex.what();out[0]=ld::infinity;out[1]=0;out[2]=0;return 0;}
}
}
