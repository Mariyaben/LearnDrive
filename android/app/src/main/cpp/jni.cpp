#include <jni.h>
#include "learndrive/c_api.h"
#include <array>
#include <vector>
#define JNI(name) Java_ai_learndrive_mobile_NativeCore_##name
static ld_engine* engine(jlong h) {return reinterpret_cast<ld_engine*>(h);}
static std::vector<double> doubles(JNIEnv* env,jdoubleArray a) {if(!a)return {};std::vector<double> v(env->GetArrayLength(a));env->GetDoubleArrayRegion(a,0,jint(v.size()),v.data());return v;}
static std::vector<float> floats(JNIEnv* env,jfloatArray a) {if(!a)return {};std::vector<float> v(env->GetArrayLength(a));env->GetFloatArrayRegion(a,0,jint(v.size()),v.data());return v;}
static jdoubleArray output(JNIEnv* env,const double* a,int n) {auto out=env->NewDoubleArray(n);if(out)env->SetDoubleArrayRegion(out,0,n,a);return out;}
extern "C" {
JNIEXPORT jlong JNICALL JNI(create)(JNIEnv*,jobject) {return reinterpret_cast<jlong>(ld_create());}
JNIEXPORT void JNICALL JNI(destroy)(JNIEnv*,jobject,jlong h) {ld_destroy(engine(h));}
JNIEXPORT void JNICALL JNI(reset)(JNIEnv*,jobject,jlong h) {ld_reset(engine(h));}
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
