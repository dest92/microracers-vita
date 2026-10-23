// HW-010 Vita3K smoke: synthetic D3D6 semantics through Ring -> Replayer -> vitaGL.
#include <vitasdk.h>
#include <vitaGL.h>
#include "scene.h"

using namespace hw::d3d;

int main() {
    sceClibPrintf("[d3d-ring] start\n");
    // vitaGL c707ff00 devuelve si hubo que reducir resolución, no un bool éxito.
    GLboolean fallback = vglInit(0x100000);
    sceClibPrintf("[d3d-ring] vglInit resolution_fallback=%u\n", unsigned(fallback));
    ring_scene::init_textures();
    Ring ring;
    Device d(ring, 160, 120, ring_scene::lookup);
    ring_scene::build(d);
    auto backend=make_vitagl_backend(960,544);
    Replayer replay(*backend); replay.play(ring);
    sceClibPrintf("[d3d-ring] scene replayed; press START to exit\n");
    uint32_t old=0;
    for (;;) { SceCtrlData pad{}; sceCtrlPeekBufferPositive(0,&pad,1);
        if ((pad.buttons&SCE_CTRL_START) && !(old&SCE_CTRL_START)) break;
        old=pad.buttons; sceKernelDelayThread(16000);
    }
    sceClibPrintf("[d3d-ring] bye\n"); sceKernelExitProcess(0); return 0;
}
