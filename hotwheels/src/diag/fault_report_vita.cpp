// src/diag/fault_report_vita.cpp — BUG-035: envuelve dyn86_fault_handle
// (winx86, vía -Wl,--wrap) para dejar constancia SÍNCRONA de todo fallo que
// winx86 declina. Un fallo declinado vuelve al kernel: el proceso se suspende
// para el coredump y ningún log asíncrono llega a escribirse.
#ifdef __vita__
#include <cstdint>
#include <cstdio>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

extern "C" {
typedef struct dyn86_fault {
    uint32_t type, pc, lr, sp, r0, r1, r2, far, fsr;
    uint32_t live[8];
    uint32_t x86insn, gaddr;
} dyn86_fault_t;
int __real_dyn86_fault_handle(dyn86_fault_t* f);
void hw_hb_udp_send(const char* s, unsigned n);  // runtime_vita.cpp (no-op sin HW_HB_UDP)

int __wrap_dyn86_fault_handle(dyn86_fault_t* f) {
    const int verdict = __real_dyn86_fault_handle(f);
    static volatile uint32_t reported = 0;
    if (verdict == 1 || reported >= 16) return verdict;  // 1 = resuelto (SMC, etc.)
    ++reported;
    char m[256];
    const int n = std::snprintf(m, sizeof m,
        "FAULT t=%u thr=0x%x verdict=%d type=%u pc=%08x lr=%08x sp=%08x far=%08x fsr=%08x r0=%08x r1=%08x r2=%08x x86=%08x g=%08x\n",
        sceKernelGetProcessTimeLow() / 1000u, (unsigned)sceKernelGetThreadId(), verdict,
        f->type, f->pc, f->lr, f->sp, f->far, f->fsr, f->r0, f->r1, f->r2, f->x86insn, f->gaddr);
    if (n <= 0) return verdict;
    hw_hb_udp_send(m, unsigned(n));
    const int fd = sceIoOpen("ux0:data/hotwheels/boot_probe.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, m, unsigned(n)); sceIoClose(fd); }
    return verdict;
}
}
#endif
