// src/hle/emu_glue.c — acceso a registros del x86emu de box86 para helpers HLE
// (HW-052). Aislado acá: es la única unidad que incluye internos de box86
// (build_vita.sh le agrega sus -I sólo a este archivo).
#include <stdint.h>
#include "regs.h"
#include "emu/x86emu_private.h"
#include "hle/aot_runtime.h"

uint32_t hw_emu_reg(void* emu, int r) { return ((x86emu_t*)emu)->regs[r].dword[0]; }
void hw_emu_set_reg(void* emu, int r, uint32_t v) { ((x86emu_t*)emu)->regs[r].dword[0] = v; }

// ---------------------------------------------------------------------------
// HLE de __ftol (CRT MSVC, HotWheels.exe 0x4521c4; perfil fiel HW-051 carrera:
// __ftol 6 % + arm_fist64_3 6 % del hilo guest). Cuerpo guest: fnstcw; RC=trunc
// con fldcw; fistp qword; fldcw restaura; EDX:EAX = resultado; leave; ret.
// Efectos observables reproducidos: EDX:EAX = FISTP m64 truncado, pop de ST0,
// control word intacto. (Scratch bajo el ESP final y flags de `or ah,0xc` no
// son observables para un llamador compilado.)
// Al inicio del bloque la caché x87 de box86 ya está en emu: el `call` emite
// BARRIER(BARRIER_FLOAT) → fpu_purgecache (dynarec_arm_00.c, 0xE8).
#include "emu/x87emu_private.h"
#include <string.h>
extern int dyn86_intrin_on;
extern uintptr_t dyn86_membase;
int64_t arm_fist64_3(double d);

typedef struct { unsigned long long calls, checked, match, mismatch; uint32_t esp; int64_t want; int pending;
                 uint32_t ebp; uint16_t cw; unsigned long long clobbered; } hw_ftol_stats_t;
hw_ftol_stats_t hw_ftol_stats;
void hw_intrin_tick_report(void);  // hw_intrin.cpp (publica el reporte)
int hw_ftol_check_mode;  // HW_FTOL_CHECK=1: sombra (calcula, no sirve; verifica a la entrada siguiente)

static int64_t ftol_native(x86emu_t* e) {
    x86emu_t* emu = e;  // las macros ST0/STll usan `emu`
    // Igual que FISTP m64 traducido (dynarec_arm_df.c): valor de FILD exacto si coincide sref.
    if (ST0.q == STll(0).sref) return STll(0).sq;
    return arm_fist64_3(ST0.d);
}

int hw_hle_ftol(void* emu_v, uint32_t esp) {
    x86emu_t* emu = (x86emu_t*)emu_v;
    if ((++hw_ftol_stats.calls & ((1ull << 22) - 1)) == 0) hw_intrin_tick_report();
    if (dyn86_intrin_on == 2) return 0;
    if (hw_ftol_check_mode) {
        if (hw_ftol_stats.pending) {  // resultado que dejó el fistp guest de la llamada anterior
            const uint8_t* g = (const uint8_t*)(dyn86_membase + hw_ftol_stats.esp - 16u);
            int64_t got; memcpy(&got, g, 8);
            uint16_t cwt, cw0; uint32_t ebp0;
            memcpy(&cwt, g + 8, 2); memcpy(&cw0, g + 10, 2); memcpy(&ebp0, g + 12, 4);
            const int intact = ebp0 == hw_ftol_stats.ebp && cw0 == hw_ftol_stats.cw && cwt == (uint16_t)(hw_ftol_stats.cw | 0x0c00);
            if (!intact) ++hw_ftol_stats.clobbered;  // scratch pisado: comparación no válida
            else {
                ++hw_ftol_stats.checked;
                if (got == hw_ftol_stats.want) ++hw_ftol_stats.match; else ++hw_ftol_stats.mismatch;
            }
        }
        hw_ftol_stats.want = ftol_native(emu);
        hw_ftol_stats.esp = esp;
        hw_ftol_stats.ebp = emu->regs[_BP].dword[0];
        hw_ftol_stats.cw = emu->cw.x16;
        hw_ftol_stats.pending = 1;
        return 0;  // el guest ejecuta su __ftol
    }
    const int64_t r = ftol_native(emu);
    fpu_do_pop(emu);
    emu->regs[_AX].dword[0] = (uint32_t)(uint64_t)r;
    emu->regs[_DX].dword[0] = (uint32_t)((uint64_t)r >> 32);
    return 1;
}

// ---------------------------------------------------------------------------
// HW-052: swRasterFT3 (0x41d5a0) / swRasterFT3Semi (0x420d30) a nivel triángulo,
// traducidos AOT x86→C por tools/aot_x86.py (src/hle/aot_raster.c, generado).
// Se incluye aquí (unity build) para no tocar tools/build_* (reservados).
// Diferencial bit a bit contra el x86 original: tests/test_aot_raster.py.
// Cota de Amdahl medida (qemu, ab_cpu_frame + HW_DIAG_SKIP_FT3): ≈19 %.
// aot_raster.c es una TRADUCCIÓN del código del juego: no se commitea (AGENTS.md:
// nada con copyright del juego). Se genera localmente con tools/gen_aot_raster.sh
// desde el HotWheels.exe del usuario; sin él, los objetivos caen al guest.
#if __has_include("hle/aot_raster.c")
#include "hle/aot_raster.c"
#define HW_HAVE_AOT_RASTER 1
#else
#define HW_HAVE_AOT_RASTER 0
#endif

static int hle_raster(x86emu_t* emu, uint32_t esp, int semi) {
    if (dyn86_intrin_on == 2) return 0;
    hle_aot_regs r;
    r.eax = emu->regs[_AX].dword[0]; r.ecx = emu->regs[_CX].dword[0]; r.edx = emu->regs[_DX].dword[0];
    r.ebx = emu->regs[_BX].dword[0]; r.ebp = emu->regs[_BP].dword[0]; r.esi = emu->regs[_SI].dword[0];
    r.edi = emu->regs[_DI].dword[0]; r.esp = esp; r.fsw = 0; r.st_depth = 0;
#if HW_HAVE_AOT_RASTER
    uint8_t* mb = (uint8_t*)dyn86_membase;
    if (semi) aot_swRasterFT3Semi(mb, &r); else aot_swRasterFT3(mb, &r);
#else
    (void)semi; return 0;  // sin aot_raster.c generado: fallback fiel al guest
#endif
    // El cuerpo restaura EBX/ESI/EDI/EBP (regmask = AX|CX|DX); ESP queda en el
    // valor de entrada para que el RET emitido consuma la dirección de retorno.
    emu->regs[_AX].dword[0] = r.eax; emu->regs[_CX].dword[0] = r.ecx; emu->regs[_DX].dword[0] = r.edx;
    if (r.fsw) emu->sw.x16 = (uint16_t)((emu->sw.x16 & ~0x4700u) | (r.fsw & 0x4700u));
    return 1;
}
int hw_hle_raster_ft3(void* emu, uint32_t esp) { return hle_raster((x86emu_t*)emu, esp, 0); }
int hw_hle_raster_ft3_semi(void* emu, uint32_t esp) { return hle_raster((x86emu_t*)emu, esp, 1); }
