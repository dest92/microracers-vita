#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdint.h>
#include <string.h>

int main(void) {
    static const char msg[] = "hello_vita3k: homebrew ejecutado desde vitasdk\n";

    SceUID fd = sceIoOpen("ux0:data/hello_vita3k.txt",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, msg, sizeof(msg) - 1);
        sceIoClose(fd);
    }

    sceClibPrintf("[hello_vita3k] escrito ux0:data/hello_vita3k.txt (fd=%d)\n", fd);

    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_IMMEDIATE) == 0 && fb.base != NULL) {
        for (unsigned int y = 0; y < fb.height; ++y) {
            uint32_t *row = (uint32_t *)((uint8_t *)fb.base + y * fb.pitch);
            for (unsigned int x = 0; x < fb.width; ++x)
                row[x] = 0xFF20C040u;
        }
        sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    }

    sceKernelDelayThread(4 * 1000 * 1000);
    sceKernelExitProcess(0);
    return 0;
}
