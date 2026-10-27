#include "learndrive/c_api.h"
#include "learndrive/fusion.hpp"
#include "learndrive/decision.hpp"
#include "learndrive/vision.hpp"
#include "learndrive/navigation.hpp"
#include "learndrive/ContextualPolicyEngine.hpp"
#include "learndrive/perception/metric_estimator.hpp"
#include "learndrive/perception/supercombo.hpp"
#include "learndrive/coaching/CoachingEngine.hpp"
#include <exception>
#include "learndrive/dual_brain_c_api.h"
struct ld_engine {
    ld::SensorFusion fusion;ld::DecisionEngine decisions;ld::CameraCoach coach;ld::DrivingCoach driving;
    ld::ApproachTracker tracker;ld::perception::MetricEstimator metric;ld::cognition::EmbeddedDualBrain dualPolicy;
    ld::Transform transform;ld::Gray gray;std::vector<ld::Box> boxes;ld_dual_scene scene{},semanticScene{};
    ld::perception::SpatialOutput spatial{};double spatialTime=-ld::infinity;
    ld::perception::CameraIntrinsics spatialCamera{};
    ld::perception::Matrix3 spatialWarp{1,0,0,0,1,0,0,0,1};
    double visionTime=-1,focalY=ld::infinity,lastAudio=-ld::infinity;
    std::array<double,ld::promptCount+1> promptTimes{};
    std::array<uint64_t,ld::promptCount+1> attentionActors{},attentionSources{};
    ld_reflex_preempt preempt=nullptr;void* audioContext=nullptr;
    const char* dualReason="waiting for a fresh scene";
    std::string reason="waiting for telemetry",cameraReason="camera not running",drivingReason="watching for road conditions",error;
    uint64_t sourceId{},guidanceSequence{};int sourceKind=LD_SOURCE_CAMERA;
    ld::Hazard semanticHazard{};
    ld::Decision cameraDecision{};
    ld::navigation::RouteCoach navigation;
    ld::navigation::ProviderTurnCoach providerNavigation;
    ld_guidance_result guidance{};
    double guidanceTime=-ld::infinity;
    double sourceStartedAt=0;
    double spatialUnavailableAt=-ld::infinity;
    ld::coaching::CoachingEngine coaching;
    ld::coaching::WorldState coachingWorld{};
    double coachingRouteReceipt=-ld::infinity;
    uint64_t coachingRouteEpoch{};
    double coachingRouteEta=ld::infinity;
    std::string coachingDiagnostics;
    ld::EngineSample coachingEngineSample{};
    ld_engine(){promptTimes.fill(-ld::infinity);}
};
static uint64_t confirmedSpatialLead(const ld_engine& e,double time) noexcept {
    // A model probability is not an independently verified object. In replay,
    // unfamiliar optics/low-resolution imagery produced confident ghost leads.
    // Require a fresh road-user box with independent optical closing evidence
    // at the model's projected lead foot. Detection alone cannot validate TTI.
    constexpr uint64_t missing=std::numeric_limits<uint64_t>::max();
    if(!e.spatialCamera.valid()||!ld::cognition::fresh(time,e.visionTime))return missing;
    const auto& lead=e.spatial.leads[0];
    if(!std::isfinite(lead.distanceM)||lead.distanceM<=0)return missing;
    const double x=256+910*lead.lateralM/lead.distanceM;
    const double y=47.6+910*1.22/lead.distanceM; // pinned model's canonical camera height
    const auto& m=e.spatialWarp;const double w=m[6]*x+m[7]*y+m[8];
    if(!std::isfinite(w)||std::abs(w)<1e-9)return missing;
    const double px=(m[0]*x+m[1]*y+m[2])/w/e.spatialCamera.width;
    const double py=(m[3]*x+m[4]*y+m[5])/w/e.spatialCamera.height;
    for(size_t i=0;i<std::min(e.boxes.size(),size_t(64));++i) {
        const auto& b=e.boxes[i];
        const bool vehicle=b.category==1||b.category==2||b.category==3||b.category==5||b.category==7;
        const bool closing=std::isfinite(b.tti)&&b.tti>0&&std::isfinite(b.confidence)&&b.confidence>=.65;
        if(vehicle&&closing&&b.score>=.4&&b.score<=1&&px>=b.x-.02&&px<=b.x+b.w+.02&&py>=b.y-.03&&py<=b.y+b.h+.03)
            return uint64_t(b.id);
    }
    return missing;
}
static void updateLeadSupport(ld_engine& e,double time) {
    for(auto& b:e.boxes)b.leadTime=-ld::infinity;
    if(!ld::cognition::fresh(time,e.spatialTime)||!ld::cognition::fresh(time,e.visionTime))return;
    const auto measured=e.sourceKind==LD_SOURCE_RECORDING?ld::Telemetry{}:e.fusion.snapshot(time);
    const auto scene=ld::perception::spatialScene(e.spatial,measured,e.spatialTime,0);
    const auto& lead=e.spatial.leads[0];
    if(!scene.count||!scene.hazards[0].inTrajectory||scene.hazards[0].lateralDistanceM>=1.8||
       !std::isfinite(lead.absoluteSpeedMps)||lead.absoluteSpeedMps<0||
       !std::isfinite(lead.lateralVelocityMps)||std::abs(lead.lateralVelocityMps)>.8)return;
    const auto matched=confirmedSpatialLead(e,time);
    for(auto& b:e.boxes)if(uint64_t(b.id)==matched)b.leadTime=e.spatialTime;
}
static bool attentionPrompt(int id) {
    return id==int(ld::Prompt::pedestrianSpace)||id==int(ld::Prompt::riderSpace)||
        id==int(ld::Prompt::vehicleSpace)||id==int(ld::Prompt::roadUserQuiet);
}
static bool audioAllowedFor(const ld_engine* e,int id,double now,const ld::Decision* candidate=nullptr) {
    if(!e||id<=0||id>ld::promptCount||!std::isfinite(now)||now<0||now<e->lastAudio)return false;
    const bool emergency=id==int(ld::Prompt::emergency)||id==int(ld::Prompt::brakeHarder)||id==int(ld::Prompt::emergencyStop);
    if(emergency)return true;
    if(now-e->lastAudio<3.5)return false;
    if(now-e->promptTimes[id]>=10)return true;
    // Phrase dedup normally spans the source. Only an independently verified
    // distinct entry may supersede the same-class attention already spoken.
    return candidate&&attentionPrompt(id)&&candidate->newEncounter&&candidate->actorId>0&&
        e->attentionSources[id]==e->sourceId&&e->attentionActors[id]==uint64_t(candidate->entryReferenceId)&&
        e->coach.newEncounterEligible(candidate->actorId,candidate->entryReferenceId,now);
}
static std::string cameraReason(const ld_engine& e,const ld::Decision& d) {
    if(!d.actorId)return d.reason;
    return d.reason+"; encounter="+std::to_string(d.actorId)+"; reference="+std::to_string(d.entryReferenceId)+
        "; source="+std::to_string(e.sourceId)+"; new_entry="+(d.newEncounter?"1":"0");
}
static ld::Telemetry coachingTelemetry(ld_telemetry a) {
    return {a.time,a.speed,a.pitch,a.acceleration,a.progress,bool(a.calibrated),bool(a.speed_valid),bool(a.pitch_valid),bool(a.braking),bool(a.hard_braking),bool(a.shock)};
}
static ld::coaching::WorldState coachingCurrent(ld_engine& e,double now) {
    using namespace ld::coaching;
    auto w=e.coachingWorld;w.now=now;w.sourceId=e.sourceId;w.actorCount=0;
    w.captureTime=e.visionTime;
    w.frameSequence=e.visionTime>=0?uint64_t(e.visionTime*1000000)+1:0;
    // Original recording motion never comes from the phone. In live operation
    // fresh fusion may add current braking/shock evidence between rule ticks.
    if(e.sourceKind==LD_SOURCE_CAMERA) {
        const auto measured=e.fusion.snapshot(now);
        if(measured.calibrated&&ld::cognition::fresh(now,measured.time,.15))w.telemetry=measured;
    }
    w.gradeBand=w.telemetry.calibrated&&w.telemetry.pitchValid?ld::slopeBand(w.telemetry.pitchDegrees):0;
    w.roadCaptureTime=e.driving.roadTime();w.roadValid=e.driving.shape().valid;w.roadBend=e.driving.shape().bend;
    w.engineCaptureTime=e.coachingEngineSample.time;
    w.engineValid=ld::cognition::fresh(now,w.engineCaptureTime,1)&&e.coachingEngineSample.gear>=1&&e.coachingEngineSample.gear<=e.coachingEngineSample.maxGear&&
        e.coachingEngineSample.rpm>=500&&e.coachingEngineSample.rpm<=12000&&e.coachingEngineSample.underLoad;
    w.engineRpm=e.coachingEngineSample.rpm;w.engineLowerRpm=e.coachingEngineSample.lowerRpm;w.engineUpperRpm=e.coachingEngineSample.upperRpm;
    w.engineGear=e.coachingEngineSample.gear;w.engineMaxGear=e.coachingEngineSample.maxGear;w.engineUnderLoad=e.coachingEngineSample.underLoad;
    if(e.sourceKind==LD_SOURCE_RECORDING||e.sourceKind==LD_SOURCE_SCREEN) {
        // MP4/screen test speed settings and the phone holding the playback
        // are not measurements of the recorded vehicle's response.
        w.telemetry.speedValid=false;w.telemetry.pitchValid=false;w.telemetry.calibrated=false;
        w.telemetry.braking=w.telemetry.hardBraking=w.telemetry.shock=false;
        w.telemetry.longitudinalAcceleration=std::numeric_limits<double>::quiet_NaN();
    }
    for(const auto& b:e.boxes) {
        if(w.actorCount==w.actors.size())break;
        const double center=b.x+b.w*.5,foot=b.y+b.h;
        const bool relevant=foot>.5&&center>.18&&center<.82;
        auto& a=w.actors[w.actorCount++];a={};a.identity={IdentitySpace::Optical,uint64_t(b.id),0};a.captureTime=e.visionTime;
        a.confidence=std::isfinite(b.tti)?std::min(b.confidence,double(b.score)):b.score;a.tti=b.tti;a.category=b.category;
        a.relevant=relevant;a.inPath=false;
        if(ld::cognition::fresh(now,e.semanticScene.capture_time))for(size_t i=0;i<std::min(e.semanticScene.count,size_t(64));++i)
            if(e.semanticScene.hazards[i].id==uint64_t(b.id)){a.inPath=e.semanticScene.hazards[i].in_trajectory!=0;break;}
        a.opticalClosing=std::isfinite(b.tti)&&b.tti>0&&b.confidence>=.65;a.leadSupported=ld::cognition::fresh(now,b.leadTime);
        a.x=b.x;a.y=b.y;a.w=b.w;a.h=b.h;
    }
    auto attention=[&](uint64_t id) {
        if(!id||w.actorCount>=w.actors.size()||!e.coach.actorRelevant(int(id),now))return;
        for(size_t i=0;i<w.actorCount;++i)if(w.actors[i].identity==Identity{IdentitySpace::Attention,id,0})return;
        ld::Box match;if(!e.coach.actorBox(int(id),now,match))return;
        ActorObservation a;a.identity={IdentitySpace::Attention,id,0};a.captureTime=e.visionTime;a.relevant=true;a.category=match.category;
        a.x=match.x;a.y=match.y;a.w=match.w;a.h=match.h;
        for(const auto& b:e.boxes)if(b.category==match.category&&ld::iou(match,b)>.5f) {
            a.confidence=std::isfinite(b.tti)?std::min(double(b.score),b.confidence):b.score;a.tti=b.tti;
            a.opticalClosing=std::isfinite(b.tti)&&b.tti>0&&b.confidence>=.65;a.leadSupported=ld::cognition::fresh(now,b.leadTime);break;
        }
        w.actors[w.actorCount++]=a;
    };
    attention(uint64_t(e.cameraDecision.actorId));
    const auto& accepted=e.coaching.acceptedTicket();
    if(accepted.valid&&accepted.candidate.identity.space==IdentitySpace::Attention)attention(accepted.candidate.identity.id);
    if(e.scene.count&&e.scene.hazards[0].id==0x100000001ULL&&w.actorCount<w.actors.size()) {
        const auto id=confirmedSpatialLead(e,now);
        for(const auto& a:w.actors)if(a.identity.space==IdentitySpace::Optical&&a.identity.id==id&&a.opticalClosing) {
            auto spatial=a;spatial.identity={IdentitySpace::Spatial,0x100000001ULL,0};spatial.captureTime=e.scene.capture_time;
            spatial.inPath=bool(e.scene.hazards[0].in_trajectory);w.actors[w.actorCount++]=spatial;break;
        }
    }
    const bool provider=e.providerNavigation.active();const auto route=provider?e.providerNavigation.context(now):e.navigation.context(now);
    w.route={};w.route.routeId=route.routeId;w.route.eventId=route.featureId;w.route.epoch=e.coachingRouteEpoch;
    w.route.captureTime=provider?e.coachingRouteReceipt:route.fixTime;w.route.gpsTime=route.fixTime;
    w.route.distanceM=route.distanceM;w.route.feature=int(route.feature);w.route.valid=route.valid&&route.featureId!=0;w.route.provider=provider;
    w.route.etaSeconds=provider&&std::isfinite(e.coachingRouteEta)?std::max(0.,e.coachingRouteEta-(now-e.coachingRouteReceipt)):ld::infinity;
    return w;
}
extern "C" {
void ld_set_reflex_preempt(ld_engine* e,ld_reflex_preempt fn,void* context){if(e){e->preempt=fn;e->audioContext=context;}}
void* ld_reflex_context(ld_engine* e){return e?e->audioContext:nullptr;}
ld_engine* ld_create(void) { try {return new ld_engine;} catch(...) {return nullptr;} }
void ld_destroy(ld_engine* e) {delete e;}
void ld_reset(ld_engine* e) {if(e) {e->fusion.reset();e->driving.reset();e->tracker.reset();e->metric.reset();e->boxes.clear();e->visionTime=-1;e->coaching.invalidateEvidence();e->error.clear();}} // Recalibration preserves tactical prompt cooldown.
void ld_camera_intrinsics(ld_engine* e,double fy) {if(e) {e->focalY=std::isfinite(fy)&&fy>0?fy:ld::infinity;e->metric.reset();}}
int ld_camera_up(ld_engine* e,double t,double out[3]) {
    if(!e||!out)return 0;const auto up=e->fusion.cameraUp(t);
    out[0]=up.x;out[1]=up.y;out[2]=up.z;return up.norm()>.9;
}
int ld_audio_allowed(ld_engine* e,int id,double now) {
    const auto* candidate=e&&e->guidance.origin==LD_GUIDANCE_CAMERA&&e->guidance.source_id==e->sourceId&&
        e->guidance.prompt==id&&e->guidance.actor_id==uint64_t(e->cameraDecision.actorId)?&e->cameraDecision:nullptr;
    return audioAllowedFor(e,id,now,candidate);
}
void ld_audio_accepted(ld_engine* e,int id,double now) {
    if(!e||id<=0||id>ld::promptCount||!std::isfinite(now)||now<0)return;
    e->promptTimes[id]=now;e->lastAudio=now;
    e->attentionActors[id]=e->attentionSources[id]=0;
}
int ld_spatial_publish(ld_engine* e,double time,const float* output,size_t count) {
    if(!e||!std::isfinite(time)||time<0||time<e->spatialTime||time<e->spatialUnavailableAt)return 0;
    try {
        e->spatial=ld::perception::decodeSupercombo(output,count);e->spatialTime=time;
        updateLeadSupport(*e,time);
        const auto measured=e->sourceKind==LD_SOURCE_RECORDING?ld::Telemetry{}:e->fusion.snapshot(time);
        const auto s=ld::perception::spatialScene(e->spatial,measured,time,e->scene.sequence+1);
        const auto matched=confirmedSpatialLead(*e,time);
        const bool confirmed=matched!=std::numeric_limits<uint64_t>::max()&&s.count>0;
        e->scene={};e->scene.sequence=s.sequence;e->scene.capture_time=time;e->scene.count=confirmed?s.count:0;
        for(size_t i=0;i<e->scene.count;++i){const auto& h=s.hazards[i];e->scene.hazards[i]={h.id,int(h.type),int(h.lane),
            h.distanceM,h.ttiSeconds,h.confidence,int(h.inTrajectory),int(h.decelerating),h.lateralDistanceM,h.opticalRelativeVelocityMps};}
        if(time>=e->semanticScene.capture_time&&time-e->semanticScene.capture_time<ld::maxVisionAgeSeconds) {
            for(size_t i=0;i<e->semanticScene.count&&e->scene.count<64;++i) {
                auto h=e->semanticScene.hazards[i];
                if((h.type==LD_VEHICLE||h.type==LD_BUS)&&confirmed&&h.id==matched) {
                    if(h.type==LD_BUS&&h.in_trajectory)e->scene.hazards[0].type=LD_BUS;
                    continue;
                }
                if(std::isfinite(h.tti_s))h.tti_s=std::max(0.,h.tti_s-(time-e->semanticScene.capture_time));
                e->scene.hazards[e->scene.count++]=h;
            }
        }
        ld::RoadShape shape;
        const auto& left=e->spatial.lanes[1];const auto& right=e->spatial.lanes[2];
        if(left.probability>.65&&right.probability>.65&&left.stdM<1&&right.stdM<1) {
            shape.valid=true;
            const double change=(left.points[20].y+right.points[20].y-left.points[10].y-right.points[10].y)/2;
            shape.bend=change>.8?1:change<-.8?-1:0;
            for(int i=0;i<5;++i) {
                const auto p=e->spatial.path[8+i*3];double x=0,y=0;
                if(ld::perception::projectVehicle({910,910,256,47.6,512,256},p,x,y)) {
                    const auto& m=e->spatialWarp;const double w=m[6]*x+m[7]*y+m[8];
                    if(std::abs(w)>1e-9&&e->spatialCamera.valid()){
                        shape.points.push_back(float((m[0]*x+m[1]*y+m[2])/w/e->spatialCamera.width));
                        shape.points.push_back(float((m[3]*x+m[4]*y+m[5])/w/e->spatialCamera.height));
                    }
                }
            }
        }
        e->driving.road(time,shape);return 1;
    }catch(const std::exception& ex){e->error=ex.what();e->spatial={};e->spatialTime=-ld::infinity;return 0;}
}
int ld_spatial_geometry(ld_engine* e,const double g[9]) {
    if(!e||!g)return 0;
    try {
        e->spatialCamera={g[0],g[1],g[2],g[3],int(g[4]),int(g[5])};
        e->spatialWarp=ld::perception::supercomboWarp(e->spatialCamera,{g[6],g[7],g[8]});return 1;
    }catch(...){e->spatialCamera={};return 0;}
}
int ld_spatial_semantics(ld_engine* e,double time,const float* detections,size_t count,int candidates,int cf,double out[3]) {
    if(!e||!out)return 0;
    try {
        auto boxes=ld::decodeYolo(detections,count,84,candidates,cf!=0,false,e->transform);
        ld::LaneMap map;map.width=32;map.height=24;map.road.resize(32*24);map.lane.resize(32*24);
        if(e->spatialCamera.valid()&&time>=e->spatialTime&&time-e->spatialTime<ld::maxVisionAgeSeconds) {
            std::array<std::array<std::array<double,2>,33>,2> lines{};
            for(int side=0;side<2;++side)for(int i=0;i<33;++i) {
                const auto& line=e->spatial.lanes[side+1];double x=0,y=0;
                lines[side][i]={-1,-1};
                if(line.probability<.65||line.stdM>1||!ld::perception::projectVehicle({910,910,256,47.6,512,256},line.points[i],x,y))continue;
                const auto& m=e->spatialWarp;const double w=m[6]*x+m[7]*y+m[8];
                if(std::abs(w)>1e-9)lines[side][i]={(m[0]*x+m[1]*y+m[2])/w/e->spatialCamera.width,
                    (m[3]*x+m[4]*y+m[5])/w/e->spatialCamera.height};
            }
            for(int row=0;row<map.height;++row) {
                const double y=(row+.5)/map.height;double edges[2]{-1,-1};
                for(int side=0;side<2;++side)for(int i=1;i<33;++i) {
                    const auto a=lines[side][i-1],b=lines[side][i];
                    if(a[1]<0||b[1]<0||y<std::min(a[1],b[1])||y>std::max(a[1],b[1])||std::abs(a[1]-b[1])<1e-8)continue;
                    edges[side]=a[0]+(b[0]-a[0])*(y-a[1])/(b[1]-a[1]);break;
                }
                if(edges[0]<0||edges[1]<0)continue;
                for(int col=0;col<map.width;++col){const double x=(col+.5)/map.width;
                    if(x>std::min(edges[0],edges[1])&&x<std::max(edges[0],edges[1]))map.road[row*map.width+col]=1;}
            }
        }
        const auto hazard=e->tracker.update(time,boxes,e->gray,map);
        e->semanticScene={};e->semanticScene.capture_time=time;e->semanticScene.count=std::min(boxes.size(),size_t(64));
        for(size_t i=0;i<e->semanticScene.count;++i) {
            const auto& b=boxes[i];const double cx=b.x+b.w/2;
            const bool path=map.inRoad(float(cx),std::min(.99f,b.y+b.h));
            const auto metric=e->metric.update(uint64_t(b.id),b.category,b.w*e->transform.width,e->spatialCamera.fx,time);
            e->semanticScene.hazards[i]={uint64_t(b.id),b.category==0?LD_PEDESTRIAN:b.category==5?LD_BUS:LD_VEHICLE,
                path?LD_EGO:LD_LANE_UNKNOWN,metric.distanceM,b.tti,std::isfinite(b.tti)?std::min(double(b.score),b.confidence):b.score,
                int(path),0,metric.valid?std::abs((cx*e->spatialCamera.width-e->spatialCamera.cx)*metric.distanceM/e->spatialCamera.fx):ld::infinity,
                metric.relativeVelocityMps};
        }
        e->boxes=std::move(boxes);e->visionTime=time;
        updateLeadSupport(*e,time);
        e->semanticHazard=hazard;
        out[0]=hazard.tti;out[1]=hazard.confidence;out[2]=hazard.inPath;return 1;
    }catch(const std::exception& ex){e->error=ex.what();return 0;}
}
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
uint64_t ld_calibration_id(ld_engine* e) {return e?e->fusion.calibrationId():0;}
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
void ld_camera_started(ld_engine* e,double time) {if(e) {e->coach.start(time);e->driving.reset();e->dualPolicy.reset();e->tracker.reset();e->metric.reset();e->boxes.clear();e->scene={};e->semanticScene={};e->spatial={};e->spatialTime=-ld::infinity;e->scene.capture_time=-ld::infinity;e->visionTime=-1;}}
int ld_camera_decide(ld_engine* e,double time,int ready,int braking,int shock) {
    if(!e)return 0;
    try {auto d=e->coach.evaluate(time,e->visionTime,e->boxes,ready!=0,braking!=0,shock!=0,0,false,false,
        e->transform.height>0?double(e->transform.width)/e->transform.height:16.0/9);e->cameraDecision=d;
        e->cameraReason=cameraReason(*e,d);return int(d.prompt);}
    catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
void ld_camera_accepted(ld_engine* e,int prompt,double time) {if(e)e->coach.accepted(static_cast<ld::Prompt>(prompt),time);}
int ld_camera_decide_telemetry(ld_engine* e,ld_telemetry t,int ready,int manual) {
    if(!e)return 0;
    try {auto d=e->coach.evaluate(t.time,e->visionTime,e->boxes,ready!=0,t.braking||t.hard_braking,t.shock,t.speed,t.speed_valid,manual!=0,
        e->transform.height>0?double(e->transform.width)/e->transform.height:16.0/9);e->cameraDecision=d;
        e->cameraReason=cameraReason(*e,d);return int(d.prompt);}
    catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
const char* ld_camera_reason(ld_engine* e) {return e?e->cameraReason.c_str():"camera not running";}
void ld_driving_reset(ld_engine* e) {if(e)e->driving.reset();}
void ld_engine_sample(ld_engine* e,double time,int gear,int maxGear,double rpm,double lower,double upper,int load) {
    if(e){e->coachingEngineSample={time,rpm,lower,upper,gear,maxGear,load!=0};e->driving.engine(e->coachingEngineSample);}
}
int ld_driving_decide(ld_engine* e,ld_telemetry a,int ready,int manual,int slope) {
    if(!e)return 0;
    try {
        ld::Telemetry t{a.time,a.speed,a.pitch,a.acceleration,a.progress,bool(a.calibrated),bool(a.speed_valid),bool(a.pitch_valid),bool(a.braking),bool(a.hard_braking),bool(a.shock)};
        auto d=e->driving.evaluate(t,ready!=0,manual!=0,slope!=0);e->drivingReason=d.reason;return int(d.prompt);
    }catch(const std::exception& ex) {e->error=ex.what();return 0;}
}
void ld_driving_accepted(ld_engine* e,int prompt,double time) {if(e)e->driving.accepted(static_cast<ld::Prompt>(prompt),time);}
const char* ld_driving_reason(ld_engine* e) {return e?e->drivingReason.c_str():"engine unavailable";}
static ld::cognition::Action actionForPrompt(int prompt) {
    using A=ld::cognition::Action;using P=ld::Prompt;
    switch(static_cast<P>(prompt)) {
    case P::holdLaneDistance:return A::holdLaneAndDistance;
    case P::keepSafeGap:return A::keepSafeGap;
    case P::slowSmoothly:return A::slowSmoothly;
    case P::watchPedestrians:return A::watchPedestrians;
    case P::prepareBend:return A::prepareBend;
    case P::observeSign:return A::observeSign;
    default:return A::none;
    }
}
static int promptForAction(ld::cognition::Action action) {
    using A=ld::cognition::Action;using P=ld::Prompt;
    switch(action) {
    case A::holdLaneAndDistance:return int(P::holdLaneDistance);
    case A::keepSafeGap:return int(P::keepSafeGap);
    case A::slowSmoothly:return int(P::slowSmoothly);
    case A::watchPedestrians:return int(P::watchPedestrians);
    case A::prepareBend:return int(P::prepareBend);
    case A::observeSign:return int(P::observeSign);
    default:return 0;
    }
}
static int dualPolicyEvaluate(ld_engine* e,ld_telemetry a,double now,int* suppress,bool preempt) {
    if(!e||!suppress)return 0;
    try {
        ld::cognition::SceneState s;s.sequence=e->scene.sequence;s.captureTime=e->scene.capture_time;
        s.telemetry={a.time,a.speed,a.pitch,a.acceleration,a.progress,bool(a.calibrated),bool(a.speed_valid),
            bool(a.pitch_valid),bool(a.braking),bool(a.hard_braking),bool(a.shock)};s.count=std::min(e->scene.count,size_t(64));
        for(size_t i=0;i<s.count;++i) {const auto& h=e->scene.hazards[i];s.hazards[i]={h.id,
            static_cast<ld::cognition::HazardType>(h.type),static_cast<ld::cognition::Lane>(h.lane),h.distance_m,h.tti_s,
            h.confidence,bool(h.in_trajectory),bool(h.decelerating),h.lateral_distance_m,h.optical_relative_velocity_mps};}
        if(ld::cognition::fresh(now,e->spatialTime)&&s.count>0&&s.hazards[0].id==0x100000001ULL) {
            const auto live=ld::perception::spatialScene(e->spatial,s.telemetry,e->spatialTime,s.sequence);
            if(live.count){const auto type=s.hazards[0].type;s.hazards[0]=live.hazards[0];s.hazards[0].type=type;}
        }
        const auto d=e->dualPolicy.evaluate(s,now);
        *suppress=d.suppressOtherAlerts;e->dualReason=d.reason;
        if(d.brakeNow) {
            const int id=int(d.reflexCommand==ld::cognition::ReflexCommand::BRAKE_HARDER?ld::Prompt::brakeHarder:
                d.reflexCommand==ld::cognition::ReflexCommand::EMERGENCY_STOP?ld::Prompt::emergencyStop:ld::Prompt::emergency);
            if(preempt&&e->preempt)e->preempt(e->audioContext,id);
            return id;
        }
        return promptForAction(d.action);
    } catch(const std::exception& ex) {e->error=ex.what();*suppress=0;return 0;}
}
int ld_dual_policy_decide(ld_engine* e,ld_telemetry a,double now,int* suppress) {
    return dualPolicyEvaluate(e,a,now,suppress,true);
}
void ld_dual_policy_accepted(ld_engine* e,int prompt,double time) {
    if(e)e->dualPolicy.accepted(actionForPrompt(prompt),prompt==int(ld::Prompt::emergency)||prompt==int(ld::Prompt::brakeHarder)||prompt==int(ld::Prompt::emergencyStop),time);
}
void ld_dual_policy_reset(ld_engine* e) {if(e)e->dualPolicy.reset();}
const char* ld_dual_policy_reason(ld_engine* e) {return e?e->dualReason:"engine unavailable";}
int ld_guidance_begin(ld_engine* e,uint64_t id,int kind,double now) {
    if(!e||!id||id<=e->sourceId||kind<LD_SOURCE_CAMERA||kind>LD_SOURCE_SCREEN||!std::isfinite(now)||now<0)return 0;
    ld_camera_started(e,now);e->sourceId=id;e->sourceKind=kind;e->guidance={};e->guidanceTime=now;e->sourceStartedAt=now;
    e->spatialCamera={};e->focalY=ld::infinity;e->gray={};e->transform={};e->semanticHazard={};
    e->spatialUnavailableAt=-ld::infinity;
    e->navigation.invalidateFix();
    e->providerNavigation.invalidateFix();
    e->providerNavigation.unavailable();
    e->coaching.beginSource(id);e->coachingWorld={};e->coachingWorld.sourceId=id;
    if(e->coaching.configured())e->coach.accepted(ld::Prompt::camera,now);
    return 1;
}
int ld_spatial_unavailable_source(ld_engine* e,uint64_t id,double time) {
    if(!e||!id||id!=e->sourceId||!std::isfinite(time)||time<e->sourceStartedAt||
       time<e->spatialTime||time<e->visionTime||time<e->spatialUnavailableAt)return 0;
    e->spatialUnavailableAt=time;
    // Repeated unavailable frames must not cancel fresh semantic-only decisions.
    if(!e->spatialCamera.valid()&&!std::isfinite(e->spatialTime))return 1;
    e->spatialCamera={};e->spatial={};e->spatialTime=-ld::infinity;
    e->metric.reset();e->tracker.reset();e->semanticHazard={};
    e->scene={};e->semanticScene={};e->scene.capture_time=e->semanticScene.capture_time=-ld::infinity;
    for(auto& box:e->boxes){box.tti=ld::infinity;box.expansion=0;box.confidence=0;box.leadTime=-ld::infinity;}
    e->driving.roadUnavailable(time);
    e->guidance.eligible=0;
    return 1;
}
int ld_spatial_publish_source(ld_engine* e,uint64_t id,double time,const float* out,size_t n) {
    return e&&id&&id==e->sourceId&&time>=e->sourceStartedAt&&e->spatialCamera.valid()?ld_spatial_publish(e,time,out,n):0;
}
int ld_spatial_semantics_source(ld_engine* e,uint64_t id,double time,const float* d,size_t n,int candidates,int cf,double out[3]) {
    return e&&id&&id==e->sourceId&&std::isfinite(time)&&time>=e->sourceStartedAt&&time>=e->visionTime?
        ld_spatial_semantics(e,time,d,n,candidates,cf,out):0;
}
int ld_guidance_decide(ld_engine* e,const ld_guidance_request* q,ld_guidance_result* out) {
    if(!e||!q||!out)return 0;
    ld_guidance_result r{};r.actor_category=-1;r.sequence=++e->guidanceSequence;r.source_id=q->source_id;
    r.capture_time=e->visionTime;r.scene_age=q->now-e->visionTime;
    if(e->coaching.configured()) {
        using namespace ld::coaching;
        if(q->source_id!=e->sourceId||!q->source_id||!std::isfinite(q->now)||q->now<e->guidanceTime||
            !std::isfinite(q->telemetry.time)||q->telemetry.time>q->now) {r.blocked=LD_GUIDANCE_INVALID;*out=r;return 1;}
        auto t=q->telemetry;
        if(e->sourceKind==LD_SOURCE_RECORDING) {t.pitch=t.acceleration=std::numeric_limits<double>::quiet_NaN();t.progress=0;t.calibrated=t.pitch_valid=t.braking=t.hard_braking=t.shock=0;}
        const bool simulation=e->sourceKind==LD_SOURCE_SIMULATION;
        const double ht=simulation?q->hazard_time:e->semanticHazard.time,tti=simulation?q->tti:e->semanticHazard.tti;
        const double conf=simulation?q->confidence:e->semanticHazard.confidence;const bool path=simulation?q->in_path!=0:e->semanticHazard.inPath;
        // Candidate producers remain unchanged. Logical/caption presentation is
        // available even when PCM is muted or the output is unavailable.
        r.tactical=ld_decide(e,t,ht,tti,conf,path,1,0);
        if(!simulation) {r.reflex=dualPolicyEvaluate(e,t,q->now,&r.suppress,false);r.camera=ld_camera_decide_telemetry(e,t,1,0);}
        r.terrain=ld_driving_decide(e,t,1,q->manual,q->slope_enabled&&e->sourceKind!=LD_SOURCE_RECORDING);
        e->coachingWorld.telemetry=coachingTelemetry(t);e->coachingWorld.active=q->active!=0;
        e->coachingWorld.allowCritical=q->allow_emergency_commands!=0;e->coachingWorld.audioReady=q->audio_ready!=0;e->coachingWorld.simulation=simulation;
        auto w=coachingCurrent(*e,q->now);w.candidateCount=0;
        auto add=[&](int id,int origin,Identity identity,EventKind kind,double capture,double maxAge=ld::maxVisionAgeSeconds) -> Candidate* {
            if(!id||w.candidateCount>=w.candidates.size())return nullptr;
            auto& c=w.candidates[w.candidateCount++];c={};c.prompt=static_cast<ld::Prompt>(id);c.origin=origin;c.identity=identity;c.kind=kind;
            c.captureTime=capture;c.maxAge=maxAge;c.expiresAt=capture+maxAge;c.supported=true;
            c.critical=id==4||id==19||id==42||id==43;return &c;
        };
        if(r.camera&&e->cameraDecision.actorId) {
            const auto& d=e->cameraDecision;const Identity key{IdentitySpace::Attention,uint64_t(d.actorId),0};
            auto* c=add(r.camera,LD_GUIDANCE_CAMERA,key,d.evidenceKind==3?EventKind::Critical:d.evidenceKind==2?EventKind::Approach:EventKind::Attention,e->visionTime);
            if(c) {c->actorCategory=d.actorCategory;c->evidenceKind=d.evidenceKind;c->x=d.actorX;c->y=d.actorY;c->w=d.actorW;c->h=d.actorH;
                for(size_t i=0;i<w.actorCount;++i)if(w.actors[i].identity==key){c->confidence=w.actors[i].confidence;c->tti=w.actors[i].tti;c->urgent=d.evidenceKind>=2&&w.actors[i].opticalClosing&&c->tti<5;break;}}
        }
        auto approach=[&](int id,int origin,double capture) {
            if(!id)return;Identity key{IdentitySpace::Tactical,1,0};double best=ld::infinity,confidence=0;bool supported=simulation&&path;
            for(size_t i=0;i<w.actorCount;++i) {const auto& a=w.actors[i];
                if(a.identity.space==IdentitySpace::Attention||!a.relevant||!a.opticalClosing||(!a.inPath&&!a.leadSupported)||a.tti>=best)continue;
                best=a.tti;confidence=a.confidence;key=a.identity;supported=true;
            }
            if(simulation){best=tti;confidence=conf;}
            auto* c=add(id,origin,key,(id==4||id==42||id==43)?EventKind::Critical:EventKind::Approach,capture);
            if(c){c->supported=supported;c->tti=best;c->confidence=confidence;c->urgent=supported&&std::isfinite(best)&&best<5;
                for(size_t i=0;i<w.actorCount;++i)if(w.actors[i].identity==key){c->captureTime=std::min(c->captureTime,w.actors[i].captureTime);c->expiresAt=c->captureTime+c->maxAge;break;}}
        };
        approach(r.tactical,LD_GUIDANCE_TACTICAL,ht);
        approach(r.reflex,LD_GUIDANCE_REFLEX,e->scene.capture_time);
        if(r.terrain) {
            const auto p=static_cast<ld::Prompt>(r.terrain);const int band=ld::slopePromptBand(p);
            const bool bend=p==ld::Prompt::bendLeft||p==ld::Prompt::bendRight||p==ld::Prompt::bendLeftQuiet||p==ld::Prompt::bendRightQuiet||p==ld::Prompt::bendLeftGear||p==ld::Prompt::bendRightGear;
            const bool shift=p==ld::Prompt::shiftUp||p==ld::Prompt::shiftDown;
            auto* c=add(r.terrain,LD_GUIDANCE_TERRAIN,{band?IdentitySpace::Slope:bend?IdentitySpace::Bend:shift?IdentitySpace::Engine:IdentitySpace::None,
                uint64_t(band?band+5:bend?e->driving.shape().bend+2:r.terrain),shift?uint64_t(e->coachingEngineSample.gear):0},band?EventKind::Slope:bend?EventKind::Bend:shift?EventKind::Shift:EventKind::Status,
                bend?e->driving.roadTime():t.time);
            if(c)c->mechanicalMeasured=shift&&w.engineValid;
        }
        if(!r.suppress&&e->sourceKind==LD_SOURCE_CAMERA) {
            const int navigation=int(e->providerNavigation.active()?e->providerNavigation.evaluate(q->now,t.braking||t.hard_braking,t.shock):e->navigation.evaluate(q->now,t.braking||t.hard_braking,t.shock));
            add(navigation,LD_GUIDANCE_NAVIGATION,{IdentitySpace::Route,w.route.eventId,w.route.routeId},EventKind::Route,w.route.captureTime,w.route.provider?3:2.5);
        }
        e->coachingWorld=w;
        const auto d=e->coaching.decide(w,r.sequence);const auto& c=d.ticket.candidate;
        if(d.ticket.valid) {r.prompt=int(c.prompt);r.origin=c.origin;r.capture_time=c.captureTime;r.scene_age=q->now-c.captureTime;
            if(c.identity.space==IdentitySpace::Attention){r.actor_id=c.identity.id;r.actor_category=c.actorCategory;r.evidence_kind=c.evidenceKind;r.actor_x=c.x;r.actor_y=c.y;r.actor_w=c.w;r.actor_h=c.h;}}
        r.eligible=d.eligible;r.semantic_available=ld::cognition::fresh(q->now,e->visionTime);r.spatial_available=ld::cognition::fresh(q->now,e->spatialTime);
        r.blocked=d.blocked==Block::None?LD_GUIDANCE_OK:d.blocked==Block::Inactive?LD_GUIDANCE_INACTIVE:d.blocked==Block::CriticalDisabled?LD_GUIDANCE_RESEARCH_ONLY:
            d.blocked==Block::Stale||d.blocked==Block::Revision?LD_GUIDANCE_INVALID:LD_GUIDANCE_COOLDOWN;
        e->guidance=r;e->guidanceTime=q->now;*out=r;return 1;
    }
    if(q->source_id!=e->sourceId||!q->source_id||!std::isfinite(q->now)||q->now<e->guidanceTime||
       !std::isfinite(q->telemetry.time)||q->telemetry.time>q->now||q->now-q->telemetry.time>=ld::maxVisionAgeSeconds)r.blocked=LD_GUIDANCE_INVALID;
    else if(!q->active)r.blocked=LD_GUIDANCE_INACTIVE;
    else {
        const bool simulation=e->sourceKind==LD_SOURCE_SIMULATION;
        auto t=q->telemetry;
        // Until an original-recording telemetry sidecar is supported, phone IMU
        // and GPS must never be presented as motion of the recorded vehicle.
        if(e->sourceKind==LD_SOURCE_RECORDING) {
            t.pitch=t.acceleration=std::numeric_limits<double>::quiet_NaN();
            t.progress=0;t.calibrated=t.pitch_valid=t.braking=t.hard_braking=t.shock=0;
        }
        const double ht=simulation?q->hazard_time:e->semanticHazard.time;
        const double tti=simulation?q->tti:e->semanticHazard.tti;
        const double confidence=simulation?q->confidence:e->semanticHazard.confidence;
        const bool path=simulation?q->in_path!=0:e->semanticHazard.inPath;
        r.tactical=ld_decide(e,t,ht,tti,confidence,path,q->audio_ready,0);
        if(!simulation) {
            r.reflex=dualPolicyEvaluate(e,t,q->now,&r.suppress,false);
            r.camera=ld_camera_decide_telemetry(e,t,q->audio_ready,q->manual);
        }
        r.terrain=ld_driving_decide(e,t,q->audio_ready,q->manual,q->slope_enabled&&e->sourceKind!=LD_SOURCE_RECORDING);
        r.prompt=r.tactical;r.origin=r.prompt?LD_GUIDANCE_TACTICAL:LD_GUIDANCE_NONE;
        auto choose=[&](int id,int origin){if(ld_prompt_priority(id)>ld_prompt_priority(r.prompt)){r.prompt=id;r.origin=origin;}};
        choose(r.reflex,LD_GUIDANCE_REFLEX);
        if(!r.suppress){
            // Live camera attention wins equal-priority ties with routine route preparation.
            choose(r.camera,LD_GUIDANCE_CAMERA);
            if(e->sourceKind==LD_SOURCE_CAMERA)choose(int(e->providerNavigation.active()?
                e->providerNavigation.evaluate(q->now,t.braking||t.hard_braking,t.shock):
                e->navigation.evaluate(q->now,t.braking||t.hard_braking,t.shock)),LD_GUIDANCE_NAVIGATION);
            choose(r.terrain,LD_GUIDANCE_TERRAIN);choose(r.camera,LD_GUIDANCE_CAMERA);}
        if(r.origin==LD_GUIDANCE_CAMERA) {
            const auto& d=e->cameraDecision;r.actor_id=d.actorId;r.actor_category=d.actorCategory;r.evidence_kind=d.evidenceKind;
            r.actor_x=d.actorX;r.actor_y=d.actorY;r.actor_w=d.actorW;r.actor_h=d.actorH;
        }
        const bool emergency=r.prompt==4||r.prompt==19||r.prompt==42||r.prompt==43;
        if(emergency&&!q->allow_emergency_commands)r.blocked=LD_GUIDANCE_RESEARCH_ONLY;
        else if(!q->audio_ready)r.blocked=LD_GUIDANCE_AUDIO;
        else if(r.prompt&&!audioAllowedFor(e,r.prompt,q->now,r.origin==LD_GUIDANCE_CAMERA?&e->cameraDecision:nullptr))r.blocked=LD_GUIDANCE_COOLDOWN;
        r.eligible=r.prompt!=0&&r.blocked==LD_GUIDANCE_OK;
        r.semantic_available=ld::cognition::fresh(q->now,e->visionTime);
        r.spatial_available=ld::cognition::fresh(q->now,e->spatialTime);
        if(r.eligible&&r.origin==LD_GUIDANCE_REFLEX&&emergency&&e->preempt)e->preempt(e->audioContext,r.prompt);
    }
    if(r.blocked!=LD_GUIDANCE_INVALID){e->guidance=r;e->guidanceTime=q->now;}
    *out=r;return 1;
}
int ld_guidance_pending(ld_engine* e,uint64_t sequence,int prompt,double now) {
    if(e&&e->coaching.configured())return e->coaching.pending(sequence,prompt,coachingCurrent(*e,now),now);
    if(!e||!e->guidance.eligible||sequence!=e->guidance.sequence||prompt!=e->guidance.prompt||
       e->guidance.source_id!=e->sourceId||!std::isfinite(now)||now<e->guidanceTime||
       now-e->guidanceTime>=ld::maxVisionAgeSeconds||!ld_audio_allowed(e,prompt,now))return 0;
    if(e->guidance.origin==LD_GUIDANCE_CAMERA&&e->guidance.actor_id&&
       (e->visionTime!=e->guidance.capture_time||now-e->guidance.capture_time>=ld::maxVisionAgeSeconds||
        !e->coach.actorRelevant(int(e->guidance.actor_id),now)))return 0;
    if(e->guidance.origin==LD_GUIDANCE_NAVIGATION&&!(e->providerNavigation.active()?
        e->providerNavigation.context(now):e->navigation.context(now)).valid)return 0;
    if(e->guidance.origin==LD_GUIDANCE_NAVIGATION&&e->providerNavigation.active()&&
       int(e->providerNavigation.evaluate(now,false,false))!=prompt)return 0;
    return 1;
}
int ld_guidance_accepted(ld_engine* e,uint64_t sequence,int prompt,double now) {
    if(e&&e->coaching.configured())return ld_coaching_accept(e,sequence,prompt,now,1);
    if(!ld_guidance_pending(e,sequence,prompt,now))return 0;
    ld_audio_accepted(e,prompt,now);
    if(e->guidance.origin==LD_GUIDANCE_CAMERA&&attentionPrompt(prompt)&&e->guidance.actor_id) {
        e->attentionActors[prompt]=e->guidance.actor_id;e->attentionSources[prompt]=e->sourceId;
    }
    if(prompt>=2&&prompt<=4)ld_accepted(e,now);
    if(e->guidance.origin==LD_GUIDANCE_REFLEX)ld_dual_policy_accepted(e,prompt,now);
    if(e->guidance.origin==LD_GUIDANCE_NAVIGATION) {
        if(e->providerNavigation.active())e->providerNavigation.accepted(now);
        else e->navigation.accepted(now);
    }
    ld_driving_accepted(e,prompt,now);
    if(e->sourceKind!=LD_SOURCE_SIMULATION)ld_camera_accepted(e,prompt,now);
    e->guidance.eligible=0;return 1;
}
int ld_coaching_configure(ld_engine* e,int profile,int voice,uint64_t revision) {
    if(!e||voice<0||voice>1||!e->coaching.configure(profile,voice!=0,revision))return 0;
    e->coaching.beginSource(e->sourceId);e->guidance.eligible=0;
    if(e->sourceId)e->coach.accepted(ld::Prompt::camera,std::max(e->sourceStartedAt,e->guidanceTime));
    return 1;
}
int ld_coaching_accept(ld_engine* e,uint64_t sequence,int prompt,double now,int submitted) {
    if(!e||!e->coaching.configured()||submitted<0||submitted>1||!e->coaching.accept(sequence,prompt,coachingCurrent(*e,now),now,submitted!=0))return 0;
    const auto& ticket=e->coaching.acceptedTicket();const auto& c=ticket.candidate;
    if(c.origin==LD_GUIDANCE_CAMERA&&c.identity.space==ld::coaching::IdentitySpace::Attention)
        e->coach.acceptedActor(static_cast<ld::Prompt>(prompt),now,int(c.identity.id),c.evidenceKind);
    // The event ledger, not unrelated producers' mutable pending fields, owns
    // logical acceptance. In particular a reflex cannot acknowledge a person
    // selected by CameraCoach or retire a different navigation maneuver.
    if(submitted)ld_audio_accepted(e,prompt,now);
    e->guidance.eligible=0;return 1;
}
int ld_coaching_playback_valid(ld_engine* e,uint64_t sequence,int prompt,double now,uint64_t revision) {
    return e&&e->coaching.playbackValid(sequence,prompt,coachingCurrent(*e,now),now,revision);
}
void ld_coaching_playback_started(ld_engine* e,uint64_t sequence,int prompt,double now,uint64_t revision) {
    if(e)e->coaching.playbackStarted(sequence,prompt,coachingCurrent(*e,now),now,revision);
}
int ld_coaching_diagnostics(ld_engine* e,double now,double* out,size_t capacity) {return e?int(e->coaching.diagnostics(now,out,capacity)):0;}
const char* ld_coaching_diagnostics_json(ld_engine* e,double now) {
    if(!e)return "{}";
    try{e->coachingDiagnostics=e->coaching.diagnosticsJson(now);return e->coachingDiagnostics.c_str();}
    catch(...){return "{}";}
}
const char* ld_guidance_reason(ld_engine* e) {
    if(!e)return "engine unavailable";
    if(e->coaching.configured())return e->coaching.reason();
    switch(e->guidance.blocked) {
    case LD_GUIDANCE_INACTIVE:return "source inactive";
    case LD_GUIDANCE_INVALID:return "stale source or invalid time";
    case LD_GUIDANCE_AUDIO:return "audio unavailable";
    case LD_GUIDANCE_COOLDOWN:return "global audio cooldown or duplicate lock";
    case LD_GUIDANCE_RESEARCH_ONLY:return "experimental emergency command disabled";
    default:break;
    }
    switch(e->guidance.origin) {
    case LD_GUIDANCE_REFLEX:return ld_dual_policy_reason(e);
    case LD_GUIDANCE_TACTICAL:return ld_decision_reason(e);
    case LD_GUIDANCE_TERRAIN:return ld_driving_reason(e);
    case LD_GUIDANCE_NAVIGATION:return e->providerNavigation.active()?
        "Provider turn estimate with measured GPS; no exact-distance, gear or safe-speed inference":
        "Offline map preparation cue; no gear or safe-speed inference";
    default:return ld_camera_reason(e);
    }
}
int ld_prompt_priority(int prompt) {return ld::priority(static_cast<ld::Prompt>(prompt));}
int ld_navigation_route(ld_engine* e,uint64_t id,const double* coordinates,size_t points,const int* maneuvers,size_t count) {
    if(!e)return 0;
    e->navigation.clear();e->providerNavigation.clear();e->guidance.eligible=0;
    if(!coordinates||points<2||points>100000||count>10000||(!maneuvers&&count))return 0;
    try {
        std::vector<ld::navigation::Coordinate> shape(points);
        for(size_t i=0;i<points;++i)shape[i]={coordinates[i*2],coordinates[i*2+1]};
        std::vector<ld::navigation::Maneuver> turns(count);
        for(size_t i=0;i<count;++i) {
            if(maneuvers[i*2]<0){e->navigation.clear();return 0;}
            turns[i]={size_t(maneuvers[i*2]),maneuvers[i*2+1]};
        }
        e->guidance.eligible=0;
        return e->navigation.load(id,shape.data(),shape.size(),turns.data(),turns.size());
    }catch(const std::exception& ex){e->error=ex.what();e->navigation.clear();return 0;}
}
int ld_navigation_turn_update(ld_engine* e,const ld_navigation_turn* turn) {
    if(!e||!turn||e->sourceKind!=LD_SOURCE_CAMERA||turn->received_at<e->sourceStartedAt)return 0;
    const ld::navigation::TurnInput input{turn->provider,turn->session,turn->route_epoch,turn->sequence,
        turn->received_at,static_cast<ld::navigation::TurnState>(turn->state),turn->step_number,
        static_cast<ld::navigation::TurnKind>(turn->maneuver),turn->estimated_remaining_m,turn->estimated_remaining_seconds};
    if(!e->providerNavigation.update(input))return 0;
    e->coachingRouteReceipt=turn->received_at;e->coachingRouteEpoch=turn->route_epoch;e->coachingRouteEta=turn->estimated_remaining_seconds;
    e->navigation.clear();e->guidance.eligible=0;return 1;
}
void ld_navigation_turn_unavailable(ld_engine* e) {
    if(!e)return;e->providerNavigation.unavailable();
    if(e->guidance.origin==LD_GUIDANCE_NAVIGATION)e->guidance.eligible=0;
}
void ld_navigation_clear(ld_engine* e){if(e){e->navigation.clear();e->providerNavigation.clear();e->guidance.eligible=0;}}
int ld_navigation_fix(ld_engine* e,uint64_t source,double time,double lat,double lon,double accuracy,
    double speed,double speedAccuracy,int speedValid,double bearing,int bearingValid) {
    if(!e||!source||source!=e->sourceId||e->sourceKind!=LD_SOURCE_CAMERA||time<e->sourceStartedAt)return 0;
    if(e->guidance.origin==LD_GUIDANCE_NAVIGATION)e->guidance.eligible=0;
    const ld::navigation::Fix fix{time,lat,lon,accuracy,speed,bearing,speedAccuracy,bool(speedValid),bool(bearingValid)};
    const auto providerAccepted=e->providerNavigation.fix(fix);
    return e->providerNavigation.active()?providerAccepted:e->navigation.fix(fix);
}
int ld_navigation_context(ld_engine* e,double now,double* out,size_t capacity) {
    if(!e||!out||capacity<17)return 0;
    const auto c=e->providerNavigation.active()?e->providerNavigation.context(now):e->navigation.context(now);
    const double v[]={double(c.routeId),double(c.featureId),double(c.feature),double(c.match),c.fixTime,c.progressM,
        c.distanceM,c.headingChangeDegrees,c.estimatedRadiusM,c.approachSpeedLimitKmh,double(c.valid),double(c.maneuverType),double(c.maneuverShapeIndex),c.maneuverDistanceM,
        c.gpsSpeedKmh,c.speedAccuracyMps,c.horizontalAccuracyM};
    std::copy_n(v,17,out);return 17;
}
const char* ld_navigation_status(ld_engine* e,double now) {
    return e?(e->providerNavigation.active()?e->providerNavigation.status(now):
        ld::navigation::RouteCoach::status(e->navigation.context(now).match)):"Navigation unavailable";
}
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
        auto hazard=e->tracker.update(t,boxes,e->gray,lanes);e->semanticHazard=hazard;const auto shape=ld::roadShape(lanes);e->driving.road(t,shape);
        e->scene.capture_time=t;++e->scene.sequence;e->scene.count=std::min(boxes.size(),size_t(64));
        for(size_t i=0;i<e->scene.count;++i) {
            const auto& b=boxes[i];const float cx=b.x+b.w/2,foot=b.y+b.h;
            const bool path=cx>.3f&&cx<.7f&&lanes.inRoad(cx,std::min(.99f,foot));
            const int type=b.category==0?LD_PEDESTRIAN:b.category==5?LD_BUS:
                (b.category==1||b.category==2||b.category==3||b.category==7)?LD_VEHICLE:b.category==11?LD_SIGN:LD_UNKNOWN;
            const auto metric=e->metric.update(static_cast<uint64_t>(b.id),b.category,b.w*e->transform.width,e->focalY,t);
            const double tti=std::isfinite(metric.ttiSeconds)?metric.ttiSeconds:b.tti;
            e->scene.hazards[i]={static_cast<uint64_t>(b.id),type,path?LD_EGO:LD_LANE_UNKNOWN,metric.distanceM,tti,
                std::isfinite(tti)?std::min(double(b.score),b.confidence):double(b.score),path?1:0,0,
                std::max(.25,std::abs(double(cx-.5f))*4.0),metric.relativeVelocityMps};
        }
        if(shape.valid&&shape.bend!=0&&e->scene.count<64) {
            auto& h=e->scene.hazards[e->scene.count++];h={static_cast<uint64_t>(0xffffffffffffffffULL),LD_BEND,LD_EGO,
                ld::infinity,ld::infinity,.9,1,0,1.0,0};
        }
        e->semanticScene=e->scene;e->boxes=std::move(boxes);e->visionTime=t;out[0]=hazard.tti;out[1]=hazard.confidence;out[2]=hazard.inPath?1:0;e->error.clear();return 1;}
    catch(const std::exception& ex) {e->driving.road(t,{});e->boxes.clear();e->scene={};e->scene.capture_time=-ld::infinity;e->visionTime=-1;e->error=ex.what();out[0]=ld::infinity;out[1]=0;out[2]=0;return 0;}
}
int ld_dual_read_vision(ld_engine* e,ld_dual_scene* out) {
    if(!e||!out||!std::isfinite(e->visionTime)||e->visionTime<0)return 0;
    *out=e->scene;return 1;
}
}
