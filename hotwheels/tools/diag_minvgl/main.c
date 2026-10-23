// diag_minvgl — BUG-035 experimento (b): app mínima sin guest/winx86.
// vitaGL clear+swap a 60 Hz + latido (UDP opcional y archivo) durante 60 s.
// Si también se detiene a ~14.5 s del launch, la causa es el estado de la
// consola, no el runtime de Hot Wheels.
#include <vitaGL.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <string.h>
#ifndef HW_DIAG_LOG_IP
#define HW_DIAG_LOG_IP "192.0.2.20" /* TEST-NET example; override at compile time. */
#endif

unsigned int _newlib_heap_size_user = 32 * 1024 * 1024;
static int sock = -1; static SceNetSockaddrIn to;

static void out(const char* s, int n) {
    int fd = sceIoOpen("ux0:data/hotwheels/minvgl.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, s, n); sceIoClose(fd); }
    if (sock >= 0) sceNetSendto(sock, s, n, 0, (SceNetSockaddr*)&to, sizeof to);
}

static int beat(SceSize a, void* p) {
    (void)a; (void)p; char m[96];
    for (;;) {
        sceKernelDelayThread(500 * 1000);
        int n = snprintf(m, sizeof m, "minvgl t=%u\n", sceKernelGetProcessTimeLow() / 1000u);
        out(m, n);
    }
    return 0;
}

int main(void) {
    sceIoRemove("ux0:data/hotwheels/minvgl.txt");
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    static char net_mem[64 * 1024]; SceNetInitParam ip = {net_mem, sizeof net_mem, 0};
    sceNetInit(&ip);
    sock = sceNetSocket("minvgl", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    int nb = 1; sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nb, sizeof nb);
    to.sin_family = SCE_NET_AF_INET; to.sin_port = sceNetHtons(9099);
    sceNetInetPton(SCE_NET_AF_INET, HW_DIAG_LOG_IP, &to.sin_addr);
    SceUID th = sceKernelCreateThread("beat", beat, 64, 0x4000, 0, 0x20000, NULL);
    sceKernelStartThread(th, 0, NULL);
#ifdef NOGL
    // Variante sin GXM/vitaGL: si también se detiene, el sistema suspende
    // cualquier app (no es el GPU).
    while (sceKernelGetProcessTimeLow() < 60u * 1000000u) sceKernelDelayThread(100 * 1000);
#else
    vglInitWithCustomSizes(0, 960, 544, 8 * 1024 * 1024, 16 * 1024 * 1024, 0, 0, SCE_GXM_MULTISAMPLE_NONE);
    for (unsigned f = 0; sceKernelGetProcessTimeLow() < 60u * 1000000u; ++f) {
        glClearColor((f & 255) / 255.0f, 0.2f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
    }
#endif
    out("minvgl fin 60s\n", 15);
    sceKernelExitProcess(0);
    return 0;
}
