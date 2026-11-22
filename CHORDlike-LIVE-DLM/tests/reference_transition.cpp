#include "LiveAudioEngine.h"
#include "OutputStage.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
using namespace chordlike;
int main(int argc,char**argv) {
 if(argc!=9)return 2;
 const int depth0=std::atoi(argv[1]), depth1=std::atoi(argv[2]);
 const int pulse0=std::atoi(argv[3]), pulse1=std::atoi(argv[4]);
 const int dac0=std::atoi(argv[5]), dac1=std::atoi(argv[6]);
 const int block=std::atoi(argv[7]), changeBlock=std::atoi(argv[8]);
 const int n=1720;const double sr=48000;
 LiveAudioEngine engine;OutputStage output;engine.prepare(sr,block);output.prepare(sr);
 Settings s;s.oversampling=depth0;s.pulseModel=pulse0;s.dacArchitecture=dac0;
 s.crossfeed=0.27;s.mix=0.9;s.outputDb=-1;s.filterMode=3;
 std::vector<float>L(n),R(n);
 for(int i=0;i<n;i++){L[i]=float(.38*std::sin(.023*i)+.22*std::cos(.007*i));R[i]=float(.3*std::sin(.013*i+.4));}
 for(int k=0,b=0;k<n;k+=block,b++){
    if(b==changeBlock){s.oversampling=depth1;s.pulseModel=pulse1;s.dacArchitecture=dac1;}
    const int m=std::min(block,n-k);
    double sq=0;for(int i=0;i<m;i++){double l=L[k+i],r=R[k+i];sq+=.5*(l*l+r*r);}sq/=m;
    engine.process(L.data()+k,R.data()+k,m,s);
    output.process(L.data()+k,R.data()+k,m,sq,true,false,-1);
 }
 fwrite(L.data(),sizeof(float),L.size(),stdout);fwrite(R.data(),sizeof(float),R.size(),stdout);
}
