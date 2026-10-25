#pragma once
#include "types.hpp"
#include <array>
#include <cstdint>
namespace ld {
struct Inclination {
    bool valid=false;
    double pitchDegrees=0,rollDegrees=0,gradePercent=0,progress=0;
};
class SensorFusion {
public:
    void reset();
    void imu(const ImuSample& sample);
    bool gps(double time,double speedMps,double accuracyMps);
    Telemetry snapshot(double time) const;
    Inclination inclination(double time) const;
    const char* calibrationReason() const {return calibrationReason_;}
    // Right/up/backward coordinates in the level plane captured at calibration.
    Vec3 normalizedVector(Vec3 deviceVector) const;
    Vec3 cameraUp(double time) const noexcept;
    uint64_t calibrationId() const noexcept {return calibrationId_;}
private:
    void calibrate(const ImuSample& s);
    void restartCalibration(const char* reason);
    void rememberCameraPose(double time) noexcept;
    struct CameraPose {double time=-infinity;Vec3 up;};
    std::array<CameraPose,64> cameraPoses_{};
    size_t poseCursor_{},poseCount_{};
    bool calibrated_{};
    uint64_t calibrationId_{};
    double start_=-1,lastImu_=-1,lastGps_=-1,calibrationProgress_{};
    int count_{},linearCount_{};
    Vec3 sum_, squares_,gyroSum_,linearSum_,gyroOffset_,linearOffset_;
    Vec3 up_,referenceUp_,forward_,right_;
    const char* calibrationReason_="Hold still on level ground; rear camera forward";
    double velocity_{},bias_{},pvv_=25,pvb_{},pbb_=1;
    double acceleration_{},pitch_{},brakeUntil_=-1,hardUntil_=-1,shockUntil_=-1;
};
}
