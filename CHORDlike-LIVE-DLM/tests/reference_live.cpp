#include "LiveAudioEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
using namespace chordlike;
int main(int argc, char** argv) {
    if(argc != 8 && argc != 12 && argc != 17) return 2;
    const double rate = std::atof(argv[1]);
    const int depth = std::atoi(argv[2]);
    const float crossfeed = std::atof(argv[3]);
    const float mix = std::atof(argv[4]);
    const float gain = std::atof(argv[5]);
    const int filter = std::atoi(argv[6]);
    const int count = std::atoi(argv[7]);
    LiveAudioEngine engine;
    engine.prepare(rate, 128);
    Settings s;
    s.oversampling = depth;
    s.crossfeed = crossfeed;
    s.mix = mix;
    s.outputDb = gain;
    s.filterMode = filter;
    if(argc >= 12) { s.reconstructionPhase = std::atoi(argv[8]); s.wtaFocus=std::atof(argv[9]); s.phaseMotion=std::atof(argv[10]); s.hfTone=std::atof(argv[11]); }
    if(argc == 17) { s.pulseModel=std::atoi(argv[12]); s.dacArchitecture=std::atoi(argv[13]); s.dacMismatch=std::atof(argv[14]); s.character=std::atof(argv[15]); s.pulseColor=std::atof(argv[16]); }
    std::vector<float> left(count), right(count);
    for(int i=0;i<count;++i) {
        left[i] = float(0.38 * std::sin(0.023 * i) + 0.22 * std::cos(0.007 * i));
        right[i] = float(0.3 * std::sin(0.013 * i + 0.4));
    }
    for(int k=0;k<count;k+=43) engine.process(left.data()+k,right.data()+k,std::min(43,count-k),s);
    std::fwrite(left.data(),sizeof(float),left.size(),stdout);
    std::fwrite(right.data(),sizeof(float),right.size(),stdout);
}
