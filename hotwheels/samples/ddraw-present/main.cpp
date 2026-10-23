#include "present/vita_present.h"
#include <vitasdk.h>
#include <cstdio>
#include <cstdint>
extern "C" {
unsigned int sceUserMainThreadStackSize=2*1024*1024;
unsigned int _newlib_heap_size_user=16*1024*1024;
}
static uint8_t indices[640*480];
static uint32_t palette[256];
int main() {
    sceClibPrintf("[hw-present] start synthetic P8\n");
    hw::present::VitaPresenter p;
    if(!p.init(640,480)) {
        sceClibPrintf("[hw-present] init FAILED: %s\n",p.error());return 1;
    }
    sceClibPrintf("[hw-present] init ok: 640x480 P8, 8MiB RAM + 16MiB CDRAM\n");
    for(unsigned y=0;y<480;++y) for(unsigned x=0;x<640;++x)
        indices[y*640+x]=uint8_t(1+(x/80));
    const uint32_t colors[8]={0xFFFF0000u,0xFF00FF00u,0xFF0000FFu,0xFFFFFF00u,
                              0xFFFF00FFu,0xFF00FFFFu,0xFFFFFFFFu,0xFF804020u};
    for(unsigned i=0;i<256;++i) palette[i]=0xFF000000u;
    for(unsigned i=0;i<8;++i) palette[i+1]=colors[i];
    for(unsigned f=0;f<600;++f) {
        // Anima sólo la paleta: los índices quedan idénticos en memoria.
        palette[1]=(f/30)%2 ? 0xFF800000u : 0xFFFF0000u;
        p.present(indices,640,palette);
        if((f%120)==0) sceClibPrintf("[hw-present] frame %u palette1=%08x status=%s\n",f,palette[1],p.error());
    }
    sceClibPrintf("[hw-present] done frames=%u\n",unsigned(p.frames()));
    p.shutdown();
    sceKernelExitProcess(0);
    return 0;
}
