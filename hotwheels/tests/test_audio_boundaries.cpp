// SPEC-048 CA4: signal must not depend on source pull/mix partitioning.
#include <winx86-dx/audio/mixer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace winx86::dx::audio;

struct Source final : PcmSource {
    std::vector<int16_t> pcm;
    int offset = 0, chunk, hz;
    Source(int n, int limit, int rate) : pcm(n * 2), chunk(limit), hz(rate) {
        for (int i = 0; i < n; ++i) {
            pcm[2*i] = int16_t(14000 * std::sin(2 * 3.141592653589793 * 0.213 * i));
            pcm[2*i+1] = int16_t(12000 * std::cos(2 * 3.141592653589793 * 0.117 * i));
        }
    }
    int rate() const override { return hz; }
    int read(int16_t* out, int n) override {
        const int got = std::min({n, chunk, int(pcm.size()/2) - offset});
        std::copy_n(pcm.data() + 2*offset, 2*got, out);
        offset += got;
        return got;
    }
};

static bool check(int hz, int out_hz, int chunk, int mix_chunk, std::vector<int16_t>* saved) {
    Source src(24000, chunk, hz);
    Mixer mixer(out_hz);
    mixer.add(&src);
    const int n = 15000;
    std::vector<int16_t> actual(n*2);
    for (int at=0; at<n; at+=mix_chunk)
        mixer.mix(actual.data()+2*at, std::min(mix_chunk,n-at));
    int worst=0, bad=0;
    double sq=0;
    for (int i=0; i<n; ++i) {
        const double x = double(i)*hz/out_hz;
        const int j=int(x);
        if (j+1 >= int(src.pcm.size()/2)) break;
        const double frac=x-j;
        for (int c=0;c<2;++c) {
            const int expect=int(src.pcm[2*j+c]+(src.pcm[2*(j+1)+c]-src.pcm[2*j+c])*frac);
            const int err=std::abs(int(actual[2*i+c])-expect);
            worst=std::max(worst,err); bad+=err>1; sq+=double(err)*err;
        }
    }
    std::printf("BOUNDARY rate=%d->%d read=%d mix=%d max_error=%d bad=%d rms_error=%.3f\n",
                hz,out_hz,chunk,mix_chunk,worst,bad,std::sqrt(sq/(n*2)));
    *saved=std::move(actual);
    return worst<=1;
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool ok=true;
    for (auto rates : {std::pair<int,int>{22050,48000},{48000,22050},{11025,48000},{48000,48000}})
        for (int read : {1,37,512,4096}) {
            std::vector<int16_t> baseline;
            for (int mix : {127,512,15000}) {
                std::vector<int16_t> actual;
                ok=check(rates.first,rates.second,read,mix,&actual)&&ok;
                if (baseline.empty()) baseline=actual;
                else {
                    int max_delta=0;
                    for (size_t i=0;i<actual.size();++i)
                        max_delta=std::max(max_delta,std::abs(int(actual[i])-baseline[i]));
                    std::printf("PARTITION rate=%d->%d read=%d mix=%d max_delta=%d\n",rates.first,rates.second,read,mix,max_delta);
                    ok=(max_delta<=1)&&ok;
                }
            }
        }
    std::puts(ok?"AUDIO-BOUNDARIES: PASS":"AUDIO-BOUNDARIES: FAIL");
    return ok?0:1;
}
