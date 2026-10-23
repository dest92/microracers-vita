// Perfil por PC host para qemu-arm. Timer de CPU thread-directed; el handler
// sólo captura PC. Dynablock/EIP/Ghidra se resuelven al finalizar.
#if defined(__linux__) && !defined(__vita__)
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#include <ucontext.h>
#include "dynablock.h"
#include "dynarec/dynablock_private.h"

#define PCPROF_MAX (1u << 20)
#define PCPROF_MAP_MAX 4096
static uintptr_t g_pcs[PCPROF_MAX];
static volatile uint32_t g_n;
static volatile sig_atomic_t g_on;
static uint64_t g_t0_ms, g_start_ms, g_dur_ms;
static timer_t g_timer;
static int g_timer_created;
static char g_scene[24] = "unknown";

typedef struct { uint32_t start, end, entry; char name[128]; } symbol_t;
typedef struct { uintptr_t key; uint32_t n; } count_t;
static symbol_t g_symbols[PCPROF_MAP_MAX];
static uint32_t g_symbol_n;

/* SPEC-053 (HW-072): instrucciones ARM ejecutadas del guest, acumuladas por el
 * dynarec en el embudo C (parche winx86-0015; requiere WX86_CHAIN=0). Es
 * independiente de HW_PC_PROFILE y vive acá porque este módulo tiene los dos
 * llamados incondicionales del host (hw_pcprof_start/report). Débiles: con un
 * winx86 sin el parche no se arma nada y el enlace sigue. */
extern int dyn86_blkprof __attribute__((weak));
extern uint64_t dyn86_blkprof_insns(void) __attribute__((weak));
extern void dyn86_blkprof_enable(int) __attribute__((weak));
static int g_blkprof;

static void blkprof_start(void) {
    const char* e = getenv("HW_BLK_INSNS");
    if (!e || *e != '1') return;
    if (!dyn86_blkprof_enable || !dyn86_blkprof_insns) return;  // winx86 sin el parche
    dyn86_blkprof_enable(1);
    g_blkprof = 1;
}

static void blkprof_report(void (*logline)(const char*)) {
    if (!g_blkprof) return;
    char b[96];
    snprintf(b, sizeof b, "blk-insns: total=%llu",
             (unsigned long long)(&dyn86_blkprof ? dyn86_blkprof_insns() : 0));
    logline(b);
}

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void load_map(const char* path) {
    if (!path || !*path) return;
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (g_symbol_n < PCPROF_MAP_MAX && fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* p = line; char* endp;
        unsigned long start = strtoul(p, &endp, 0); if (endp == p || *endp != ',') continue; p = endp + 1;
        unsigned long end = strtoul(p, &endp, 0); if (endp == p || *endp != ',') continue; p = endp + 1;
        unsigned long entry = strtoul(p, &endp, 0); if (endp == p || *endp != ',') continue; p = endp + 1;
        p[strcspn(p, "\r\n")] = 0;
        if (!*p || start >= end || end > UINT32_MAX || entry > UINT32_MAX) continue;
        symbol_t* s = &g_symbols[g_symbol_n++];
        s->start = (uint32_t)start; s->end = (uint32_t)end; s->entry = (uint32_t)entry;
        snprintf(s->name, sizeof s->name, "%s", p);
    }
    fclose(f);
}

static void on_prof(int sig, siginfo_t* si, void* ctx) {
    (void)sig; (void)si;
    if (!g_on) return;
    uint64_t elapsed = mono_ms() - g_t0_ms;
    if (elapsed < g_start_ms || (g_dur_ms && elapsed >= g_start_ms + g_dur_ms)) return;
    uint32_t i = __atomic_fetch_add(&g_n, 1u, __ATOMIC_RELAXED);
    if (i < PCPROF_MAX)
        __atomic_store_n(&g_pcs[i], (uintptr_t)((ucontext_t*)ctx)->uc_mcontext.arm_pc, __ATOMIC_RELAXED);
}

int hw_pcprof_start(void) {
    blkprof_start();
    const char* e = getenv("HW_PC_PROFILE");
    if (!e || *e != '1') return 0;
    const char* scene = getenv("HW_PC_PROFILE_SCENE");
    if (scene && (!strcmp(scene, "menu") || !strcmp(scene, "race"))) snprintf(g_scene, sizeof g_scene, "%s", scene);
    const char* start = getenv("HW_PC_PROFILE_START_MS");
    const char* duration = getenv("HW_PC_PROFILE_DURATION_MS");
    g_start_ms = start ? strtoull(start, 0, 10) : 0;
    g_dur_ms = duration ? strtoull(duration, 0, 10) : 0;
    g_n = 0; g_symbol_n = 0; g_t0_ms = mono_ms();
    load_map(getenv("HW_PC_PROFILE_MAP") ? getenv("HW_PC_PROFILE_MAP") : getenv("HW_GUEST_PROFILE_MAP"));
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, 0) != 0) return 0;
    struct sigevent ev; memset(&ev, 0, sizeof ev);
    ev.sigev_notify = SIGEV_THREAD_ID;
    ev.sigev_signo = SIGPROF;
    ev._sigev_un._tid = (pid_t)syscall(SYS_gettid);
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &g_timer) != 0) return 0;
    g_timer_created = 1;
    struct itimerspec it = {{0, 1000000}, {0, 1000000}};
    g_on = 1;
    if (timer_settime(g_timer, 0, &it, 0) != 0) {
        g_on = 0; timer_delete(g_timer); g_timer_created = 0; return 0;
    }
    return 1;
}

static uint32_t x86_of(dynablock_t* db, uintptr_t pc) {
    uintptr_t nat = (uintptr_t)db->block, x86 = (uintptr_t)db->x86_addr;
    if (db->instsize)
        for (int i = 0; i < db->isize; ++i) {
            uintptr_t next = nat + db->instsize[i].nat * 4u;
            if (pc < next) break;
            nat = next; x86 += db->instsize[i].x86;
        }
    return (uint32_t)x86;
}

static uint32_t function_key(uint32_t eip, const char** name) {
    const symbol_t* best = NULL;
    for (uint32_t i = 0; i < g_symbol_n; ++i) {
        const symbol_t* s = &g_symbols[i];
        if (s->start <= eip && eip < s->end && (!best || s->end - s->start < best->end - best->start)) best = s;
    }
    if (best) { *name = best->name; return best->entry; }
    *name = NULL;
    return eip & ~0xfffu;
}

static int by_key(const void* a, const void* b) {
    const count_t* x = a; const count_t* y = b;
    return (x->key > y->key) - (x->key < y->key);
}
static int by_count(const void* a, const void* b) {
    const count_t* x = a; const count_t* y = b;
    return (y->n > x->n) - (y->n < x->n);
}
static uint32_t compact(count_t* rows, uint32_t n) {
    if (!n) return 0;
    qsort(rows, n, sizeof *rows, by_key);
    uint32_t out = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (out && rows[out - 1].key == rows[i].key) rows[out - 1].n += rows[i].n;
        else rows[out++] = rows[i];
    }
    qsort(rows, out, sizeof *rows, by_count);
    return out;
}

// Report game functions and every unique native PC; pcprof_report.py resolves
// host PCs against this exact ELF and assigns helper/shim/render/audio groups.
uint32_t hw_pcprof_report(void (*logline)(const char*)) {
    blkprof_report(logline);
    g_on = 0;
    if (g_timer_created) { timer_delete(g_timer); g_timer_created = 0; }
    uint32_t n = g_n < PCPROF_MAX ? g_n : PCPROF_MAX;
    if (!n) return 0;
    count_t* game = calloc(n, sizeof *game), *host = calloc(n, sizeof *host);
    if (!game || !host) { free(game); free(host); return 0; }
    uint32_t ng = 0, nh = 0, read = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uintptr_t pc = __atomic_load_n(&g_pcs[i], __ATOMIC_RELAXED);
        if (!pc) continue;
        ++read;
        dynablock_t* db = FindDynablockFromNativeAddress((void*)pc);
        if (db && db->x86_addr) {
            uint32_t eip = x86_of(db, pc); const char* ignored;
            game[ng++] = (count_t){function_key(eip, &ignored), 1};
        } else host[nh++] = (count_t){pc, 1};
    }
    ng = compact(game, ng); nh = compact(host, nh);
    char b[256];
    snprintf(b, sizeof b, "pcprof: scene=%s total=%u translated=%u native=%u map_functions=%u window_start_ms=%llu duration_ms=%llu",
        g_scene, read, ({ uint32_t sum=0; for (uint32_t i=0;i<ng;++i) sum+=game[i].n; sum; }),
        ({ uint32_t sum=0; for (uint32_t i=0;i<nh;++i) sum+=host[i].n; sum; }), g_symbol_n,
        (unsigned long long)g_start_ms, (unsigned long long)g_dur_ms);
    logline(b);
    for (uint32_t i = 0; i < ng && i < 30; ++i) {
        const char* name = NULL; uint32_t key = (uint32_t)game[i].key;
        function_key(key, &name);
        if (!name) { snprintf(b, sizeof b, "sub_%08X", key); name = b + 0; }
        char line[512];
        snprintf(line, sizeof line, "pcprof-game scene=%s rank=%u eip=0x%08x name=%s samples=%u pct=%.2f",
            g_scene, i + 1, key, name, game[i].n, 100.0 * game[i].n / read);
        logline(line);
    }
    for (uint32_t i = 0; i < nh; ++i) {
        snprintf(b, sizeof b, "pcprof-host scene=%s pc=0x%08x samples=%u pct=%.2f",
            g_scene, (unsigned)host[i].key, host[i].n, 100.0 * host[i].n / read);
        logline(b);
    }
    free(game); free(host);
    return read;
}
#else
int hw_pcprof_start(void) { return 0; }
unsigned hw_pcprof_report(void (*logline)(const char*)) { (void)logline; return 0; }
#endif
