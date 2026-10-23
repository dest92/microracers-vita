// src/runtime/hw_intrin.cpp — ver hw_intrin.h.
#include "runtime/hw_intrin.h"
#include "dyn86_intrin.h"
#include "hle/span_ft3.h"
#include <cstdio>
#include <cstdlib>

extern "C" uintptr_t dyn86_membase;
extern "C" uint32_t hw_emu_reg(void* emu, int r);
extern "C" void hw_emu_set_reg(void* emu, int r, uint32_t v);
extern "C" int hw_hle_ftol(void* emu, uint32_t esp);  // src/hle/emu_glue.c
extern "C" int hw_hle_raster_ft3(void* emu, uint32_t esp);       // emu_glue.c (AOT)
extern "C" int hw_hle_raster_ft3_semi(void* emu, uint32_t esp);  // emu_glue.c (AOT)
extern "C" int hw_ftol_check_mode;
extern "C" int dyn86_jitprof;
extern "C" uint64_t dyn86_jp_lookup_calls, dyn86_jp_lookup_hits, dyn86_jp_lookup_miss, dyn86_jp_recompiles;
extern "C" uint64_t dyn86_jp_revalidations __attribute__((weak));
extern "C" struct { unsigned long long calls, checked, match, mismatch; uint32_t esp; int64_t want; int pending;
                    uint32_t ebp; uint16_t cw; unsigned long long clobbered; } hw_ftol_stats;

namespace hw::intrin {

int parse_mode(const char* env) {
    if (!env || !env[0] || env[1]) return 0;
    return (env[0] >= '0' && env[0] <= '2') ? env[0] - '0' : 0;
}

namespace {
uint32_t g_span = 0;
std::function<void(const std::string&)> g_log;
void maybe_report();
dyn86_intrin_t* g_span_entry = nullptr;
dyn86_intrin_t* g_semi_entry = nullptr;
enum { kAX = 0, kCX = 1, kDX = 2 };  // regs.h: _AX, _CX, _DX

// spanFT3 0x450af3: cdecl(dst, u, v, n, tex); lee EAX (bytes altos que la cola
// <4 px conserva); deja EAX/ECX/EDX como el guest (EBX/ESI/EDI/EBP restaurados).
int intrin_span_ft3(void* emu, uint32_t esp) {
    if (g_span_entry && (++g_span_entry->calls & ((1ull << 22) - 1)) == 0) maybe_report();
    if (dyn86_intrin_on == 2 || !dyn86_membase || !g_span) return 0;
    uint8_t* mb = reinterpret_cast<uint8_t*>(dyn86_membase);
    if (esp > g_span - 24) return 0;
    uint32_t a[6];
    for (int i = 0; i < 6; ++i) a[i] = *reinterpret_cast<const uint32_t*>(mb + esp + 4u * i);
    if (!span_ft3_in_bounds(a[1], a[4], a[5], g_span)) return 0;
    hle_span_regs r;
    hle_span_ft3(mb, a[1], a[2], a[3], a[4], a[5], hw_emu_reg(emu, kAX), &r);
    hw_emu_set_reg(emu, kAX, r.eax); hw_emu_set_reg(emu, kCX, r.ecx); hw_emu_set_reg(emu, kDX, r.edx);
    if (g_span_entry) ++g_span_entry->served;
    return 1;
}
// spanFT3Semi 0x450c19: cdecl(dst, u, v, n, tex, blend); no lee registros.
int intrin_span_ft3_semi(void* emu, uint32_t esp) {
    if (g_semi_entry) ++g_semi_entry->calls;
    if (dyn86_intrin_on == 2 || !dyn86_membase || !g_span) return 0;
    uint8_t* mb = reinterpret_cast<uint8_t*>(dyn86_membase);
    if (esp > g_span - 28) return 0;
    uint32_t a[7];
    for (int i = 0; i < 7; ++i) a[i] = *reinterpret_cast<const uint32_t*>(mb + esp + 4u * i);
    if (!span_ft3_in_bounds(a[1], a[4], a[5], g_span)) return 0;
    if (!span_ft3_in_bounds(a[1], a[4], a[6], g_span)) return 0;   // página de la tabla
    hle_span_regs r;
    hle_span_ft3_semi(mb, a[1], a[2], a[3], a[4], a[5], a[6], &r);
    hw_emu_set_reg(emu, kAX, r.eax); hw_emu_set_reg(emu, kCX, r.ecx); hw_emu_set_reg(emu, kDX, r.edx);
    if (g_semi_entry) ++g_semi_entry->served;
    return 1;
}
// Diagnóstico (HW_DIAG_SKIP_FT3=1): swRasterFT3/Semi como no-op → cota superior
// (Amdahl) de lo que puede ganar un HLE a nivel triángulo. Rompe el render.
int intrin_skip(void*, uint32_t) { return 1; }
}  // namespace

bool span_ft3_in_bounds(uint32_t dst, uint32_t n, uint32_t tex, uint32_t span) {
    const uint32_t page = tex & 0xffff0000u;
    if (page > span || span - page < 0x10000u) return false;
    if (dst > span || n > dst) return false;
    if (n > 4096) return false;  // ancho máximo razonable (800 px); más = no seguro
    return true;
}

int mode_from_env() {
    if (const char* e = std::getenv("HW_INTRIN")) return parse_mode(e);
    return parse_mode(std::getenv("HW_HLE_RASTER"));
}

constexpr uint16_t kAll7 = DYN86_IR_AX | DYN86_IR_CX | DYN86_IR_DX | DYN86_IR_BX | DYN86_IR_BP | DYN86_IR_SI | DYN86_IR_DI;

const Target* targets(unsigned* n) {
    // Cada entrada exige: prueba de captura (tools/verif_hle_capture.py),
    // test diferencial (tests/test_hle_*.py) y regmask/inmask del desensamblado.
    static const Target kTargets[] = {
        {HLE_SPAN_FT3_VA, intrin_span_ft3, 0, DYN86_IR_AX, DYN86_IR_AX | DYN86_IR_CX | DYN86_IR_DX, "spanFT3"},
        {HLE_SPAN_FT3_SEMI_VA, intrin_span_ft3_semi, 0, 0, DYN86_IR_AX | DYN86_IR_CX | DYN86_IR_DX, "spanFT3Semi"},
        // __ftol (CRT): 401 call directos, captura completa; perfil fiel carrera ≈12 %.
        // Triángulo completo (AOT x86→C, diferencial tests/test_aot_raster.py).
        {0x0041d5a0u, hw_hle_raster_ft3, 0, kAll7, DYN86_IR_AX | DYN86_IR_CX | DYN86_IR_DX, "rasterFT3"},
        {0x00420d30u, hw_hle_raster_ft3_semi, 0, kAll7, DYN86_IR_AX | DYN86_IR_CX | DYN86_IR_DX, "rasterFT3Semi"},
        {0x004521c4u, hw_hle_ftol, 0, DYN86_IR_BP /* sólo modo sombra */, DYN86_IR_AX | DYN86_IR_DX, "ftol"},
    };
    *n = sizeof kTargets / sizeof kTargets[0];
    return kTargets;
}

namespace {
// Host (qemu) no tiene fps-window: el helper publica el reporte cada 2^22
// llamadas a spanFT3 (segundos de carrera SW).
void maybe_report() { if (g_log) { const std::string r = report(); if (!r.empty()) g_log(r); } }
}  // namespace

extern "C" void hw_intrin_tick_report(void) { maybe_report(); }

int install(int mode, uint32_t arena_span, const std::function<void(const std::string&)>& log) {
    g_span = arena_span;
    g_log = log;
    hw_ftol_check_mode = std::getenv("HW_FTOL_CHECK") && *std::getenv("HW_FTOL_CHECK") == '1';
    unsigned n = 0;
    const Target* t = targets(&n);
    dyn86_intrin_on = mode;
    if (!mode) return 0;
    int added = 0;
    // HW_HLE_TARGETS=ftol,spanFT3,...: registra sólo esos (A/B por objetivo).
    const char* only = std::getenv("HW_HLE_TARGETS");
    const std::string sel = only ? std::string(",") + only + "," : "";
    for (unsigned i = 0; i < n; ++i) {
        if (only && sel.find(std::string(",") + t[i].name + ",") == std::string::npos) continue;
        // ftol como HLE es +2 % más lento (A/B qemu): sólo para validar (HW_FTOL_CHECK),
        // nunca por defecto. La ganancia real está en winx86-0008 (FISTP inline).
        if (!only && std::string(t[i].name) == "ftol") continue;
        if (dyn86_intrin_add(t[i].va, t[i].fn, t[i].retn, t[i].inmask, t[i].regmask, t[i].name)) ++added;
    }
    if (const char* e = std::getenv("HW_DIAG_SKIP_FT3"); e && *e == '1') {
        dyn86_intrin_add(0x0041d5a0u, intrin_skip, 0, 0, 0, "skipFT3");
        dyn86_intrin_add(0x00420d30u, intrin_skip, 0, 0, 0, "skipFT3Semi");
        log("intrin: DIAG swRasterFT3/Semi como no-op (cota superior, render roto)");
    }
    for (int i = 0; i < dyn86_intrin_n; ++i)
        if (dyn86_intrin_tbl[i].va == HLE_SPAN_FT3_VA) g_span_entry = &dyn86_intrin_tbl[i];
        else if (dyn86_intrin_tbl[i].va == HLE_SPAN_FT3_SEMI_VA) g_semi_entry = &dyn86_intrin_tbl[i];
    log("intrin: modo " + std::to_string(mode) + ", objetivos " + std::to_string(added) + "/" + std::to_string(n));
    return added;
}

std::string report() {
    if (dyn86_jitprof) {  // WX86_JITPROFILE=1: contadores del dispatcher (dynablock.c)
        char j[200];
        std::snprintf(j, sizeof j, "jitprof: lookups=%llu hits=%llu miss=%llu recompiles=%llu revalidations=%llu",
                      (unsigned long long)dyn86_jp_lookup_calls, (unsigned long long)dyn86_jp_lookup_hits,
                      (unsigned long long)dyn86_jp_lookup_miss, (unsigned long long)dyn86_jp_recompiles,
                      (unsigned long long)((&dyn86_jp_revalidations) ? dyn86_jp_revalidations : 0));
        if (!dyn86_intrin_on) return j;
        return std::string(j) + " | " + report_intrin();
    }
    return report_intrin();
}

std::string report_intrin() {
    if (!dyn86_intrin_on) return {};
    char b[256];
    const int n = dyn86_intrin_report(b, sizeof b);
    std::string s = n > 0 ? std::string(b, size_t(n)) : std::string();
    if (hw_ftol_check_mode) {
        char c[160];
        std::snprintf(c, sizeof c, " | ftol_check calls=%llu checked=%llu match=%llu mismatch=%llu clobbered=%llu",
                      hw_ftol_stats.calls, hw_ftol_stats.checked, hw_ftol_stats.match, hw_ftol_stats.mismatch, hw_ftol_stats.clobbered);
        s += c;
    }
    return s;
}

}  // namespace hw::intrin
