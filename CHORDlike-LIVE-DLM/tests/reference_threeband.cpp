#include "LiveAudioEngine.h"
#include "OutputStage.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
using namespace chordlike;
int main(int argc, char** argv) {
    if(argc != 16) return 2;
    const double rate=std::atof(argv[1]);
    const int depth=std::atoi(argv[2]);
    const float cross=std::atof(argv[3]);
    const float mix=std::atof(argv[4]);
    const float gain=std::atof(argv[5]);
    const int filter=std::atoi(argv[6]);
    const int count=std::atoi(argv[7]);
    const bool autoMatch=std::atoi(argv[8])!=0;
    const bool limiter=std::atoi(argv[9])!=0;
    const float ceiling=std::atof(argv[10]);
    const int blockSize=std::atoi(argv[11]);
    const int dac=std::atoi(argv[12]);
    LiveAudioEngine engine;
    OutputStage output;
    engine.prepare(rate,blockSize);
    output.prepare(rate);
    Settings s;
    s.oversampling=depth;
    s.crossfeed=cross;
    s.mix=mix;
    s.outputDb=gain;
    s.filterMode=filter;
    s.dacArchitecture=dac;
    s.tubeRouting=1;
    s.tubeLowHz=std::atof(argv[13]);
    s.tubeHighHz=std::atof(argv[14]);
    const int mode=std::atoi(argv[15]);
    s.tubeBands[0].type=mode==1?0:2;
    s.tubeBands[0].topology=0;
    s.tubeBands[0].driveDb=14.1f;
    s.tubeBands[0].bias=0.408f;
    s.tubeBands[0].wet=0.696f;
    s.tubeBands[1].type=mode==1?0:1;
    s.tubeBands[1].topology=0;
    s.tubeBands[1].driveDb=14.8f;
    s.tubeBands[1].bias=0.368f;
    s.tubeBands[1].wet=0.616f;
    s.tubeBands[2].type=mode==1?0:3;
    s.tubeBands[2].topology=0;
    s.tubeBands[2].driveDb=27.6f;
    s.tubeBands[2].bias=0.44f;
    s.tubeBands[2].wet=1.0f;
    std::vector<float> left(count),right(count);
    for(int i=0;i<count;i++) {
        left[i]=float(0.38*std::sin(0.023*i)+0.22*std::cos(0.007*i));
        right[i]=float(0.3*std::sin(0.013*i+0.4));
    }
    for(int k=0;k<count;k+=blockSize) {
        int n=std::min(blockSize,count-k);
        double inputSquare=0.0;
        for(int i=0;i<n;i++) {
            const double l=left[k+i],r=right[k+i];
            inputSquare+=0.5*(l*l+r*r);
        }
        if(n>0)inputSquare/=n;
        engine.process(left.data()+k,right.data()+k,n,s);
        output.process(left.data()+k,right.data()+k,n,inputSquare,autoMatch,limiter,ceiling);
    }
    std::fwrite(left.data(),sizeof(float),left.size(),stdout);
    std::fwrite(right.data(),sizeof(float),right.size(),stdout);
}
