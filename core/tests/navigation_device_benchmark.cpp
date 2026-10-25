// Standalone Android/desktop microbenchmark of the same route-context source.
// No JNI, rendering or camera cost is included. Build without LTO.
#include "learndrive/navigation.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
static std::atomic<size_t> allocations{0};
void* operator new(size_t n){++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
int main() {
    using namespace ld;using namespace ld::navigation;
    auto xy=[](double x,double y)->Coordinate{return {10.+y/6371008.8*180/pi,76.+x/6371008.8*180/pi/std::cos(10*pi/180)};};
    std::vector<Coordinate> shape;
    for(double y=0;y<=90;y+=10)shape.push_back(xy(0,y));
    for(double s=10;s<=150;s+=10)shape.push_back(xy(s*std::sin(50*pi/180),90+s*std::cos(50*pi/180)));
    RouteCoach coach;if(!coach.load(1,shape.data(),shape.size(),nullptr,0))return 1;
    const auto p=xy(0,0);
    coach.fix({1,p.latitude,p.longitude,3,45,0,.5,true,true});
    coach.fix({2,p.latitude,p.longitude,3,45,0,.5,true,true});
    if(coach.evaluate(2,false,false)!=Prompt::mapRightBend)return 2;
    constexpr int iterations=100000;
    const auto before=allocations.load();const auto start=std::chrono::steady_clock::now();
    volatile uint64_t checksum=0;
    for(int i=0;i<iterations;++i)checksum+=coach.context(2).featureId+uint64_t(coach.evaluate(2,false,false));
    const auto end=std::chrono::steady_clock::now();const auto heap=allocations.load()-before;
    const double us=std::chrono::duration<double,std::micro>(end-start).count()/iterations;
    std::cout<<"{\"scenario\":\"50deg_right_bend_90m_45kmh\",\"iterations\":"<<iterations
        <<",\"context_and_policy_mean_us\":"<<us<<",\"heap_allocations\":"<<heap
        <<",\"checksum\":"<<checksum<<",\"includes_jni_render_camera\":false}\n";
    return heap==0&&us<500?0:3;
}
