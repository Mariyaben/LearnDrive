#pragma once
#include "camera_calibration.hpp"
#include "learndrive/SceneState.hpp"
#include "learndrive/vision.hpp"
#include <memory>
#include <string>

namespace ld::perception {
constexpr size_t supercomboFrameSize=6*128*256;
constexpr size_t supercomboInputSize=2*supercomboFrameSize;
constexpr size_t supercomboOutputSize=6472;
constexpr size_t supercomboStateOffset=5960;
struct SpatialLine {std::array<Vec3,33> points{}; double probability{},stdM{};};
struct SpatialLead {double distanceM{},lateralM{},absoluteSpeedMps{},accelerationMps2{},probability{},stdM{},lateralVelocityMps{};};
struct SpatialOutput {
    std::array<Vec3,33> path{};
    std::array<SpatialLine,4> lanes{};
    std::array<SpatialLine,2> edges{};
    std::array<SpatialLead,3> leads{};
    bool valid{};
};
SpatialOutput decodeSupercombo(const float*,size_t);
void packSupercombo(const YuvFrame&,const Matrix3& sourceFromModel,float* currentFrame,size_t capacity);
cognition::SceneState spatialScene(const SpatialOutput&,const Telemetry&,double captureTime,uint64_t sequence) noexcept;
struct ProviderReport {
    std::string selected="CPU",fallbackReason;
    double initializationMs{},baselineMs{},qnnMs{};
    bool qnnProbed{},bigCoreAffinity{};
};
struct SpatialRuntimeOptions {bool profileQnn=true;std::string qnnBackendPath;};
// The inference worker owns the session, temporal input and recurrent state.
class SupercomboRuntime {
public:
    SupercomboRuntime(const std::string& modelPath,SpatialRuntimeOptions options={});
    ~SupercomboRuntime();
    void reset() noexcept;
    SpatialOutput run(const YuvFrame&,const Matrix3&,double captureTime);
    const ProviderReport& report() const noexcept;
    const float* packedInput() const noexcept;
    const float* rawOutput() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ld::perception
