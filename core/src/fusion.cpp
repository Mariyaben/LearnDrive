#include "learndrive/fusion.hpp"
namespace ld {
void SensorFusion::reset() {const auto id=calibrationId_;*this=SensorFusion{};calibrationId_=id;}
void SensorFusion::restartCalibration(const char* reason) {
    calibrated_=false;start_=-1;count_=linearCount_=0;
    sum_=squares_=gyroSum_=linearSum_={};calibrationProgress_=0;
    calibrationReason_=reason;
    poseCursor_=poseCount_=0;
}
void SensorFusion::calibrate(const ImuSample& s) {
    // Only accept a quiet, stationary five-second window. Gravity gives roll/pitch,
    // never yaw; the forward-looking rear camera supplies the heading assumption.
    if(std::abs(s.acceleration.norm()-gravity)>.4 || s.gyro.norm()>.08 ||
       (s.hasLinear&&s.linear.norm()>.2)||(lastGps_>=0&&s.time>=lastGps_&&s.time-lastGps_<3&&velocity_>.5)) {
        restartCalibration("Keep the mounted phone still on level ground");return;
    }
    if(start_<0) start_=s.time;
    const Vec3 measured=s.hasLinear?s.acceleration-s.linear:s.acceleration;
    sum_=sum_+measured;
    squares_=squares_+Vec3{measured.x*measured.x,measured.y*measured.y,measured.z*measured.z};
    gyroSum_=gyroSum_+s.gyro;
    if(s.hasLinear){linearSum_=linearSum_+s.linear;++linearCount_;}
    ++count_;
    calibrationProgress_=std::clamp(std::min((s.time-start_)/5,double(count_)/400),0.0,1.0);
    calibrationReason_="Normalizing phone plane; keep still on level ground";
    if(s.time-start_<5 || count_<400) return;
    Vec3 mean=sum_*(1.0/count_);
    double variance=(squares_.x+squares_.y+squares_.z)/count_-mean.dot(mean);
    Vec3 up=mean.unit(), camera{0,0,-1};
    Vec3 forward=camera-up*camera.dot(up);
    if(variance>.04) {restartCalibration("Phone moved during normalization; hold still");return;}
    if(forward.norm()<.35) {restartCalibration("Point the rear camera forward, away from the ground or sky");return;}
    referenceUp_=up;up_=up;forward_=forward.unit();right_=forward_.cross(referenceUp_).unit();
    gyroOffset_=gyroSum_*(1.0/count_);
    linearOffset_=linearCount_>0?linearSum_*(1.0/linearCount_):Vec3{};
    pitch_=0;acceleration_=0;calibrated_=true;calibrationProgress_=1;
    ++calibrationId_;
    calibrationReason_="Phone plane normalized";
}
void SensorFusion::imu(const ImuSample& s) {
    if(!std::isfinite(s.time)||s.time<0||!s.acceleration.finite()||!s.gyro.finite()||
       (s.hasLinear&&!s.linear.finite())||s.time<=lastImu_) return;
    double dt=lastImu_<0?0:s.time-lastImu_;
    if(dt>.2)restartCalibration("Motion data paused; normalize the phone plane again");
    lastImu_=s.time;
    if(!calibrated_) { calibrate(s);rememberCameraPose(s.time);return; }
    if(dt<=0||dt>.2) return; // Never integrate a long suspend/resume gap.
    const Vec3 rotation=s.gyro-gyroOffset_;
    const double angle=rotation.norm()*dt;
    if(angle>1e-9) {
        const Vec3 axis=rotation.unit();
        up_=(up_*std::cos(angle)-axis.cross(up_)*std::sin(angle)+axis*(axis.dot(up_)*(1-std::cos(angle)))).unit();
    }
    Vec3 measured=s.hasLinear?s.acceleration-s.linear:s.acceleration;
    // Without gravity-separated input, acceleration can look like a hill.
    // Keep gyro orientation during such motion rather than tilting to raw force.
    const bool gravityReliable=s.hasLinear||(s.acceleration-up_*gravity).norm()<.6;
    if(gravityReliable&&std::abs(measured.norm()-gravity)<.35) {
        double alpha=1-std::exp(-dt/.8);
        up_=(up_*(1-alpha)+measured.unit()*alpha).unit();
    }
    pitch_=std::asin(std::clamp(up_.dot(forward_),-1.0,1.0))*180/pi;
    Vec3 linear=s.hasLinear?s.linear-linearOffset_:s.acceleration-up_*gravity;
    double longitudinal=linear.dot(forward_);
    acceleration_=longitudinal; // Driver-action suppression sees the current 100 Hz sample.
    if(longitudinal<-.8) brakeUntil_=s.time+.45;
    if(longitudinal<=-3.5) hardUntil_=s.time+.6;
    if(linear.norm()>12) shockUntil_=s.time+.5;
    // State [velocity, accelerometer bias]; GPS corrects both through covariance.
    velocity_=std::max(0.0,velocity_+(acceleration_-bias_)*dt);
    pvv_=std::max(1e-6,pvv_-2*dt*pvb_+dt*dt*pbb_+.7*dt);
    pvb_-=dt*pbb_;
    pbb_+=.002*dt;
    rememberCameraPose(s.time);
}
bool SensorFusion::gps(double t,double v,double accuracy) {
    if(!std::isfinite(t)||t<0||t<=lastGps_||!std::isfinite(v)||v<0||v>90||
       !std::isfinite(accuracy)||accuracy<=0||accuracy>10) return false;
    double r=std::max(.25,accuracy*accuracy), innovation=v-velocity_, s=pvv_+r;
    // Reject an isolated discontinuity after lock. A stale lock may reacquire.
    if(lastGps_>=0&&t-lastGps_<3&&std::abs(innovation)>std::max(8.0,5*std::sqrt(s))) return false;
    double kv=pvv_/s,kb=pvb_/s,oldvv=pvv_,oldvb=pvb_;
    velocity_=std::max(0.0,velocity_+kv*innovation);
    bias_=std::clamp(bias_+kb*innovation,-2.0,2.0);
    pvv_=(1-kv)*oldvv; pvb_=(1-kv)*oldvb; pbb_=std::max(1e-6,pbb_-kb*oldvb);
    lastGps_=t; return true;
}
Telemetry SensorFusion::snapshot(double t) const {
    Telemetry out;
    out.time=t; out.speedKmh=velocity_*3.6; out.pitchDegrees=pitch_;
    out.longitudinalAcceleration=acceleration_; out.calibrationProgress=calibrationProgress_;
    out.calibrated=calibrated_;
    bool fresh=std::isfinite(t)&&lastImu_>=0&&t>=lastImu_&&t-lastImu_<.25;
    out.speedValid=fresh&&lastGps_>=0&&t>=lastGps_&&t-lastGps_<3;
    out.pitchValid=fresh&&calibrated_;
    out.braking=fresh&&t<brakeUntil_; out.hardBraking=fresh&&t<hardUntil_;
    out.shock=fresh&&t<shockUntil_;
    if(!fresh)out.longitudinalAcceleration=std::numeric_limits<double>::quiet_NaN();
    return out;
}
Vec3 SensorFusion::normalizedVector(Vec3 value) const {
    if(!calibrated_||!value.finite())return {};
    return {value.dot(right_),value.dot(referenceUp_),-value.dot(forward_)};
}
Vec3 SensorFusion::cameraUp(double time) const noexcept {
    if(!std::isfinite(time)||lastImu_<0||time-lastImu_>.25)return {};
    // Camera capture normally precedes the newest 100 Hz sample. Retrieve the
    // nearest historical gravity/gyro pose instead of discarding it as future.
    double closest=.03;Vec3 result{};
    for(size_t i=0;i<poseCount_;++i) {
        const double age=std::abs(time-cameraPoses_[i].time);
        if(age<closest){closest=age;result=cameraPoses_[i].up;}
    }
    if(result.norm()>.9)return result;
    if(time>=lastImu_&&time-lastImu_<.25)return calibrated_?up_:(count_>20?sum_.unit():Vec3{});
    return {};
}
void SensorFusion::rememberCameraPose(double time) noexcept {
    const Vec3 value=calibrated_?up_:(count_>20?sum_.unit():Vec3{});
    if(value.norm()<.9)return;
    cameraPoses_[poseCursor_]={time,value};poseCursor_=(poseCursor_+1)%cameraPoses_.size();
    poseCount_=std::min(poseCount_+1,cameraPoses_.size());
}
Inclination SensorFusion::inclination(double time) const {
    const auto telemetry=snapshot(time);
    Inclination result;result.progress=telemetry.calibrationProgress;
    // Beyond this range the mounted-vehicle interpretation is unreliable.
    if(!telemetry.pitchValid||telemetry.shock||std::abs(pitch_)>30||up_.dot(referenceUp_)<.5)return result;
    result.valid=true;result.pitchDegrees=pitch_;
    result.rollDegrees=std::atan2(up_.dot(right_),up_.dot(referenceUp_))*180/pi;
    result.gradePercent=100*std::tan(pitch_*pi/180);
    return result;
}
}
