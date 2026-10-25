#include "learndrive/perception/camera_calibration.hpp"
#include <stdexcept>

namespace ld::perception {
bool CameraIntrinsics::valid() const noexcept {
    return width>0&&height>0&&std::isfinite(fx)&&std::isfinite(fy)&&fx>0&&fy>0&&std::isfinite(cx)&&std::isfinite(cy);
}
Matrix3 multiply(const Matrix3& a,const Matrix3& b) noexcept {
    Matrix3 c{};
    for(int r=0;r<3;++r)for(int s=0;s<3;++s)for(int k=0;k<3;++k)c[r*3+s]+=a[r*3+k]*b[k*3+s];
    return c;
}
Matrix3 inverse(const Matrix3& a) {
    Matrix3 c{a[4]*a[8]-a[5]*a[7],a[2]*a[7]-a[1]*a[8],a[1]*a[5]-a[2]*a[4],
        a[5]*a[6]-a[3]*a[8],a[0]*a[8]-a[2]*a[6],a[2]*a[3]-a[0]*a[5],
        a[3]*a[7]-a[4]*a[6],a[1]*a[6]-a[0]*a[7],a[0]*a[4]-a[1]*a[3]};
    const double d=a[0]*c[0]+a[1]*c[3]+a[2]*c[6];
    if(!std::isfinite(d)||std::abs(d)<1e-12)throw std::invalid_argument("singular camera homography");
    for(auto& v:c)v/=d;
    return c;
}
CameraCalibration calibrateHorizon(Vec3 gravityDevice, double fx, double fy,
                                   double cx, double cy) noexcept {
    CameraCalibration out;
    if (!gravityDevice.finite() || gravityDevice.norm() < 1e-6 ||
        !std::isfinite(fx) || !std::isfinite(fy) || fx <= 0 || fy <= 0 ||
        !std::isfinite(cx) || !std::isfinite(cy)) return out;
    const Vec3 up = gravityDevice.unit();
    // A camera pointing near the ground/sky has no stable forward horizontal
    // projection. Match the mount constraint used by level-plane normalization.
    if (!up.finite() || std::hypot(up.x, up.y) < .35) return out;
    out.rollRadians = std::atan2(up.x, up.y);
    out.pitchRadians = std::atan2(-up.z, std::hypot(up.x, up.y));
    const double cr=std::cos(-out.rollRadians), sr=std::sin(-out.rollRadians);
    const double cp=std::cos(out.pitchRadians), sp=std::sin(out.pitchRadians);
    // View axes are +x right,+y down,+z forward. R levels the measured up ray.
    const double r00=cr, r01=-sr, r02=0;
    const double r10=cp*sr, r11=cp*cr, r12=-sp;
    const double r20=sp*sr, r21=sp*cr, r22=cp;
    auto& h=out.horizonHomography;
    const Matrix3 r{r00,r01,r02,r10,r11,r12,r20,r21,r22};
    const Matrix3 k{fx,0,cx,0,fy,cy,0,0,1},ki{1/fx,0,-cx/fx,0,1/fy,-cy/fy,0,0,1};
    h=multiply(multiply(k,r),ki);
    out.valid = true;
    return out;
}
Matrix3 supercomboWarp(const CameraIntrinsics& c,Vec3 up) {
    if(!c.valid())throw std::invalid_argument("camera intrinsics unavailable");
    const auto alignment=calibrateHorizon(up,c.fx,c.fy,c.cx,c.cy);
    if(!alignment.valid)throw std::invalid_argument("camera gravity unavailable");
    const Matrix3 k{c.fx,0,c.cx,0,c.fy,c.cy,0,0,1};
    const Matrix3 modelInverse{1/910.,0,-256/910.,0,1/910.,-47.6/910.,0,0,1};
    return multiply(multiply(inverse(alignment.horizonHomography),k),modelInverse);
}
Vec3 vehicleToView(Vec3 p) noexcept {return {p.y,p.z,p.x};}
bool projectVehicle(const CameraIntrinsics& c,Vec3 p,double& x,double& y) noexcept {
    const auto v=vehicleToView(p);
    if(!c.valid()||!v.finite()||v.z<=.1)return false;
    x=c.fx*v.x/v.z+c.cx;y=c.fy*v.y/v.z+c.cy;
    return std::isfinite(x)&&std::isfinite(y);
}
} // namespace ld::perception
