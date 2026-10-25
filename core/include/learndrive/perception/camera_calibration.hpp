#pragma once
#include "learndrive/types.hpp"
#include <array>

namespace ld::perception {
using Matrix3 = std::array<double,9>;
struct CameraIntrinsics {
    double fx{}, fy{}, cx{}, cy{};
    int width{}, height{};
    bool valid() const noexcept;
};
Matrix3 multiply(const Matrix3& a,const Matrix3& b) noexcept;
Matrix3 inverse(const Matrix3& a);
Matrix3 supercomboWarp(const CameraIntrinsics&, Vec3 up);
Vec3 vehicleToView(Vec3 forwardRightDown) noexcept;
bool projectVehicle(const CameraIntrinsics&,Vec3 forwardRightDown,double& x,double& y) noexcept;
struct CameraCalibration {
    double pitchRadians{};
    double rollRadians{};
    std::array<double, 9> horizonHomography{1,0,0, 0,1,0, 0,0,1};
    bool valid{};
};

// Builds the rotation homography K*R*K^-1. Intrinsics are in pixels and the
// output maps camera pixels into the gravity-aligned camera coordinate system.
CameraCalibration calibrateHorizon(Vec3 gravityDevice, double fx, double fy,
                                   double cx, double cy) noexcept;
} // namespace ld::perception
