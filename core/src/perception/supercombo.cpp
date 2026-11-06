// Tensor layout and offsets: commaai/openpilot v0.8.13 (MIT; COMMA-LICENSE.txt).
#include "learndrive/perception/supercombo.hpp"
#include <stdexcept>

namespace ld::perception {
namespace {
double sigmoid(float v) {return 1/(1+std::exp(-std::clamp(double(v),-50.,50.)));}
double deviation(float v) {return std::exp(std::clamp(double(v),-20.,20.));}
double xIndex(int i) {return 192.*i*i/(32.*32.);}
float pixel(const Plane& p,double x,double y,int width,int height,float border) {
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>width-1||y>height-1)return border;
    const int ix=int(x),iy=int(y),jx=std::min(ix+1,width-1),jy=std::min(iy+1,height-1);
    const size_t largest=size_t(jy)*p.rowStride+size_t(jx)*p.pixelStride;
    if(!p.data||p.rowStride<=0||p.pixelStride<=0||largest>=p.size)throw std::invalid_argument("short Supercombo YUV plane");
    auto at=[&](int xx,int yy){return float(p.data[size_t(yy)*p.rowStride+size_t(xx)*p.pixelStride]);};
    const float dx=float(x-ix),dy=float(y-iy);
    return (1-dy)*((1-dx)*at(ix,iy)+dx*at(jx,iy))+dy*((1-dx)*at(ix,jy)+dx*at(jx,jy));
}
}
SpatialOutput decodeSupercombo(const float* a,size_t count) {
    if(!a||count!=supercomboOutputSize)throw std::invalid_argument("Supercombo requires float32 [1,6472]");
    for(size_t i=0;i<count;++i)if(!std::isfinite(a[i]))throw std::invalid_argument("nonfinite Supercombo output");
    SpatialOutput out;
    int best=0;
    for(int i=1;i<5;++i)if(a[i*991+990]>a[best*991+990])best=i;
    for(int j=0;j<33;++j)out.path[j]={a[best*991+j*15],a[best*991+j*15+1],a[best*991+j*15+2]};
    for(int l=0;l<4;++l) {
        auto& line=out.lanes[l];line.probability=sigmoid(a[5483+l*2+1]);line.stdM=deviation(a[5219+l*66]);
        for(int j=0;j<33;++j)line.points[j]={xIndex(j),a[4955+l*66+j*2],a[4955+l*66+j*2+1]};
    }
    for(int l=0;l<2;++l) {
        auto& edge=out.edges[l];edge.stdM=deviation(a[5623+l*66]);edge.probability=1/(1+edge.stdM);
        for(int j=0;j<33;++j)edge.points[j]={xIndex(j),a[5491+l*66+j*2],a[5491+l*66+j*2+1]};
    }
    for(int t=0;t<3;++t) {
        const int bestLead=a[5755+51+48+t]>a[5755+48+t]?1:0;
        const auto* p=a+5755+bestLead*51;
        out.leads[t]={p[0],p[1],p[2],p[3],sigmoid(a[5857+t]),deviation(p[24]),(p[5]-p[1])/2.};
    }
    out.valid=true;
    return out;
}
void packSupercombo(const YuvFrame& f,const Matrix3& h,float* out,size_t capacity) {
    if(!out||capacity<supercomboFrameSize||f.width<2||f.height<2||
       (f.rotation!=0&&f.rotation!=90&&f.rotation!=180&&f.rotation!=270))
        throw std::invalid_argument("invalid Supercombo frame");
    auto sample=[&](int mx,int my,int component) {
        const double d=h[6]*mx+h[7]*my+h[8];
        if(!std::isfinite(d)||std::abs(d)<1e-9)return component?128.f:16.f;
        double x=(h[0]*mx+h[1]*my+h[2])/d,y=(h[3]*mx+h[4]*my+h[5])/d;
        double sx=x,sy=y;
        if(f.rotation==90){sx=y;sy=f.height-1-x;}
        if(f.rotation==180){sx=f.width-1-x;sy=f.height-1-y;}
        if(f.rotation==270){sx=f.width-1-y;sy=x;}
        if(component) {
            const float v=pixel(component==1?f.u:f.v,sx/2,sy/2,(f.width+1)/2,(f.height+1)/2,128);
            return f.fullRange?128+(v-128)*(224.f/255):v;
        }
        const float v=pixel(f.y,sx,sy,f.width,f.height,f.fullRange?0:16);
        return f.fullRange?16+v*(219.f/255):v;
    };
    constexpr size_t plane=128*256;
    for(int y=0;y<128;++y)for(int x=0;x<256;++x) {
        const size_t i=size_t(y)*256+x;
        out[i]=sample(2*x,2*y,0);out[plane+i]=sample(2*x,2*y+1,0);
        out[2*plane+i]=sample(2*x+1,2*y,0);out[3*plane+i]=sample(2*x+1,2*y+1,0);
        out[4*plane+i]=sample(2*x,2*y,1);out[5*plane+i]=sample(2*x,2*y,2);
    }
}
cognition::SceneState spatialScene(const SpatialOutput& out,const Telemetry& telemetry,double time,uint64_t seq) noexcept {
    cognition::SceneState s;s.sequence=seq;s.captureTime=time;s.telemetry=telemetry;
    if(!out.valid)return s;
    // +2s/+4s heads are future lead selections, not independent current objects.
    const auto& lead=out.leads[0];
    if(lead.probability>=.65&&lead.distanceM>0&&lead.distanceM<250&&lead.stdM<std::max(5.,lead.distanceM*.5)) {
        double pathY=0;
        for(size_t i=1;i<out.path.size();++i)if(out.path[i].x>=lead.distanceM) {
            const auto& p=out.path[i-1];const auto& q=out.path[i];
            const double mix=std::clamp((lead.distanceM-p.x)/std::max(.001,q.x-p.x),0.,1.);
            pathY=p.y+(q.y-p.y)*mix;break;
        }
        const double relative=telemetry.speedValid?lead.absoluteSpeedMps-telemetry.speedKmh/3.6:0;
        const double tti=relative<-.1?lead.distanceM/-relative:infinity;
        const double lateral=std::abs(lead.lateralM-pathY);
        const double futureLateral=std::abs(lead.lateralM-pathY+lead.lateralVelocityMps*std::min(3.,tti));
        s.hazards[s.count++]={0x100000001ULL,cognition::HazardType::vehicle,cognition::Lane::ego,
            lead.distanceM,tti,lead.probability,lateral<1.8||futureLateral<1.8,lead.accelerationMps2<-.5,lateral,relative};
    }
    return s;
}
} // namespace ld::perception
