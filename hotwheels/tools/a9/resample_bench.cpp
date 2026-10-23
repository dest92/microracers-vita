// resample_bench.cpp — HW-073 (perf-host): mide el costo del mezclado/resampleo
// por voz del ds_emul. Se compila contra UN kernel a la vez eligiendo el -I del
// variant (mismo ABI: ResampleVoice + mix_voice). Determinista: el checksum sólo
// valida que el kernel corre; el objetivo es contar instrucciones con qemu-arm.
//
// Uso: resample_bench <frames>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ds_resample.h"

using namespace d2rt::dsound;

int main(int argc, char** argv) {
    int frames = argc > 1 ? std::atoi(argv[1]) : 480;
    const int SRCLEN = 8192;                 // muestras (frames estéreo) de fuente
    static int16_t src[2 * SRCLEN];
    for (int i = 0; i < 2 * SRCLEN; ++i) src[i] = int16_t((i * 2654435761u) >> 16);
    static int32_t out[2 * 8192];
    std::memset(out, 0, sizeof out);

    ResampleVoice v;
    v.host = reinterpret_cast<const uint8_t*>(src);
    v.len = uint32_t(2 * SRCLEN);            // bytes (PCM16 estéreo)
    v.pos = 0;
    v.blockAlign = 4;                        // 2 canales × 2 bytes
    v.rate = 32342;                          // fuente real: SetFrequency típico
    v.acc = 0;
    v.ch = 2;
    v.gL = 1u << 16;                         // ganancia 1.0
    v.gR = 1u << 16;
    v.loop = true;                           // voz de motor/música: nunca termina

    int done = 0;
    int64_t sum = 0;
    while (done < frames) {
        int chunk = frames - done;
        if (chunk > 1024) chunk = 1024;
        if (!mix_voice(v, 48000, chunk, out)) break;
        for (int i = 0; i < 2 * chunk; ++i) sum += out[i];
        done += chunk;
    }
    // volatile para que el loop no se elimine; el valor no importa.
    volatile int64_t sink = sum;
    std::printf("frames=%d checksum=%lld\n", done, (long long)sink);
    return 0;
}
