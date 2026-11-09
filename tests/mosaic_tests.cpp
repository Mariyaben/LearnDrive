// Detector mosaic (road band + 2x zoom band in one inference): the zoom band shows the far road
// at twice the scale, and a box found there decodes to the SAME frame geometry as the object
// (so distances from foot position and box height are unchanged).
#include "learndrive/vision.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ld;
namespace {
int passed=0;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void test(const char* name,F f){f();++passed;std::cout<<"PASS "<<name<<'\n';}
// A 640x480 grey frame with a bright rectangle (x1,y1)-(x2,y2) in frame pixels.
struct Frame {
    int w=640,h=480;std::vector<uint8_t> y,u,v;
    Frame(int x1,int y1,int x2,int y2):y(size_t(w)*h,60),u(size_t(w/2)*(h/2),128),v(size_t(w/2)*(h/2),128) {
        for(int r=y1;r<y2;++r)for(int c=x1;c<x2;++c)y[size_t(r)*w+c]=240;}
    YuvFrame yuv() const {return {w,h,0,{y.data(),y.size(),w,1},{u.data(),u.size(),w/2,1},{v.data(),v.size(),w/2,1},true};}
};
// The bright region's bounds in the [3,320,320] float tensor (channel 0), within rows [r0,r1).
std::array<int,4> brightIn(const std::vector<float>& t,int r0,int r1) {
    int x1=999,y1=999,x2=-1,y2=-1;
    for(int y=r0;y<r1;++y)for(int x=0;x<320;++x)if(t[size_t(y)*320+x]>.8f){x1=std::min(x1,x);y1=std::min(y1,y);x2=std::max(x2,x);y2=std::max(y2,y);}
    return {x1,y1,x2+1,y2+1};
}
// One YOLOv8 candidate ([84,1] channel-first) for a box in model pixels.
std::vector<float> candidate(float x1,float y1,float x2,float y2,int cls) {
    std::vector<float> v(84,0);v[0]=(x1+x2)/2;v[1]=(y1+y2)/2;v[2]=x2-x1;v[3]=y2-y1;v[4+cls]=.9f;return v;
}
}
int main(){try{
    test("the mosaic keeps the road band at full width and puts a 2x zoom of the far road below it",[]{
        // A distant car near the horizon (horizon at 60% of the height).
        Frame f(300,270,340,296);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        const auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,.6f,m);
        check(m.active&&m.bandBottom>0&&m.stripTop()>m.bandBottom&&m.stripTop()<300,"no zoom band");
        check(tr.scale==.5f&&tr.padX==0&&tr.padY<0,"road band is not the full width from just above the horizon");
        check(std::abs(m.visibleTop-(.6-.50))<.01,"road band top not half the height above the horizon");
        const auto road=brightIn(t,0,m.bandBottom),zoom=brightIn(t,m.stripTop(),320);
        check(road[2]-road[0]==20&&road[3]-road[1]==13,"road band scale wrong");
        check(zoom[2]-zoom[0]==40&&zoom[3]-zoom[1]==26,"zoom band is not twice the road band's scale");
        check(g.pixels.size()==80*80,"motion image missing");});
    test("a box found in the zoom band decodes to the object's true frame position and size",[]{
        Frame f(300,270,340,296);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        const auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,.6f,m);
        const auto z=brightIn(t,m.stripTop(),320);
        auto raw=candidate(float(z[0]),float(z[1]),float(z[2]),float(z[3]),2);
        mosaicToFull(raw.data(),1,true,tr,m);
        const auto boxes=decodeYolo(raw.data(),raw.size(),84,1,true,false,tr);
        check(boxes.size()==1,"zoom-band box lost");
        const auto& b=boxes[0];
        // Frame-normalized: x 300..340 / 640, y 270..296 / 480 (within a pixel).
        check(std::abs(b.x*640-300)<1.1&&std::abs((b.x+b.w)*640-340)<1.1,"horizontal geometry changed");
        check(std::abs(b.y*480-270)<1.1&&std::abs((b.y+b.h)*480-296)<1.1,"vertical geometry (foot row, height) changed");});
    test("a box found in the road band decodes as before; boxes cut by a band edge are dropped",[]{
        // A near person low in the frame.
        Frame f(100,350,140,470);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        const auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,.6f,m);
        const auto r=brightIn(t,0,m.bandBottom);
        auto raw=candidate(float(r[0]),float(r[1]),float(r[2]),float(r[3]),0);
        mosaicToFull(raw.data(),1,true,tr,m);
        auto boxes=decodeYolo(raw.data(),raw.size(),84,1,true,false,tr);
        check(boxes.size()==1&&std::abs(boxes[0].y*480-350)<2.1&&std::abs((boxes[0].y+boxes[0].h)*480-470)<2.1,"road band geometry changed");
        // Straddling the two bands, or touching the zoom band's edge: a partial object.
        auto straddle=candidate(50,float(m.bandBottom-10),80,float(m.stripTop()+10),0);
        mosaicToFull(straddle.data(),1,true,tr,m);check(decodeYolo(straddle.data(),84,84,1,true,false,tr).empty(),"straddling box kept");
        auto cut=candidate(1,float(m.stripTop()+20),30,float(m.stripTop()+60),2);
        mosaicToFull(cut.data(),1,true,tr,m);check(decodeYolo(cut.data(),84,84,1,true,false,tr).empty(),"box cut by the zoom edge kept");});
    test("a 4:3 camera always has room for the zoom band; a portrait frame keeps the plain letterbox",[]{
        Frame f(10,10,20,20);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,.2f,m);
        check(m.active&&320-m.stripTop()>=48&&tr.padY==0,"high horizon: no zoom band in the letterbox's spare rows");
        auto portrait=f.yuv();portrait.rotation=90;
        tr=preprocessMosaic(portrait,320,t.data(),t.size()*4,0,1,1,0,&g,.5f,m);
        check(!m.active&&tr.padX>0,"portrait frame made a mosaic");});
    // Long range (0.8.3): the phone's 1280x960 frame, cropped to the wide preview (bottom 72 %).
    struct Wide {
        int w=1280,h=692;std::vector<uint8_t> y,u,v;
        Wide():y(size_t(w)*h,60),u(size_t(w/2)*(h/2),128),v(size_t(w/2)*(h/2),128){}
        void rect(int x1,int y1,int x2,int y2,uint8_t value=240){for(int r=y1;r<y2;++r)for(int c=x1;c<x2;++c)y[size_t(r)*w+c]=value;}
        YuvFrame yuv() const {return {w,h,0,{y.data(),y.size(),w,1},{u.data(),u.size(),w/2,1},{v.data(),v.size(),w/2,1},true};}
    };
    auto longRange=[](float horizon,float aimNear,float aimFar){MosaicPlan p;p.horizon=horizon;p.above=.35f;p.views=2;
        p.zoom[0]=2;p.zoom[1]=4;p.aim[0]=aimNear;p.aim[1]=aimFar;p.smooth=true;return p;};
    test("long range: a 1280-wide frame gets a 2x and a 4x view; the 4x view is the camera's own pixels",[&]{
        // A person ~60 m ahead (S24 Ultra main camera, 1280 wide: ~850 px per radian): 12 x 24 px,
        // feet ~18 px below the horizon (0.6 of the height: a phone pitched up ~13 degrees).
        Wide f;f.rect(634,409,646,433);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        const auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,longRange(.6f,.5f,.5f),m);
        check(m.active&&m.views==2&&m.view[0].top>m.bandBottom&&m.view[1].top>m.view[0].top+m.view[0].rows,"two zoomed views expected");
        check(m.view[0].rows>=48&&m.view[1].rows>=48,"a view too short to hold a road user");
        check(std::abs(m.view[1].scale-1.f)<1e-5f&&std::abs(m.view[0].scale-.5f)<1e-5f&&tr.scale==.25f,"scales: road 1/4, near view 1/2, far view native");
        check(std::abs(m.visibleTop-(.6-.35))<.01,"road band top not 35 % of the height above the horizon");
        const auto far=brightIn(t,m.view[1].top,m.view[1].top+m.view[1].rows),near=brightIn(t,m.view[0].top,m.view[0].top+m.view[0].rows);
        check(far[2]-far[0]==12&&far[3]-far[1]==24,"far view is not the native 12x24 px");
        check(std::abs((near[2]-near[0])-6)<=1&&std::abs((near[3]-near[1])-12)<=1,"near view is not half size");
        // Decoded from the far view: the person's true frame geometry (for distance from height and feet).
        auto raw=candidate(float(far[0]),float(far[1]),float(far[2]),float(far[3]),0);
        mosaicToFull(raw.data(),1,true,tr,m);
        const auto boxes=decodeYolo(raw.data(),raw.size(),84,1,true,false,tr);
        check(boxes.size()==1,"far-view box lost");
        check(std::abs(boxes[0].x*1280-634)<1.1&&std::abs((boxes[0].x+boxes[0].w)*1280-646)<1.1,"far view: horizontal geometry changed");
        check(std::abs(boxes[0].y*692-409)<1.1&&std::abs((boxes[0].y+boxes[0].h)*692-433)<1.1,"far view: foot row or height changed");
        // A horizon low in the image (pitched up 17 degrees): the road band still keeps 40 % of the rows.
        preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,longRange(.69f,.5f,.5f),m);
        check(m.bandBottom>=128&&m.views==2,"a low horizon squeezed the road band");});
    test("long range: the zoomed views follow the road into a bend",[&]{
        Wide f;std::vector<float> t(3*320*320);Gray g;Mosaic m;
        preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,longRange(.6f,.35f,.25f),m);
        const float nearCentre=m.view[0].srcX+160/m.view[0].scale,farCentre=m.view[1].srcX+160/m.view[1].scale;
        check(std::abs(nearCentre-.35f*1280)<1&&std::abs(farCentre-.25f*1280)<1,"views not centred where the road goes");
        preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,longRange(.6f,.02f,.0f),m);
        check(m.view[0].srcX==0&&m.view[1].srcX==0,"a view aimed past the frame edge must stay inside the frame");});
    test("long range: downscaled views are box-filtered, so fine detail does not alias into noise",[&]{
        // One-pixel vertical stripes: nearest-neighbour sampling at 1/4 scale lands on one colour.
        Wide f;for(int r=0;r<f.h;++r)for(int c=0;c<f.w;c+=2)f.y[size_t(r)*f.w+c]=240;
        std::vector<float> t(3*320*320);Gray g;Mosaic m;auto plan=longRange(.6f,.5f,.5f);
        preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,plan,m);
        const float smooth=t[size_t(20)*320+100];
        plan.smooth=false;preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,plan,m);
        const float sharp=t[size_t(20)*320+100];
        check(std::abs(smooth-150.f/255)<.03f,"road band not averaged");
        check(std::abs(sharp-150.f/255)>.2f,"control: nearest sampling should alias");});
    test("long range: a 640-wide frame or no room falls back to fewer views, never a broken layout",[&]{
        Frame f(10,10,20,20);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        // Horizon high in a 4:3 frame: the road band needs most rows; at least one view remains.
        auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,longRange(.2f,.5f,.5f),m);
        check(m.active&&m.views>=1&&m.view[m.views-1].top+m.view[m.views-1].rows<=320&&tr.scale==.5f,"no valid layout");
        for(int k=0;k<m.views;++k)check(m.view[k].rows>=48,"a view too short");
        // A 640-wide stream has no detail for a 4x view (it would be 2x enlarged): one 2x view only.
        check(m.views==1&&std::abs(m.view[0].scale-1.f)<1e-5f,"a 640-wide frame got an enlarged 4x view");});
    std::cout<<passed<<" mosaic groups passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<"FAIL "<<ex.what()<<'\n';return 1;}}
