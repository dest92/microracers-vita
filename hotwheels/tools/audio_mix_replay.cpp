// Replays a captured stereo s16 stream through the production final Mixer.
#include <winx86-dx/audio/mixer.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace winx86::dx::audio;
struct Source final : PcmSource {
    std::vector<int16_t> pcm;
    size_t pos=0;
    int hz=22050;
    int rate() const override { return hz; }
    int read(int16_t* out,int n) override {
        int got=std::min({n,512,int(pcm.size()/2-pos)});
        std::copy_n(pcm.data()+2*pos,2*got,out);pos+=got;return got;
    }
};
int main(int argc,char** argv) {
    if(argc!=3) {std::fprintf(stderr,"usage: audio_mix_replay input-22050-stereo.s16 output-48000-stereo.s16\n");return 2;}
    std::ifstream f(argv[1],std::ios::binary|std::ios::ate);
    auto size=f.tellg();
    if(size<=0||size%4||size>256*1024*1024) {std::fprintf(stderr,"invalid PCM size\n");return 1;}
    Source source;source.pcm.resize(size_t(size)/2);f.seekg(0);
    f.read(reinterpret_cast<char*>(source.pcm.data()),size);
    std::ofstream out(argv[2],std::ios::binary);
    if(!f||!out)return 1;
    Mixer mixer(48000);mixer.add(&source);
    size_t count=source.pcm.size()/2*48000/22050;
    int16_t block[1024];
    while(count) {
        int n=int(std::min(count,size_t(512)));mixer.mix(block,n);
        out.write(reinterpret_cast<char*>(block),n*4);count-=n;
    }
    return out?0:1;
}
