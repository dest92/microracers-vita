// SPEC-048 exact replay: calls the production ds_emul per-voice kernel.
#include "runtime/ds_resample.h"
#include "integration/dsound_trace.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace hw::integration;
using namespace d2rt::dsound;
struct Voice {
    std::shared_ptr<std::vector<uint8_t>> data;
    uint32_t len=0,flags=0,ch=0,bits=0,rate=0,fmt=0,align=0,cur=0;
    uint64_t acc=0;
    int32_t vol=0,pan=0;
    bool playing=false,loop=false;
};
static void require(bool ok,const char* msg) { if(!ok) throw std::runtime_error(msg); }
static uint32_t u32(std::istream& f) {
    uint8_t b[4];f.read(reinterpret_cast<char*>(b),4);
    require(bool(f),"truncated trace");
    return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;
}
static uint32_t gain(int32_t mb) {
    if(mb<=-10000)return 0;
    if(mb>=0)return 65536;
    return uint32_t(65536.0*std::pow(10.0,double(mb)/2000.0)+.5);
}
int main(int argc,char** argv) {
    if(argc!=3) { std::fprintf(stderr,"usage: dsound_replay trace.bin output.s16\n");return 2; }
    try {
        std::ifstream f(argv[1],std::ios::binary);
        char magic[8];f.read(magic,8);
        require(bool(f)&&std::memcmp(magic,"HWDS001\0",8)==0,"bad trace magic");
        std::ofstream out(argv[2],std::ios::binary);
        require(bool(out),"cannot open output");
        std::map<uint32_t,Voice> voices;
        uint64_t frames=0,allocated=0,cursor_bad=0;
        bool ended=false;
        while(!ended) {
            const uint32_t kind=u32(f),n=u32(f),bytes=u32(f);
            require(n<=16&&bytes<=4*1024*1024,"record too large");
            std::vector<uint32_t> w(n);
            for(auto& v:w)v=u32(f);
            std::vector<uint8_t> data(bytes);
            if(bytes)f.read(reinterpret_cast<char*>(data.data()),bytes);
            require(bool(f),"truncated payload");
            auto voice=[&]() -> Voice& {
                require(!w.empty(),"missing object");auto it=voices.find(w[0]);
                require(it!=voices.end(),"unknown voice");return it->second;
            };
            auto size=[&](unsigned expected) { require(n==expected,"invalid word count"); };
            switch(kind) {
            case DsCreate: {
                require(n==8||n==9,"invalid Create");
                require(w[2]>0&&w[2]<=4*1024*1024&&voices.size()<4096,"invalid capacity");
                require(w[5]==16&&w[4]>=1&&w[4]<=2&&w[7]==2*w[4]&&w[6]>=100&&w[6]<=200000&&w[2]%w[7]==0
                        &&(n<9||w[8]==1),"unsupported PCM format (runtime requires 16-bit)");
                allocated+=w[2];require(allocated<=128*1024*1024,"trace allocation limit");
                Voice v;v.len=w[2];v.flags=w[3];v.ch=w[4];v.bits=w[5];v.fmt=v.rate=w[6];v.align=w[7];
                v.data=std::make_shared<std::vector<uint8_t>>(v.len,0);
                voices[w[0]]=std::move(v);break;
            }
            case DsDuplicate: {
                size(2);auto it=voices.find(w[1]);require(it!=voices.end(),"duplicate source missing");
                Voice v=it->second;v.cur=0;v.acc=0;v.playing=false;v.loop=false;
                voices[w[0]]=std::move(v);break;
            }
            case DsRelease:size(1);require(voices.erase(w[0])==1,"unknown release");break;
            case DsLock:size(5);(void)voice();break;
            case DsUnlock: {
                size(3);auto& v=voice();require(w[1]<=v.len&&w[2]<=v.len-w[1]&&bytes==w[2],"Unlock range/payload invalid");
                std::copy(data.begin(),data.end(),v.data->begin()+w[1]);break;
            }
            case DsFrequency:size(2);require(w[1]==0||(w[1]>=100&&w[1]<=200000),"invalid frequency");voice().rate=w[1]?w[1]:voice().fmt;break;
            case DsVolume:size(2);voice().vol=int32_t(w[1]);break;
            case DsPan:size(2);voice().pan=int32_t(w[1]);break;
            case DsPosition:size(2);voice().cur=w[1]%voice().len;voice().acc=0;break;
            case DsPlay:size(2);voice().playing=true;voice().loop=(w[1]&1)!=0;break;
            case DsStop:size(1);voice().playing=false;break;
            case DsGetPosition: {
                size(3);const auto& v=voice();uint32_t lead=std::min(1024*v.align,v.len/4);
                lead=std::max(lead,v.align);
                cursor_bad+=(w[1]!=v.cur||w[2]!=(uint64_t(v.cur)+lead)%v.len);break;
            }
            case DsFormat: {
                size(5);auto& v=voice();require(w[2]==16&&w[1]>=1&&w[1]<=2&&w[3]>=100&&w[3]<=200000&&w[4]==2*w[1],"unsupported PCM SetFormat");
                v.ch=w[1];v.bits=w[2];v.fmt=v.rate=w[3];v.align=w[4];break;
            }
            case DsMix: {
                size(4);require((uint64_t(w[1])<<32|w[0])==frames&&w[2]>0&&w[2]<=512&&w[3]>=1000&&w[3]<=192000,"invalid mix timeline");
                std::vector<int32_t> acc(w[2]*2,0);
                for(auto& item:voices) {
                    auto& v=item.second;
                    if(!v.playing||(v.flags&1))continue;
                    uint32_t l=gain(v.vol),r=l;
                    if(v.pan>0)l=uint32_t(uint64_t(l)*gain(-v.pan)>>16);
                    if(v.pan<0)r=uint32_t(uint64_t(r)*gain(v.pan)>>16);
                    ResampleVoice rv{v.data->data(),v.len,v.cur,v.align,v.rate,v.acc,int(v.ch),l,r,v.loop};
                    mix_voice(rv,w[3],int(w[2]),acc.data());
                    v.acc+=uint64_t(w[2])*v.rate;
                    uint32_t adv=uint32_t(v.acc/w[3])*v.align;v.acc%=w[3];
                    if(adv) {
                        uint64_t pos=uint64_t(v.cur)+adv;
                        if(v.loop)v.cur=uint32_t(pos%v.len);
                        else if(pos>=v.len) {v.cur=v.len>=v.align?v.len-v.align:0;v.acc=0;v.playing=false;}
                        else v.cur=uint32_t(pos);
                    }
                }
                for(int32_t s:acc) {
                    const uint16_t q=uint16_t(std::clamp(s,-32768,32767));
                    const char b[]={char(q),char(q>>8)};out.write(b,2);
                }
                frames+=w[2];break;
            }
            case DsOutput:size(4);require(bytes==w[2]*4,"output length invalid");break;
            case DsEnd:size(1);require(w[0]==0,"trace capped or incomplete");ended=true;break;
            default:throw std::runtime_error("unknown event");
            }
        }
        require(f.peek()==std::char_traits<char>::eof(),"bytes after END");
        require(bool(out),"output write failed");
        std::printf("replay_frames=%llu cursor_mismatches=%llu\n",(unsigned long long)frames,(unsigned long long)cursor_bad);
    } catch(const std::exception& e) {std::fprintf(stderr,"replay: %s\n",e.what());return 1;}
    return 0;
}
