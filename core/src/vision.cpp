#include "learndrive/vision.hpp"
#include <stdexcept>
#include <array>
namespace ld {
namespace {
uint8_t read(const Plane& p,int x,int y) {
    size_t offset=size_t(y)*p.rowStride+size_t(x)*p.pixelStride;
    if(!p.data||x<0||y<0||p.rowStride<=0||p.pixelStride<=0||offset>=p.size) throw std::invalid_argument("invalid YUV plane bounds");
    return p.data[offset];
}
std::array<int,2> source(const YuvFrame& f,int x,int y) {
    switch(f.rotation) {
    case 90:return {y,f.height-1-x};
    case 180:return {f.width-1-x,f.height-1-y};
    case 270:return {f.width-1-y,x};
    default:return {x,y};
    }
}
float sigmoidDiff(float foreground,float background) {
    return 1/(1+std::exp(std::clamp(background-foreground,-30.f,30.f)));
}
}
Transform preprocess(const YuvFrame& f,int size,void* tensor,size_t capacity,int dtype,int layout,float qs,int zp,Gray* gray) {
    if(f.width<=0||f.height<=0||size<=0||size>1024||!tensor||dtype<0||dtype>2||layout<0||layout>1||
       (f.rotation!=0&&f.rotation!=90&&f.rotation!=180&&f.rotation!=270)||
       (dtype!=0&&(!std::isfinite(qs)||qs<=0))||capacity<size_t(size)*size*3*(dtype==0?4:1)) throw std::invalid_argument("invalid preprocessing arguments");
    bool swap=f.rotation==90||f.rotation==270;
    Transform tr{swap?f.height:f.width,swap?f.width:f.height,size,0,0,0};
    tr.scale=std::min(float(size)/tr.width,float(size)/tr.height);
    tr.padX=(size-tr.width*tr.scale)/2; tr.padY=(size-tr.height*tr.scale)/2;
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        float fx=(x+.5f-tr.padX)/tr.scale,fy=(y+.5f-tr.padY)/tr.scale;
        float rgb[3]={114,114,114};
        if(fx>=0&&fy>=0&&fx<tr.width&&fy<tr.height) {
            auto xy=source(f,std::min(int(fx),tr.width-1),std::min(int(fy),tr.height-1));
            // BT.601 baseline. Native adapters identify full vs limited range;
            // model/device validation must also verify the color matrix.
            float Y=read(f.y,xy[0],xy[1]),U=read(f.u,xy[0]/2,xy[1]/2)-128.f,V=read(f.v,xy[0]/2,xy[1]/2)-128.f;
            if(f.fullRange) {
                rgb[0]=Y+1.402f*V; rgb[1]=Y-.344136f*U-.714136f*V; rgb[2]=Y+1.772f*U;
            }else {
                Y=1.164383f*(Y-16);
                rgb[0]=Y+1.596027f*V;rgb[1]=Y-.391762f*U-.812968f*V;rgb[2]=Y+2.017232f*U;
            }
        }
        for(int c=0;c<3;++c) {
            float value=std::clamp(rgb[c],0.f,255.f)/255;
            size_t i=layout==0?(size_t(y)*size+x)*3+c:size_t(c)*size*size+y*size+x;
            if(dtype==0) static_cast<float*>(tensor)[i]=value;
            else { int q=int(std::round(value/qs))+zp;
                if(dtype==1) static_cast<uint8_t*>(tensor)[i]=uint8_t(std::clamp(q,0,255));
                else static_cast<int8_t*>(tensor)[i]=int8_t(std::clamp(q,-128,127)); }
        }
    }
    if(gray) {
        gray->pixels.resize(gray->width*gray->height);
        for(int y=0;y<gray->height;++y) for(int x=0;x<gray->width;++x) {
            auto xy=source(f,std::min(int((x+.5)*tr.width/gray->width),tr.width-1),std::min(int((y+.5)*tr.height/gray->height),tr.height-1));
            gray->pixels[y*gray->width+x]=read(f.y,xy[0],xy[1]);
        }
    }
    return tr;
}
float iou(const Box& a,const Box& b) {
    float w=std::max(0.f,std::min(a.x+a.w,b.x+b.w)-std::max(a.x,b.x));
    float h=std::max(0.f,std::min(a.y+a.h,b.y+b.h)-std::max(a.y,b.y));
    float intersection=w*h; return intersection/std::max(1e-8f,a.w*a.h+b.w*b.h-intersection);
}
std::vector<Box> decodeYolo(const float* v,size_t count,int channels,int n,bool cf,bool normalized,const Transform& tr) {
    if(!v||channels!=84||n<=0||n>30000||count!=size_t(channels)*n||tr.size<=0) throw std::invalid_argument("expected YOLOv8 raw COCO [1,84,N] or [1,N,84]");
    auto at=[&](int c,int i){return v[cf?size_t(c)*n+i:size_t(i)*channels+c];};
    std::vector<Box> boxes;
    for(int i=0;i<n;++i) {
        float score=0; int cls=-1;
        // Select across ALL classes first, to avoid reclassifying a non-road object.
        for(int c=0;c<80;++c) if(at(c+4,i)>score) { score=at(c+4,i); cls=c; }
        if(score<.4f||!std::isfinite(score)||(cls!=0&&cls!=1&&cls!=2&&cls!=3&&cls!=5&&cls!=7)) continue;
        float factor=normalized?float(tr.size):1.f;
        float cx=at(0,i)*factor,cy=at(1,i)*factor,w=at(2,i)*factor,h=at(3,i)*factor;
        if(!std::isfinite(cx)||!std::isfinite(cy)||!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0) continue;
        float x1=std::clamp((cx-w/2-tr.padX)/tr.scale/tr.width,0.f,1.f);
        float y1=std::clamp((cy-h/2-tr.padY)/tr.scale/tr.height,0.f,1.f);
        float x2=std::clamp((cx+w/2-tr.padX)/tr.scale/tr.width,0.f,1.f);
        float y2=std::clamp((cy+h/2-tr.padY)/tr.scale/tr.height,0.f,1.f);
        if(x2<=x1||y2<=y1) continue;
        boxes.push_back({x1,y1,x2-x1,y2-y1,score,cls});
    }
    std::sort(boxes.begin(),boxes.end(),[](const Box& a,const Box& b){return a.score>b.score;});
    if(boxes.size()>200) boxes.resize(200);
    std::vector<Box> kept;
    for(const auto& b:boxes) {
        bool suppressed=false;
        for(const auto& k:kept) if(k.category==b.category&&iou(k,b)>.45f) { suppressed=true; break; }
        if(!suppressed) kept.push_back(b);
        if(kept.size()>=64) break;
    }
    return kept;
}
LaneMap decodeLanes(const float* road,const float* lane,int w,int h,const Transform& tr) {
    if(!road||!lane||w<=0||h<=0||w>1024||h>1024||tr.size<=0) throw std::invalid_argument("invalid two-class lane logits");
    LaneMap map; map.width=80; map.height=80; map.road.resize(6400); map.lane.resize(6400);
    size_t area=size_t(w)*h;
    for(int y=0;y<80;++y) for(int x=0;x<80;++x) {
        float sx=((x+.5f)/80*tr.width*tr.scale+tr.padX)/tr.size;
        float sy=((y+.5f)/80*tr.height*tr.scale+tr.padY)/tr.size;
        int ix=std::clamp(int(sx*w),0,w-1),iy=std::clamp(int(sy*h),0,h-1),i=iy*w+ix;
        map.road[y*80+x]=sigmoidDiff(road[area+i],road[i]);
        map.lane[y*80+x]=sigmoidDiff(lane[area+i],lane[i]);
    }
    return map;
}
void ApproachTracker::reset() { tracks_.clear(); previous_.pixels.clear(); last_=-1; nextId_=1; }
Hazard ApproachTracker::update(double time,std::vector<Box>& boxes,const Gray& gray,const LaneMap& lanes) {
    Hazard hazard; hazard.time=time;
    if(!std::isfinite(time)||time<0||gray.pixels.size()!=size_t(gray.width)*gray.height) { reset(); return hazard; }
    double dt=time-last_;
    bool temporal=last_>=0&&dt>=.008&&dt<maxVisionAgeSeconds&&previous_.width==gray.width&&previous_.height==gray.height&&!previous_.pixels.empty();
    if(!temporal) tracks_.clear();
    std::vector<bool> used(tracks_.size()); std::vector<Track> next;
    for(auto& b:boxes) {
        int best=-1; float overlap=.25f;
        for(size_t i=0;i<tracks_.size();++i) if(!used[i]&&tracks_[i].box.category==b.category) {
            float score=iou(b,tracks_[i].box); if(score>overlap) { overlap=score; best=int(i); }
        }
        Track track; track.box=b; track.box.id=nextId_++; track.hits=1;
        if(best>=0) {
            used[best]=true; const auto& old=tracks_[best];
            track.box.id=old.box.id; track.hits=old.hits+1;
            double ratio=std::sqrt((b.w*b.h)/(old.box.w*old.box.h));
            double expansion=std::log(std::max(.01,ratio))/dt;
            track.rate=old.rate*.65+expansion*.35;
            // Sparse integer patch flow on a small luminance image. Four textured
            // patches must agree with box expansion; pure translation is rejected.
            double divergent=0; int valid=0;
            for(int sy:{-1,1}) for(int sx:{-1,1}) {
                int px=int((old.box.x+old.box.w*(.5f+sx*.25f))*gray.width);
                int py=int((old.box.y+old.box.h*(.5f+sy*.25f))*gray.height);
                if(px<7||py<7||px>=gray.width-7||py>=gray.height-7) continue;
                int low=255,high=0;
                for(int yy=-2;yy<=2;++yy) for(int xx=-2;xx<=2;++xx) {
                    int value=previous_.pixels[(py+yy)*gray.width+px+xx]; low=std::min(low,value);high=std::max(high,value);
                }
                if(high-low<25) continue;
                int cost=100000,dxBest=0,dyBest=0;
                for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx) {
                    int candidate=0;
                    for(int yy=-2;yy<=2;++yy) for(int xx=-2;xx<=2;++xx)
                        candidate+=std::abs(int(previous_.pixels[(py+yy)*gray.width+px+xx])-int(gray.pixels[(py+dy+yy)*gray.width+px+dx+xx]));
                    if(candidate<cost) {cost=candidate; dxBest=dx;dyBest=dy;}
                }
                if(cost>25*18) continue;
                divergent+=sx*dxBest+sy*dyBest; ++valid;
            }
            track.box.expansion=track.rate;
            if(track.hits>=3&&valid>=3&&divergent>0&&track.rate>.025&&track.rate<3&&expansion>0) {
                track.box.tti=1/track.rate;
                track.box.confidence=std::min(.95,.55+.08*valid+.01*std::min(track.hits,8));
            }
        }
        b=track.box; next.push_back(track);
        float cx=b.x+b.w/2,foot=b.y+b.h;
        // Drivable mask AND central corridor are required. A road mask alone
        // cannot identify an ego lane or distinguish adjacent traffic.
        bool inPath=cx>.3f&&cx<.7f&&lanes.inRoad(cx,std::min(.99f,foot));
        if(inPath&&b.confidence>=.65&&b.tti<hazard.tti) { hazard.tti=b.tti;hazard.confidence=b.confidence;hazard.inPath=true; }
    }
    tracks_=std::move(next); previous_=gray; last_=time; return hazard;
}
}
