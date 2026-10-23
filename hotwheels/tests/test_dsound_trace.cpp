// Trace writer must be inert by default and terminate capped files explicitly.
#include "integration/dsound_trace.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
int main() {
    using hw::integration::DsoundTrace;
    const std::string p="/tmp/hw069-trace-"+std::to_string(getpid());
    DsoundTrace t;
    const uint32_t w[]={0};
    t.event(1,w,1,nullptr,0); // unopened: no file
    assert(!t.active());
    assert(t.open(p.c_str(),128));
    const char payload[100]{};
    t.event(1,w,1,payload,sizeof payload); // over cap, reserve room for END
    t.close();
    std::ifstream f(p,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)),{});
    assert(bytes.size()<=128 && bytes.size()>=24);
    assert(bytes.substr(0,8)==std::string("HWDS001\0",8));
    assert(static_cast<unsigned char>(bytes[8])==16); // END
    assert(static_cast<unsigned char>(bytes[20])==1); // capped
    std::remove(p.c_str());
    assert(t.open(p.c_str(),128));
    t.event(17,w,1,nullptr,0); // invalid guest range marks trace, never changes HRESULT
    t.close();
    std::ifstream invalid(p,std::ios::binary);
    std::string bad((std::istreambuf_iterator<char>(invalid)),{});
    assert(bad.size()==24 && static_cast<unsigned char>(bad[20])==2);
    std::remove(p.c_str());
    std::puts("DSOUND-TRACE: PASS");
}
