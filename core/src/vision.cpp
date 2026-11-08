#include "learndrive/vision.hpp"
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cmath>
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
Transform preprocess(const YuvFrame& f,int size,void* tensor,size_t capacity,int dtype,int layout,float qs,int zp,Gray* gray,const Crop* crop) {
    if(f.width<=0||f.height<=0||size<=0||size>1024||!tensor||dtype<0||dtype>2||layout<0||layout>1||
       (f.rotation!=0&&f.rotation!=90&&f.rotation!=180&&f.rotation!=270)||
       (dtype!=0&&(!std::isfinite(qs)||qs<=0))||capacity<size_t(size)*size*3*(dtype==0?4:1)) throw std::invalid_argument("invalid preprocessing arguments");
    bool swap=f.rotation==90||f.rotation==270;
    Transform tr{swap?f.height:f.width,swap?f.width:f.height,size,0,0,0};
    int offsetX=0,offsetY=0;
    if(crop) {
        if(crop->side<8||crop->x<0||crop->y<0||crop->x+crop->side>tr.width||crop->y+crop->side>tr.height)throw std::invalid_argument("invalid zoom crop");
        offsetX=crop->x;offsetY=crop->y;tr.width=tr.height=crop->side;gray=nullptr;
    }
    tr.scale=std::min(float(size)/tr.width,float(size)/tr.height);
    tr.padX=(size-tr.width*tr.scale)/2; tr.padY=(size-tr.height*tr.scale)/2;
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        float fx=(x+.5f-tr.padX)/tr.scale,fy=(y+.5f-tr.padY)/tr.scale;
        float rgb[3]={114,114,114};
        if(fx>=0&&fy>=0&&fx<tr.width&&fy<tr.height) {
            auto xy=source(f,offsetX+std::min(int(fx),tr.width-1),offsetY+std::min(int(fy),tr.height-1));
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
Transform preprocessMosaic(const YuvFrame& f,int size,void* tensor,size_t capacity,int dtype,int layout,float qs,int zp,Gray* gray,float horizon,Mosaic& m) {
    m={};
    if(f.width<=0||f.height<=0||size<=0||size>1024||!tensor||dtype<0||dtype>2||layout<0||layout>1||
       (f.rotation!=0&&f.rotation!=90&&f.rotation!=180&&f.rotation!=270)||
       (dtype!=0&&(!std::isfinite(qs)||qs<=0))||capacity<size_t(size)*size*3*(dtype==0?4:1)) throw std::invalid_argument("invalid preprocessing arguments");
    const bool swap=f.rotation==90||f.rotation==270;
    Transform tr{swap?f.height:f.width,swap?f.width:f.height,size,0,0,0};
    const float h=std::isfinite(horizon)?std::clamp(horizon,.2f,.85f):.5f;
    const float s=float(size)/tr.width;                          // the road band fills the width
    // Rows more than half the height above the horizon: sky and canopy. A road user's head is
    // that high only within ~1 m on a level road (or ~3 m for someone on a raised verandah);
    // the road band always keeps at least half the image.
    const float cropY=std::clamp((h-.50f)*tr.height,0.f,std::max(0.f,tr.height-size*.5f/s));
    const int band=int(std::ceil((tr.height-cropY)*s)),separator=4,strip=size-band-separator;
    if(tr.width<tr.height||band<=0||strip<48)return preprocess(f,size,tensor,capacity,dtype,layout,qs,zp,gray);
    tr.scale=s;tr.padX=0;tr.padY=-cropY*s;
    // Zoom band: 2x, centred across the frame, horizon 40% down it (most of a road user
    // stands below the horizon; far ones straddle it).
    const float zs=2*s,srcW=size/zs,srcH=strip/zs;
    const float srcX=std::max(0.f,(tr.width-srcW)/2),srcY=std::clamp(h*tr.height-.4f*srcH,0.f,std::max(0.f,tr.height-srcH));
    m={true,band,band+separator,srcX,srcY,zs,cropY/tr.height};
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        float fx=-1,fy=-1;
        if(y<band){fx=(x+.5f)/s;fy=(y+.5f)/s+cropY;}
        else if(y>=m.stripTop){fx=srcX+(x+.5f)/zs;fy=srcY+(y-m.stripTop+.5f)/zs;}
        float rgb[3]={114,114,114};
        if(fx>=0&&fy>=0&&fx<tr.width&&fy<tr.height) {
            auto xy=source(f,std::min(int(fx),tr.width-1),std::min(int(fy),tr.height-1));
            float Y=read(f.y,xy[0],xy[1]),U=read(f.u,xy[0]/2,xy[1]/2)-128.f,V=read(f.v,xy[0]/2,xy[1]/2)-128.f;
            if(f.fullRange){rgb[0]=Y+1.402f*V;rgb[1]=Y-.344136f*U-.714136f*V;rgb[2]=Y+1.772f*U;}
            else {Y=1.164383f*(Y-16);rgb[0]=Y+1.596027f*V;rgb[1]=Y-.391762f*U-.812968f*V;rgb[2]=Y+2.017232f*U;}
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
    if(gray) { // Motion cues use the whole frame, as without the mosaic.
        gray->pixels.resize(gray->width*gray->height);
        for(int y=0;y<gray->height;++y) for(int x=0;x<gray->width;++x) {
            auto xy=source(f,std::min(int((x+.5)*tr.width/gray->width),tr.width-1),std::min(int((y+.5)*tr.height/gray->height),tr.height-1));
            gray->pixels[y*gray->width+x]=read(f.y,xy[0],xy[1]);
        }
    }
    return tr;
}
void mosaicToFull(float* v,int n,bool cf,const Transform& tr,const Mosaic& m) {
    if(!m.active)return;
    if(!v||n<=0||tr.scale<=0||m.stripScale<=0)throw std::invalid_argument("invalid mosaic merge");
    auto at=[&](int c,int i)->float&{return v[cf?size_t(c)*n+i:size_t(i)*84+c];};
    const float edge=2;
    for(int i=0;i<n;++i) {
        const float cx=at(0,i),cy=at(1,i),w=at(2,i),h=at(3,i),y1=cy-h/2,y2=cy+h/2;
        if(y2<=m.bandBottom+1)continue; // Road band: already in its own coordinates.
        // A road user touching the frame's bottom edge (a passing bus, a close autorickshaw):
        // the box overshoots the band a little. It is that object, clipped to the frame.
        if(cy<m.bandBottom&&y2<=m.bandBottom+std::max(8.f,.12f*h)) {
            const float bottom=float(m.bandBottom);at(1,i)=(y1+bottom)/2;at(3,i)=bottom-y1;continue;
        }
        bool keep=y1>=m.stripTop-1;
        if(keep) {
            const float x1=cx-w/2,x2=cx+w/2;
            // Cut by the zoom band's edge: a partial object the road band covers.
            keep=x1>edge&&x2<tr.size-edge&&y1>m.stripTop+edge&&y2<tr.size-edge;
            // Zoom-band pixels -> frame pixels -> road-band model pixels.
            const float fx1=m.srcX+x1/m.stripScale,fx2=m.srcX+x2/m.stripScale;
            const float fy1=m.srcY+(y1-m.stripTop)/m.stripScale,fy2=m.srcY+(y2-m.stripTop)/m.stripScale;
            at(0,i)=(fx1+fx2)/2*tr.scale+tr.padX;at(1,i)=(fy1+fy2)/2*tr.scale+tr.padY;
            at(2,i)=(fx2-fx1)*tr.scale;at(3,i)=(fy2-fy1)*tr.scale;
        }
        if(!keep)for(int c=4;c<84;++c)at(c,i)=0;
    }
}
Crop zoomCrop(int width,int height,float horizon) {
    if(width<=0||height<=0||!std::isfinite(horizon))throw std::invalid_argument("invalid zoom frame");
    const int side=std::min(std::max(width,height)/2,std::min(width,height));
    const int x=(width-side)/2;
    const int y=std::clamp(int(std::lround(double(horizon)*height-side/2.)),0,height-side);
    return {x,y,side};
}
void zoomToFull(float* v,int n,const Transform& full,const Transform& zoom,const Crop& crop) {
    if(!v||n<=0||full.scale<=0||zoom.scale<=0)throw std::invalid_argument("invalid zoom merge");
    const float edge=2.f; // crop pixels
    for(int i=0;i<n;++i) {
        float& cx=v[i];float& cy=v[n+i];float& w=v[2*n+i];float& h=v[3*n+i];
        // Crop model pixels -> crop pixels -> upright frame pixels -> full model pixels.
        const float x1=(cx-w/2-zoom.padX)/zoom.scale,x2=(cx+w/2-zoom.padX)/zoom.scale;
        const float y1=(cy-h/2-zoom.padY)/zoom.scale,y2=(cy+h/2-zoom.padY)/zoom.scale;
        if(!(x1>edge&&y1>edge&&x2<crop.side-edge&&y2<crop.side-edge))for(int c=4;c<84;++c)v[size_t(c)*n+i]=0;
        cx=((x1+x2)/2+crop.x)*full.scale+full.padX;cy=((y1+y2)/2+crop.y)*full.scale+full.padY;
        w=(x2-x1)*full.scale;h=(y2-y1)*full.scale;
    }
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
        if(sy<0){map.road[y*80+x]=map.lane[y*80+x]=0;continue;} // Above the mosaic's road band (sky).
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
