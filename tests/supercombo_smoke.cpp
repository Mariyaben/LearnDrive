#include "learndrive/perception/supercombo.hpp"
#include <iostream>
#include <fstream>
#include <vector>
using namespace ld;
using namespace ld::perception;
int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"model.onnx output-prefix\n";return 1;}
    try {
        SupercomboRuntime runtime(argv[1],{false,{}});
        std::vector<uint8_t> y(512*256),uv(256*128,128);
        const Matrix3 identity{1,0,0,0,1,0,0,0,1};
        for(int frame=0;frame<3;++frame) {
            for(int row=0;row<256;++row)for(int col=0;col<512;++col)y[row*512+col]=uint8_t(16+(row+col+frame*7)%220);
            const auto decoded=runtime.run({512,256,0,{y.data(),y.size(),512,1},
                {uv.data(),uv.size(),256,1},{uv.data(),uv.size(),256,1},false},identity,1+frame*.05);
            if(!decoded.valid)return 2;
        }
        std::ofstream(std::string(argv[2])+"-input.bin",std::ios::binary).write(
            reinterpret_cast<const char*>(runtime.packedInput()),supercomboInputSize*sizeof(float));
        std::ofstream(std::string(argv[2])+"-output.bin",std::ios::binary).write(
            reinterpret_cast<const char*>(runtime.rawOutput()),supercomboOutputSize*sizeof(float));
        const auto& p=runtime.report();
        std::cout<<"Real Supercombo: "<<p.selected<<", "<<p.baselineMs<<" ms warm inference\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 3;}
}
