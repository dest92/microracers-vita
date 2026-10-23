#include "audio/output_contract.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
int main() {
    using namespace hw::audio;
    OutputBlocks blocks;
    int16_t a[128],b[128];
    for(int i=0;i<128;++i){a[i]=int16_t(i);b[i]=int16_t(1000+i);}
    auto* first=blocks.prepare(a,64,64);
    assert(first && reinterpret_cast<uintptr_t>(first)%64==0);
    auto* second=blocks.prepare(b,64,64);
    assert(second && second!=first && reinterpret_cast<uintptr_t>(second)%64==0);
    for(int i=0;i<128;++i)assert(first[i]==a[i] && second[i]==b[i]);
    auto* partial=blocks.prepare(a,2,64);
    assert(partial==first);
    for(int i=0;i<4;++i)assert(partial[i]==a[i]);
    for(int i=4;i<128;++i)assert(partial[i]==0);
    assert(!blocks.prepare(a,65,64));
    assert(!blocks.prepare(a,64,63));
    assert(!blocks.prepare(nullptr,64,64));
    assert(output_succeeded(0) && output_succeeded(512) && !output_succeeded(-1));

    // Costo que agrega el bloque alineado: una copia de 2048 B por grano. Antes
    // el sink entregaba el vector de origen sin copiar (costo 0). Medición de
    // HOST x86: no es el costo en A9 (ni en Vita), queda como cota superior de
    // referencia y como base para comparar cambios.
    {
        OutputBlocks blocks;
        int16_t src[512 * 2];
        for (int i = 0; i < 512 * 2; ++i) src[i] = int16_t(i);
        const int kIters = 20000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < kIters; ++k) blocks.prepare(src, 512, 512);
        const auto t1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters;
        std::printf("OUTPUT-CONTRACT: prepare+copia = %.0f ns por grano de 2048 B [host x86, no A9]\n", ns);
    }
    std::puts("AUDIO-OUTPUT-CONTRACT: PASS");
}
