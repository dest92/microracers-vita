// samples/audio-engine — fixture Vita3K/consola de Engine::start (HW-018) en
// modo Vita: aísla el crash "engine: start ..." sin "ok" (run002952) del juego.
// Sin datos del juego: alimenta un seno 440 Hz por el DsoundBus 5 s.
#include "audio/engine.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>

#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    sceIoMkdir("ux0:data/hotwheels", 0777);
    if (FILE* f = std::fopen("ux0:data/hotwheels/audio.log", "w")) { std::fputs("fixture\n", f); std::fclose(f); }
    sceClibPrintf("AUDIO-ENGINE: log creado\n");
    sceClibPrintf("AUDIO-ENGINE: start ...\n");
    auto* e = new hw::audio::Engine();
    hw::audio::EngineConfig cfg;
    cfg.vita = true;
    cfg.grain = 512;
    const bool ok = e->start(cfg);
    sceClibPrintf("AUDIO-ENGINE: start %s\n", ok ? "ok" : "FALLO");
    if (ok) {
        std::vector<int16_t> pcm(512 * 2);
        double ph = 0;
        for (int i = 0; i < 5 * 22050 / 512; ++i) {
            for (int k = 0; k < 512; ++k) {
                const int16_t s = (int16_t)(8000 * std::sin(ph)); ph += 2 * M_PI * 440 / 22050;
                pcm[2 * k] = pcm[2 * k + 1] = s;
            }
            e->dsound().push(pcm.data(), 512, 22050);
            sceKernelDelayThread(512 * 1000000 / 22050);
        }
        sceClibPrintf("AUDIO-ENGINE: feed ok underruns=%llu\n", (unsigned long long)e->underruns());
        e->stop();
        sceClibPrintf("AUDIO-ENGINE: stop ok\n");
    }
    sceClibPrintf("AUDIO-ENGINE: FIN\n");
    sceKernelExitProcess(0);
    return 0;
}
