// sinc16_hoisted.h — HIV-073 (perf-host): prototipo diagnóstico del kernel sinc16
// del ds_emul. NO es código de producción: sirve para MEDIR el costo del
// bounds-check por tap del prototipo original. Misma semántica de mezcla; el
// camino rápido (taps interiores) no evalúa ramas por tap; el borde cae a la
// ruta segura idéntica a ds_resample.h.
//
// Diferencia medida con hotwheels/tools/a9/run_a9_budget.sh.
#pragma once
#include <cstdint>
#include <cstring>
#include "ds_coefficients.h"

namespace d2rt { namespace dsound {

struct ResampleVoice {
    const uint8_t* host; uint32_t len, pos, blockAlign, rate;
    uint64_t acc; int ch; uint32_t gL, gR; bool loop;
};

inline bool mix_voice(ResampleVoice& v, uint32_t out_rate, int frames, int32_t* out) {
    const uint32_t width = (v.ch == 2) ? 4u : 2u;
    if (!v.blockAlign || v.len < width || !out_rate) return false;
    if (v.pos % v.len + width > v.len) v.pos = 0; else v.pos %= v.len;
    const uint64_t inv = (1ull << 32) / out_rate;
    const uint32_t raw_bank = v.rate <= out_rate ? 0 : 32 - uint32_t(uint64_t(out_rate) * 32 / v.rate);
    const uint32_t hw_bank = raw_bank < 32 ? raw_bank : 31;
    auto rd = [&](uint32_t p, int k) { int16_t s; std::memcpy(&s, v.host + p + 2 * k, 2); return (int32_t)s; };
    for (int i = 0; i < frames; i++) {
        uint32_t np = v.pos + v.blockAlign;
        if (np + width > v.len) np = v.loop ? 0 : v.pos;
        const int64_t f = (int64_t)((v.acc * inv) >> 16);
        int32_t l0 = rd(v.pos, 0), r0 = (v.ch == 2) ? rd(v.pos, 1) : l0;
        {
            const uint32_t bank = hw_bank;
            const int16_t* weights = hw_ds_coeff[bank][uint32_t(f) >> 8];
            // El prototipo evalúa `p<0 || p+width>len` en cada uno de los 16 taps.
            // Acá se decide una vez: si la ventana pos-7..pos+8 cabe entera (caso
            // común salvo 15 frames en cada vuelta de una voz en loop), los 16
            // taps son contiguos y el acceso no necesita guarda.
            const uint32_t span = 7u * v.blockAlign;
            const bool interior = v.pos >= span && uint64_t(v.pos) + 8u * v.blockAlign + width <= v.len;
            auto filtered_fast = [&](int channel) -> int32_t {
                const uint8_t* base = v.host + (v.pos - span) + uint32_t(2 * channel);
                int64_t sum = 0;
                for (int tap = 0; tap < 16; ++tap)
                    sum += int64_t(*reinterpret_cast<const int16_t*>(base + uint32_t(tap) * v.blockAlign)) * weights[tap];
                return int32_t(sum >> 15);
            };
            auto filtered_safe = [&](int channel) -> int32_t {
                int64_t sum = 0;
                for (int tap = 0; tap < 16; ++tap) {
                    int64_t p = int64_t(v.pos) + int64_t(tap - 7) * v.blockAlign;
                    if (p < 0 || uint64_t(p) + width > v.len) {
                        if (v.loop) { p %= v.len; if (p < 0) p += v.len; }
                        else p = p < 0 ? 0 : v.len - v.blockAlign;
                    }
                    sum += int64_t(rd(uint32_t(p), channel)) * weights[tap];
                }
                return int32_t(sum >> 15);
            };
            if (f || bank) {
                if (interior) { l0 = filtered_fast(0); r0 = v.ch == 2 ? filtered_fast(1) : l0; }
                else          { l0 = filtered_safe(0); r0 = v.ch == 2 ? filtered_safe(1) : l0; }
            }
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
