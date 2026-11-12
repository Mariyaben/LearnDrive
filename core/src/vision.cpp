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
namespace {
// One output pixel: the source pixel at (fx,fy) of the upright frame, or for a downscaled view
// (`step` > 1.5 source pixels per output pixel) the mean luminance of the 2x2 source pixels at the
// footprint's centre: a cheap box filter that removes the finest detail (one-pixel stripes, wires,
// railings), which nearest-neighbour sampling turns into noise. Chroma from the centre.
void writePixel(const YuvFrame& f,const Transform& tr,float fx,float fy,float step,void* tensor,int size,int x,int y,int dtype,int layout,float qs,int zp) {
    float rgb[3]={114,114,114};
    if(fx>=0&&fy>=0&&fx<tr.width&&fy<tr.height) {
        auto xy=source(f,std::min(int(fx),tr.width-1),std::min(int(fy),tr.height-1));
        float Y=read(f.y,xy[0],xy[1]);
        if(step>1.5f) {
            const float d=.5f;float sum=0;
            for(float oy:{-d,d})for(float ox:{-d,d}) {
                const auto q=source(f,std::clamp(int(fx+ox),0,tr.width-1),std::clamp(int(fy+oy),0,tr.height-1));
                sum+=read(f.y,q[0],q[1]);
            }
            Y=sum*.25f;
        }
        float U=read(f.u,xy[0]/2,xy[1]/2)-128.f,V=read(f.v,xy[0]/2,xy[1]/2)-128.f;
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
}
Transform preprocessMosaic(const YuvFrame& f,int size,void* tensor,size_t capacity,int dtype,int layout,float qs,int zp,Gray* gray,float horizon,Mosaic& m) {
    MosaicPlan plan;plan.horizon=horizon;plan.above=.5f;plan.views=1;plan.zoom[0]=2;plan.aim[0]=.5f;plan.smooth=false;
    return preprocessMosaic(f,size,tensor,capacity,dtype,layout,qs,zp,gray,plan,m);
}
Transform preprocessMosaic(const YuvFrame& f,int size,void* tensor,size_t capacity,int dtype,int layout,float qs,int zp,Gray* gray,const MosaicPlan& p,Mosaic& m) {
    m={};
    if(f.width<=0||f.height<=0||size<=0||size>1024||!tensor||dtype<0||dtype>2||layout<0||layout>1||
       (f.rotation!=0&&f.rotation!=90&&f.rotation!=180&&f.rotation!=270)||
       (dtype!=0&&(!std::isfinite(qs)||qs<=0))||capacity<size_t(size)*size*3*(dtype==0?4:1)) throw std::invalid_argument("invalid preprocessing arguments");
    const bool swap=f.rotation==90||f.rotation==270;
    Transform tr{swap?f.height:f.width,swap?f.width:f.height,size,0,0,0};
    const float h=std::isfinite(p.horizon)?std::clamp(p.horizon,.05f,.95f):.5f;
    const float above=std::isfinite(p.above)?std::clamp(p.above,.2f,1.f):.5f;
    const float s=float(size)/tr.width;                          // the road band fills the width
    int wanted=std::clamp(p.views,0,2);
    // A zoomed view must show the camera's own detail: never enlarge a frame pixel more than 1.5x
    // (a 640-wide fallback stream gets only the 2x view, with all the spare rows).
    if(p.smooth)while(wanted>0&&(std::isfinite(p.zoom[wanted-1])?p.zoom[wanted-1]:2.f)*s>1.5f)--wanted;
    // Rows well above the horizon: sky and canopy. A road user's head is that high only right
    // beside the car (0.8.0 kept half the frame height above the horizon: a road user within
    // ~1 m; 0.35 keeps a bus top from ~9 m). The road band keeps at least 40-50 % of the image.
    const float minBand=size*(wanted>=2?.4f:.5f);
    const float cropY=std::clamp((h-above)*tr.height,0.f,std::max(0.f,tr.height-minBand/s));
    const int band=int(std::ceil((tr.height-cropY)*s)),separator=4;
    int views=wanted,spare=size-band-separator*views;
    while(views>0&&spare<48*views){spare+=separator;--views;}   // fewer views when there is no room
    if(tr.width<tr.height||band<=0||views==0)return preprocess(f,size,tensor,capacity,dtype,layout,qs,zp,gray);
    tr.scale=s;tr.padX=0;tr.padY=-cropY*s;
    m.active=true;m.bandBottom=band;m.views=views;m.visibleTop=cropY/tr.height;
    int top=band+separator;
    for(int k=0;k<views;++k) {
        const int rows=k+1<views?spare/views:spare-(spare/views)*(views-1);
        const float zs=(std::isfinite(p.zoom[k])&&p.zoom[k]>=1?p.zoom[k]:2.f)*s,srcW=size/zs,srcH=rows/zs;
        const float centre=(std::isfinite(p.aim[k])?std::clamp(p.aim[k],0.f,1.f):.5f)*tr.width;
        // Horizon 40 % down the view: most of a road user stands below it; far ones straddle it.
        m.view[k]={top,rows,std::clamp(centre-srcW/2,0.f,std::max(0.f,tr.width-srcW)),
            std::clamp(h*tr.height-.4f*srcH,0.f,std::max(0.f,tr.height-srcH)),zs};
        top+=rows+separator;
    }
    for(int y=0;y<size;++y) {
        float scale=0,fy=-1,x0=0;
        if(y<band){scale=s;fy=(y+.5f)/s+cropY;}
        else for(int k=0;k<views;++k)if(y>=m.view[k].top&&y<m.view[k].top+m.view[k].rows) {
            scale=m.view[k].scale;x0=m.view[k].srcX;fy=m.view[k].srcY+(y-m.view[k].top+.5f)/scale;}
        const float step=p.smooth&&scale>0&&scale<.75f?1/scale:1;
        for(int x=0;x<size;++x)writePixel(f,tr,scale>0?x0+(x+.5f)/scale:-1,fy,step,tensor,size,x,y,dtype,layout,qs,zp);
    }
    if(gray) { // Motion cues use the whole frame, as without the mosaic.
        gray->pixels.resize(gray->width*gray->height);
        const float cellX=float(tr.width)/gray->width,cellY=float(tr.height)/gray->height;
        const bool average=p.smooth&&cellX>=4&&cellY>=4;
        for(int y=0;y<gray->height;++y) for(int x=0;x<gray->width;++x) {
            if(!average) {
                auto xy=source(f,std::min(int((x+.5)*tr.width/gray->width),tr.width-1),std::min(int((y+.5)*tr.height/gray->height),tr.height-1));
                gray->pixels[y*gray->width+x]=read(f.y,xy[0],xy[1]);continue;
            }
            int sum=0;
            for(float oy:{-.5f,.5f})for(float ox:{-.5f,.5f}) {
                auto xy=source(f,std::clamp(int((x+.5f)*cellX+ox),0,tr.width-1),std::clamp(int((y+.5f)*cellY+oy),0,tr.height-1));
                sum+=read(f.y,xy[0],xy[1]);
            }
            gray->pixels[y*gray->width+x]=uint8_t(sum/4);
        }
    }
    return tr;
}
void mosaicToFull(float* v,int n,bool cf,const Transform& tr,const Mosaic& m) {
    if(!m.active)return;
    if(!v||n<=0||tr.scale<=0||m.views<1||m.views>2)throw std::invalid_argument("invalid mosaic merge");
    for(int k=0;k<m.views;++k)if(m.view[k].scale<=0)throw std::invalid_argument("invalid mosaic merge");
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
        const MosaicView* view=nullptr;
        for(int k=0;k<m.views;++k)if(y1>=m.view[k].top-1&&y2<=m.view[k].top+m.view[k].rows+1)view=&m.view[k];
        bool keep=view!=nullptr;
        if(keep) {
            const float x1=cx-w/2,x2=cx+w/2;
            // Cut by the view's edge: a partial object the road band (or the wider view) covers.
            keep=x1>edge&&x2<tr.size-edge&&y1>view->top+edge&&y2<view->top+view->rows-edge;
            // View pixels -> frame pixels -> road-band model pixels.
            const float fx1=view->srcX+x1/view->scale,fx2=view->srcX+x2/view->scale;
            const float fy1=view->srcY+(y1-view->top)/view->scale,fy2=view->srcY+(y2-view->top)/view->scale;
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
