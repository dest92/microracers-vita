// CPU-cost diagnostic. Wall ns/sample on host or qemu is NOT A9 cycles.
#include "runtime/ds_resample.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
int main() {
    constexpr int n=4096,grain=512,iterations=512;
    std::vector<int16_t> pcm(n);
    for(int i=0;i<n;++i)pcm[i]=int16_t(10000*std::sin(i*.063));
    std::vector<int32_t> mix(grain*2);
    long long checksum=0;
    for(uint32_t out : {22050u,48000u}) {
        const auto start=std::chrono::steady_clock::now();
        d2rt::dsound::ResampleVoice v{reinterpret_cast<const uint8_t*>(pcm.data()),n*2,0,2,32342,0,1,65536,50000,true};
        for(int k=0;k<iterations;++k) {
            std::fill(mix.begin(),mix.end(),0);
            d2rt::dsound::mix_voice(v,out,grain,mix.data());checksum+=mix[k%grain];
        }
        const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        std::printf("rate=%u channels=1 ns_per_output_frame=%.3f checksum=%lld\n",out,ns/(grain*iterations),checksum);
    }
}
