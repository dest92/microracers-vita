// tests/bench_mixer.cpp — HW-PERF: costo del Mixer por frame de salida (host, -O2).
#include "audio/dsound_bus.h"
#include "audio/mixer.h"
#include <chrono>
#include <cstdio>
#include <vector>
using namespace hw::audio;
struct Gen : PcmSource { int r; explicit Gen(int r_) : r(r_) {}
  int read(int16_t* o, int n) override { for (int i = 0; i < n * 2; ++i) o[i] = (int16_t)(i * 37); return n; }
  int rate() const override { return r; } };
int main() {
    Gen a(22050), b(44100); Mixer m; m.add(&a); m.add(&b);
    std::vector<int16_t> out(512 * 2);
    auto t0 = std::chrono::steady_clock::now();
    const int grains = 20000;
    for (int i = 0; i < grains; ++i) m.mix(out.data(), 512);
    double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    std::printf("mixer 2 fuentes: %.1f ns/frame de salida (%.2f%% de un core a 48 kHz en este host)\n",
                ns / (grains * 512.0), ns / (grains * 512.0) * 48000 / 1e9 * 100);
}
