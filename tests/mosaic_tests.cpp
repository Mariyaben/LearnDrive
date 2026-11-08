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
        check(m.active&&m.bandBottom>0&&m.stripTop>m.bandBottom&&m.stripTop<300,"no zoom band");
        check(tr.scale==.5f&&tr.padX==0&&tr.padY<0,"road band is not the full width from just above the horizon");
        check(std::abs(m.visibleTop-(.6-.50))<.01,"road band top not half the height above the horizon");
        const auto road=brightIn(t,0,m.bandBottom),zoom=brightIn(t,m.stripTop,320);
        check(road[2]-road[0]==20&&road[3]-road[1]==13,"road band scale wrong");
        check(zoom[2]-zoom[0]==40&&zoom[3]-zoom[1]==26,"zoom band is not twice the road band's scale");
        check(g.pixels.size()==80*80,"motion image missing");});
    test("a box found in the zoom band decodes to the object's true frame position and size",[]{
        Frame f(300,270,340,296);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        const auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,.6f,m);
        const auto z=brightIn(t,m.stripTop,320);
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
        auto straddle=candidate(50,float(m.bandBottom-10),80,float(m.stripTop+10),0);
        mosaicToFull(straddle.data(),1,true,tr,m);check(decodeYolo(straddle.data(),84,84,1,true,false,tr).empty(),"straddling box kept");
        auto cut=candidate(1,float(m.stripTop+20),30,float(m.stripTop+60),2);
        mosaicToFull(cut.data(),1,true,tr,m);check(decodeYolo(cut.data(),84,84,1,true,false,tr).empty(),"box cut by the zoom edge kept");});
    test("a 4:3 camera always has room for the zoom band; a portrait frame keeps the plain letterbox",[]{
        Frame f(10,10,20,20);std::vector<float> t(3*320*320);Gray g;Mosaic m;
        auto tr=preprocessMosaic(f.yuv(),320,t.data(),t.size()*4,0,1,1,0,&g,.2f,m);
        check(m.active&&320-m.stripTop>=48&&tr.padY==0,"high horizon: no zoom band in the letterbox's spare rows");
        auto portrait=f.yuv();portrait.rotation=90;
        tr=preprocessMosaic(portrait,320,t.data(),t.size()*4,0,1,1,0,&g,.5f,m);
        check(!m.active&&tr.padX>0,"portrait frame made a mosaic");});
    std::cout<<passed<<" mosaic groups passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<"FAIL "<<ex.what()<<'\n';return 1;}}
