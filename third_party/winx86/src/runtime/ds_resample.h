// ds_resample.h — per-voice mix step of ds_emul, kept pure (no Cpu, no locks)
// so it can be tested on the host.
//
// The read position advances by rate/out_rate with exact integer arithmetic
// (`acc` is the fractional remainder in units of 1/out_rate), the same as the
// voice cursor in ds_emul, so mix position and cursor never drift apart.
// Samples are linearly interpolated between the current frame and the next
// one using that same remainder. At rate == out_rate the remainder stays zero
// and output is sample-for-sample identical to the source. The phase
// (pos, acc) carries across calls, so a SetFrequency between grains changes
// the step without a discontinuity.
#pragma once
#include <cstdint>
#include <cstring>

namespace d2rt { namespace dsound {

struct ResampleVoice {
    const uint8_t* host; uint32_t len, pos, blockAlign, rate;
    uint64_t acc; int ch; uint32_t gL, gR; bool loop;
};

// Mixes `frames` stereo output frames into out[2*frames]. Returns false once a
// one-shot voice runs past its end (the rest of the grain stays silent).
inline bool mix_voice(ResampleVoice& v, uint32_t out_rate, int frames, int32_t* out) {
    const uint32_t width = (v.ch == 2) ? 4u : 2u;
    if (!v.blockAlign || v.len < width || !out_rate) return false;
    if (v.pos % v.len + width > v.len) v.pos = 0; else v.pos %= v.len;
    // Q16 fraction from a reciprocal computed once per call: the Cortex-A9 has
    // no integer divider, so no division per sample.
    const uint64_t inv = (1ull << 32) / out_rate;
    auto rd = [&](uint32_t p, int k) { int16_t s; std::memcpy(&s, v.host + p + 2 * k, 2); return (int32_t)s; };
    for (int i = 0; i < frames; i++) {
        uint32_t np = v.pos + v.blockAlign;
        if (np + width > v.len) np = v.loop ? 0 : v.pos;   // one-shot end: hold the last frame
        const int64_t f = (int64_t)((v.acc * inv) >> 16);   // acc/out_rate in Q16
        int32_t l0 = rd(v.pos, 0), r0 = (v.ch == 2) ? rd(v.pos, 1) : l0;
        if (f) {
            const int32_t l1 = rd(np, 0), r1 = (v.ch == 2) ? rd(np, 1) : l1;
            l0 += (int32_t)(((int64_t)(l1 - l0) * f) >> 16);
            r0 += (int32_t)(((int64_t)(r1 - r0) * f) >> 16);
        }
        out[2 * i]     += (int32_t)(((int64_t)l0 * (int64_t)v.gL) >> 16);
        out[2 * i + 1] += (int32_t)(((int64_t)r0 * (int64_t)v.gR) >> 16);
        v.acc += v.rate;
        while (v.acc >= out_rate) {
            v.acc -= out_rate;
            v.pos += v.blockAlign;
            if (v.pos + width > v.len) {
                if (v.loop) v.pos = 0;
                else return false;
            }
        }
    }
    return true;
}

}}  // namespace d2rt::dsound
