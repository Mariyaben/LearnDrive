#include <jni.h>
#include "learndrive/c_api.h"
#include "learndrive/decision_benchmark.hpp"
#include <array>
#include <vector>
#define JNI(name) Java_ai_learndrive_mobile_NativeCore_##name
static ld_engine* engine(jlong h) {return reinterpret_cast<ld_engine*>(h);}
static std::vector<double> doubles(JNIEnv* env,jdoubleArray a) {if(!a)return {};std::vector<double> v(env->GetArrayLength(a));env->GetDoubleArrayRegion(a,0,jint(v.size()),v.data());return v;}
static std::vector<float> floats(JNIEnv* env,jfloatArray a) {if(!a)return {};std::vector<float> v(env->GetArrayLength(a));env->GetFloatArrayRegion(a,0,jint(v.size()),v.data());return v;}
static jdoubleArray output(JNIEnv* env,const double* a,int n) {auto out=env->NewDoubleArray(n);if(out)env->SetDoubleArrayRegion(out,0,n,a);return out;}
struct AudioBinding {JavaVM* vm{};jobject target{};jmethodID preempt{};};
static void preemptAudio(void* context,int id) {
    auto* b=static_cast<AudioBinding*>(context);if(!b)return;JNIEnv* e=nullptr;
    if(b->vm->GetEnv(reinterpret_cast<void**>(&e),JNI_VERSION_1_6)!=JNI_OK)return;
    e->CallVoidMethod(b->target,b->preempt,id);
    if(e->ExceptionCheck())e->ExceptionClear();
}
extern "C" {
JNIEXPORT jdoubleArray JNICALL JNI(benchmark)(JNIEnv* e,jobject){double values[5];ld::benchmarkDecisions(values);return output(e,values,5);}
JNIEXPORT jlong JNICALL JNI(create)(JNIEnv*,jobject) {return reinterpret_cast<jlong>(ld_create());}
JNIEXPORT jboolean JNICALL JNI(coachingConfigure)(JNIEnv*,jobject,jlong h,jint profile,jboolean voice,jlong revision) {
    return revision>0&&ld_coaching_configure(engine(h),profile,voice,uint64_t(revision));
}
JNIEXPORT jboolean JNICALL JNI(coachingAccept)(JNIEnv*,jobject,jlong h,jlong sequence,jint prompt,jdouble time,jboolean voice) {
    return ld_coaching_accept(engine(h),uint64_t(sequence),prompt,time,voice);
}
JNIEXPORT jboolean JNICALL JNI(coachingPlaybackValid)(JNIEnv*,jobject,jlong h,jlong sequence,jint prompt,jdouble time,jlong revision) {
    return ld_coaching_playback_valid(engine(h),uint64_t(sequence),prompt,time,uint64_t(revision));
}
JNIEXPORT void JNICALL JNI(coachingPlaybackStarted)(JNIEnv*,jobject,jlong h,jlong sequence,jint prompt,jdouble time,jlong revision) {
    ld_coaching_playback_started(engine(h),uint64_t(sequence),prompt,time,uint64_t(revision));
}
JNIEXPORT jdoubleArray JNICALL JNI(coachingDiagnostics)(JNIEnv* e,jobject,jlong h,jdouble time) {
    double values[20]{};ld_coaching_diagnostics(engine(h),time,values,20);return output(e,values,20);
}
JNIEXPORT jstring JNICALL JNI(coachingDiagnosticsJson)(JNIEnv* e,jobject,jlong h,jdouble time) {
    return e->NewStringUTF(ld_coaching_diagnostics_json(engine(h),time));
}
JNIEXPORT void JNICALL JNI(destroy)(JNIEnv* e,jobject,jlong h) {
    auto* b=static_cast<AudioBinding*>(ld_reflex_context(engine(h)));
    ld_set_reflex_preempt(engine(h),nullptr,nullptr);if(b){e->DeleteGlobalRef(b->target);delete b;}ld_destroy(engine(h));
}
JNIEXPORT void JNICALL JNI(bindAudioDispatcher)(JNIEnv* e,jobject,jlong h,jobject target) {
    auto* previous=static_cast<AudioBinding*>(ld_reflex_context(engine(h)));
    if(previous){e->DeleteGlobalRef(previous->target);delete previous;}
    ld_set_reflex_preempt(engine(h),nullptr,nullptr);if(!target)return;
    auto* b=new AudioBinding;e->GetJavaVM(&b->vm);b->target=e->NewGlobalRef(target);
    const auto cls=e->GetObjectClass(target);b->preempt=e->GetMethodID(cls,"onReflex","(I)V");e->DeleteLocalRef(cls);
    if(!b->target||!b->preempt){if(b->target)e->DeleteGlobalRef(b->target);delete b;return;}
    ld_set_reflex_preempt(engine(h),preemptAudio,b);
}
JNIEXPORT void JNICALL JNI(reset)(JNIEnv*,jobject,jlong h) {ld_reset(engine(h));}
JNIEXPORT jlong JNICALL JNI(calibrationId)(JNIEnv*,jobject,jlong h) {return jlong(ld_calibration_id(engine(h)));}
JNIEXPORT void JNICALL JNI(cameraIntrinsics)(JNIEnv*,jobject,jlong h,jdouble fy) {ld_camera_intrinsics(engine(h),fy);}
JNIEXPORT jdoubleArray JNICALL JNI(cameraUp)(JNIEnv* e,jobject,jlong h,jdouble time){double out[3]{};ld_camera_up(engine(h),time,out);return output(e,out,3);}
JNIEXPORT jboolean JNICALL JNI(spatialPublish)(JNIEnv* e,jobject,jlong h,jlong id,jdouble time,jobject out){
    auto* p=static_cast<float*>(e->GetDirectBufferAddress(out));auto n=e->GetDirectBufferCapacity(out);
    return p&&n>0&&ld_spatial_publish_source(engine(h),id,time,p,size_t(n)/4);
}
JNIEXPORT jboolean JNICALL JNI(spatialUnavailable)(JNIEnv*,jobject,jlong h,jlong id,jdouble time){
    return ld_spatial_unavailable_source(engine(h),id,time);
}
JNIEXPORT void JNICALL JNI(spatialGeometry)(JNIEnv* e,jobject,jlong h,jdoubleArray a){
    if(!a||e->GetArrayLength(a)!=9)return;double g[9];e->GetDoubleArrayRegion(a,0,9,g);ld_spatial_geometry(engine(h),g);
}
JNIEXPORT jdoubleArray JNICALL JNI(spatialLabels)(JNIEnv* e,jobject,jlong h,jlong id,jdouble t,jfloatArray d,jint n,jboolean cf){
    auto dv=floats(e,d);double out[3];if(!ld_spatial_semantics_source(engine(h),id,t,dv.data(),dv.size(),n,cf,out))return nullptr;return output(e,out,3);
}
JNIEXPORT jboolean JNICALL JNI(guidanceBegin)(JNIEnv*,jobject,jlong h,jlong id,jint kind,jdouble time){return ld_guidance_begin(engine(h),id,kind,time);}
JNIEXPORT jdoubleArray JNICALL JNI(guidanceDecide)(JNIEnv* e,jobject,jlong h,jlong id,jdoubleArray a,jdouble now,jboolean active,jboolean ready,jboolean manual,jboolean slope,jboolean emergencies,jdouble ht,jdouble tti,jdouble confidence,jboolean path){
    if(!a||e->GetArrayLength(a)!=11)return nullptr;
    std::array<double,11> v{};e->GetDoubleArrayRegion(a,0,11,v.data());
    ld_guidance_request q{};q.source_id=id;q.telemetry={v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};
    q.now=now;q.active=active;q.audio_ready=ready;q.manual=manual;q.slope_enabled=slope;q.allow_emergency_commands=emergencies;
    q.hazard_time=ht;q.tti=tti;q.confidence=confidence;q.in_path=path;
    ld_guidance_result r{};if(!ld_guidance_decide(engine(h),&q,&r))return nullptr;
    double out[]={double(r.sequence),double(r.source_id),double(r.prompt),double(r.origin),double(r.tactical),double(r.reflex),double(r.terrain),double(r.camera),double(r.suppress),double(r.eligible),double(r.blocked),r.capture_time,r.scene_age,double(r.semantic_available),double(r.spatial_available),
        double(r.actor_id),double(r.actor_category),double(r.evidence_kind),r.actor_x,r.actor_y,r.actor_w,r.actor_h};
    return output(e,out,22);
}
JNIEXPORT jboolean JNICALL JNI(guidanceAccepted)(JNIEnv*,jobject,jlong h,jlong sequence,jint prompt,jdouble time){return ld_guidance_accepted(engine(h),sequence,prompt,time);}
JNIEXPORT jboolean JNICALL JNI(guidancePending)(JNIEnv*,jobject,jlong h,jlong sequence,jint prompt,jdouble time){return ld_guidance_pending(engine(h),sequence,prompt,time);}
JNIEXPORT jstring JNICALL JNI(guidanceReason)(JNIEnv* e,jobject,jlong h){return e->NewStringUTF(ld_guidance_reason(engine(h)));}
JNIEXPORT jstring JNICALL JNI(temporalContext)(JNIEnv* e,jobject,jlong h){return e->NewStringUTF(ld_coaching_context_json(engine(h)));}
JNIEXPORT void JNICALL JNI(coachingDeliveryEvent)(JNIEnv*,jobject,jlong h,jlong sequence,jint prompt,jdouble time,jint stage){ld_coaching_delivery_event(engine(h),sequence,prompt,time,stage);}
JNIEXPORT jboolean JNICALL JNI(navigationRoute)(JNIEnv* e,jobject,jlong h,jlong id,jdoubleArray shape,jintArray maneuvers){
    ld_navigation_clear(engine(h));
    if(!shape||!maneuvers)return false;const auto n=e->GetArrayLength(shape),m=e->GetArrayLength(maneuvers);
    if(n<4||n>200000||n%2||m>20000||m%2)return false;
    const auto points=doubles(e,shape);std::vector<int> turns(m);e->GetIntArrayRegion(maneuvers,0,m,turns.data());
    return ld_navigation_route(engine(h),id,points.data(),n/2,turns.data(),m/2);
}
JNIEXPORT void JNICALL JNI(navigationClear)(JNIEnv*,jobject,jlong h){ld_navigation_clear(engine(h));}
JNIEXPORT void JNICALL JNI(navigationTurnUnavailable)(JNIEnv*,jobject,jlong h){ld_navigation_turn_unavailable(engine(h));}
JNIEXPORT jboolean JNICALL JNI(navigationTurn)(JNIEnv*,jobject,jlong h,jint provider,jlong session,jlong epoch,jlong sequence,
    jdouble receivedAt,jint state,jint step,jint maneuver,jdouble meters,jdouble seconds){
    if(session<=0||epoch<=0||sequence<=0)return false;
    const ld_navigation_turn turn{provider,uint64_t(session),uint64_t(epoch),uint64_t(sequence),receivedAt,state,step,maneuver,meters,seconds};
    return ld_navigation_turn_update(engine(h),&turn);
}
JNIEXPORT jboolean JNICALL JNI(navigationFix)(JNIEnv*,jobject,jlong h,jlong source,jdouble t,jdouble lat,jdouble lon,jdouble accuracy,jdouble speed,jdouble speedAccuracy,jboolean speedValid,jdouble bearing,jboolean bearingValid){
    return ld_navigation_fix(engine(h),source,t,lat,lon,accuracy,speed,speedAccuracy,speedValid,bearing,bearingValid);
}
JNIEXPORT jdoubleArray JNICALL JNI(navigationContext)(JNIEnv* e,jobject,jlong h,jdouble t){double v[17]{};ld_navigation_context(engine(h),t,v,17);return output(e,v,17);}
JNIEXPORT jstring JNICALL JNI(navigationStatus)(JNIEnv* e,jobject,jlong h,jdouble t){return e->NewStringUTF(ld_navigation_status(engine(h),t));}
JNIEXPORT jboolean JNICALL JNI(audioAllowed)(JNIEnv*,jobject,jlong h,jint id,jdouble time){return ld_audio_allowed(engine(h),id,time);}
JNIEXPORT void JNICALL JNI(audioAccepted)(JNIEnv*,jobject,jlong h,jint id,jdouble time){ld_audio_accepted(engine(h),id,time);}
JNIEXPORT void JNICALL JNI(imu)(JNIEnv* e,jobject,jlong h,jdouble t,jdoubleArray a,jdoubleArray g,jdoubleArray l,jboolean has) {
    auto av=doubles(e,a),gv=doubles(e,g),lv=doubles(e,l);if(av.size()==3&&gv.size()==3&&lv.size()==3)ld_imu(engine(h),t,av.data(),gv.data(),lv.data(),has);
}
JNIEXPORT jboolean JNICALL JNI(gps)(JNIEnv*,jobject,jlong h,jdouble t,jdouble v,jdouble a) {return ld_gps(engine(h),t,v,a);}
JNIEXPORT jdoubleArray JNICALL JNI(snapshot)(JNIEnv* e,jobject,jlong h,jdouble t) {auto s=ld_snapshot(engine(h),t);double v[]={s.time,s.speed,s.pitch,s.acceleration,s.progress,double(s.calibrated),double(s.speed_valid),double(s.pitch_valid),double(s.braking),double(s.hard_braking),double(s.shock)};return output(e,v,11);}
JNIEXPORT jdoubleArray JNICALL JNI(inclination)(JNIEnv* e,jobject,jlong h,jdouble t) {double v[5]{};ld_inclination(engine(h),t,v,5);return output(e,v,5);}
JNIEXPORT jstring JNICALL JNI(calibrationReason)(JNIEnv* e,jobject,jlong h) {return e->NewStringUTF(ld_calibration_reason(engine(h)));}
JNIEXPORT jdoubleArray JNICALL JNI(adviceTelemetry)(JNIEnv* e,jobject,jdoubleArray a,jboolean required,jdouble manual) {
    auto v=doubles(e,a);if(v.size()!=11)return nullptr;
    ld_telemetry measured{v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};
    auto selected=ld_advice_telemetry(measured,required,manual);v[1]=selected.speed;v[6]=selected.speed_valid;return output(e,v.data(),11);
}
JNIEXPORT jdoubleArray JNICALL JNI(videoTelemetry)(JNIEnv* e,jobject,jdoubleArray a,jdouble speed,jdouble pitch) {
    auto v=doubles(e,a);if(v.size()!=11)return nullptr;
    ld_telemetry measured{v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};
    auto t=ld_video_telemetry(measured,speed,pitch);double values[]={t.time,t.speed,t.pitch,t.acceleration,t.progress,double(t.calibrated),double(t.speed_valid),double(t.pitch_valid),double(t.braking),double(t.hard_braking),double(t.shock)};
    return output(e,values,11);
}
JNIEXPORT jdoubleArray JNICALL JNI(videoSpeedTelemetry)(JNIEnv* e,jobject,jdoubleArray a,jdouble speed) {
    auto v=doubles(e,a);if(v.size()!=11)return nullptr;
    ld_telemetry measured{v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};
    auto t=ld_video_speed_telemetry(measured,speed);v[1]=t.speed;v[6]=t.speed_valid;return output(e,v.data(),11);
}
JNIEXPORT jint JNICALL JNI(decide)(JNIEnv* e,jobject,jlong h,jdoubleArray a,jdouble ht,jdouble tti,jdouble conf,jboolean path,jboolean ready,jboolean gear) {
    auto v=doubles(e,a);if(v.size()!=11)return 0;ld_telemetry t{v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};return ld_decide(engine(h),t,ht,tti,conf,path,ready,gear);
}
JNIEXPORT void JNICALL JNI(accepted)(JNIEnv*,jobject,jlong h,jdouble t) {ld_accepted(engine(h),t);}
JNIEXPORT void JNICALL JNI(cameraStarted)(JNIEnv*,jobject,jlong h,jdouble t) {ld_camera_started(engine(h),t);}
JNIEXPORT jint JNICALL JNI(cameraAdvice)(JNIEnv* e,jobject,jlong h,jdoubleArray a,jboolean ready,jboolean manual) {
    auto v=doubles(e,a);if(v.size()!=11)return 0;
    ld_telemetry t{v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};return ld_camera_decide_telemetry(engine(h),t,ready,manual);
}
JNIEXPORT void JNICALL JNI(cameraAccepted)(JNIEnv*,jobject,jlong h,jint id,jdouble t) {ld_camera_accepted(engine(h),id,t);}
JNIEXPORT jstring JNICALL JNI(cameraReason)(JNIEnv* e,jobject,jlong h) {return e->NewStringUTF(ld_camera_reason(engine(h)));}
JNIEXPORT jint JNICALL JNI(promptPriority)(JNIEnv*,jobject,jint id) {return ld_prompt_priority(id);}
JNIEXPORT jint JNICALL JNI(promptCount)(JNIEnv*,jobject) {return ld_prompt_count();}
JNIEXPORT jint JNICALL JNI(slopeBand)(JNIEnv*,jobject,jdouble degrees) {return ld_slope_band(degrees);}
JNIEXPORT jboolean JNICALL JNI(promptMayInterrupt)(JNIEnv*,jobject,jint next,jint current) {return ld_prompt_may_interrupt(next,current);}
JNIEXPORT jdoubleArray JNICALL JNI(roadShape)(JNIEnv* e,jobject,jlong h) {double values[13]{};ld_road_shape(engine(h),values,13);return output(e,values,13);}
JNIEXPORT jstring JNICALL JNI(promptText)(JNIEnv* e,jobject,jint id) {return e->NewStringUTF(ld_prompt_text(id));}
JNIEXPORT void JNICALL JNI(drivingReset)(JNIEnv*,jobject,jlong h) {ld_driving_reset(engine(h));}
JNIEXPORT void JNICALL JNI(engineSample)(JNIEnv*,jobject,jlong h,jdouble t,jint gear,jint maxGear,jdouble rpm,jdouble lower,jdouble upper,jboolean load) {ld_engine_sample(engine(h),t,gear,maxGear,rpm,lower,upper,load);}
JNIEXPORT jint JNICALL JNI(drivingAdvice)(JNIEnv* e,jobject,jlong h,jdoubleArray a,jboolean ready,jboolean manual,jboolean slope) {
    auto v=doubles(e,a);if(v.size()!=11)return 0;
    ld_telemetry t{v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};
    return ld_driving_decide(engine(h),t,ready,manual,slope);
}
JNIEXPORT void JNICALL JNI(drivingAccepted)(JNIEnv*,jobject,jlong h,jint id,jdouble t) {ld_driving_accepted(engine(h),id,t);}
JNIEXPORT jstring JNICALL JNI(drivingReason)(JNIEnv* e,jobject,jlong h) {return e->NewStringUTF(ld_driving_reason(engine(h)));}
JNIEXPORT jintArray JNICALL JNI(dualPolicyAdvice)(JNIEnv* e,jobject,jlong h,jdoubleArray a,jdouble now) {
    if(!a||e->GetArrayLength(a)!=11)return nullptr;
    std::array<double,11> v{};e->GetDoubleArrayRegion(a,0,11,v.data());
    ld_telemetry t{v[0],v[1],v[2],v[3],v[4],int(v[5]),int(v[6]),int(v[7]),int(v[8]),int(v[9]),int(v[10])};
    int suppress=0;const jint values[]={ld_dual_policy_decide(engine(h),t,now,&suppress),suppress};
    auto out=e->NewIntArray(2);if(out)e->SetIntArrayRegion(out,0,2,values);return out;
}
JNIEXPORT void JNICALL JNI(dualPolicyAccepted)(JNIEnv*,jobject,jlong h,jint prompt,jdouble time) {ld_dual_policy_accepted(engine(h),prompt,time);}
JNIEXPORT void JNICALL JNI(dualPolicyReset)(JNIEnv*,jobject,jlong h) {ld_dual_policy_reset(engine(h));}
JNIEXPORT jstring JNICALL JNI(dualPolicyReason)(JNIEnv* e,jobject,jlong h) {return e->NewStringUTF(ld_dual_policy_reason(engine(h)));}
JNIEXPORT jstring JNICALL JNI(error)(JNIEnv* e,jobject,jlong h) {return e->NewStringUTF(ld_error(engine(h)));}
JNIEXPORT jstring JNICALL JNI(reason)(JNIEnv* e,jobject,jlong h) {return e->NewStringUTF(ld_decision_reason(engine(h)));}
JNIEXPORT jfloatArray JNICALL JNI(boxes)(JNIEnv* e,jobject,jlong h) {
    std::array<float,64*6> values{};const auto count=ld_boxes(engine(h),values.data(),values.size())*6;
    auto out=e->NewFloatArray(jint(count));if(out)e->SetFloatArrayRegion(out,0,jint(count),values.data());return out;
}
JNIEXPORT jboolean JNICALL JNI(prepare)(JNIEnv* e,jobject,jlong h,jobject y,jint yr,jint yp,jobject u,jint ur,jint up,jobject v,jint vr,jint vp,jint w,jint height,jint rot,jint size,jobject tensor,jint dtype,jfloat scale,jint zp,jboolean full,jboolean gray,jint layout) {
    auto ptr=[&](jobject b){return static_cast<uint8_t*>(e->GetDirectBufferAddress(b));};
    auto bytes=[&](jobject b){auto n=e->GetDirectBufferCapacity(b);return n<0?size_t(0):size_t(n);};
    return ld_preprocess(engine(h),ptr(y),bytes(y),yr,yp,ptr(u),bytes(u),ur,up,ptr(v),bytes(v),vr,vp,w,height,rot,size,ptr(tensor),bytes(tensor),dtype,layout,scale,zp,full,gray);
}
JNIEXPORT jdoubleArray JNICALL JNI(vision)(JNIEnv* e,jobject,jlong h,jdouble t,jfloatArray d,jint c,jint n,jboolean cf,jboolean normalized,jfloatArray road,jfloatArray lane,jint w,jint height) {
    auto dv=floats(e,d),rv=floats(e,road),lv=floats(e,lane);if(rv.size()!=size_t(w)*height*2||lv.size()!=rv.size())return nullptr;
    double out[3];if(!ld_vision(engine(h),t,dv.data(),dv.size(),c,n,cf,normalized,rv.data(),lv.data(),w,height,out))return nullptr;return output(e,out,3);
}
}
