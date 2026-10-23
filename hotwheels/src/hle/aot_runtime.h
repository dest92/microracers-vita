// src/hle/aot_runtime.h — runtime del C generado por tools/aot_x86.py (HW-052).
// Semántica = box86: x87 en double IEEE (≡ x87 con PC=53 bits); flags x86
// exactos para los jcc que el código consume (cmp/test/testb/add/sub/logic).
#pragma once
#include <stdint.h>
#include <string.h>
#include "hle/span_ft3.h"

typedef struct {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t fsw;     // último status x87 producido (C0/C2/C3)
    int st_depth;     // profundidad x87 relativa a la entrada (debe ser 0 al salir)
} hle_aot_regs;

#define RD32(a)    aot_rd32(mb, (a))
#define WR32(a, v) aot_wr32(mb, (a), (v))
#define RDF(a)     aot_rdf(mb, (a))
#define WRF(a, v)  aot_wrf(mb, (a), (v))

static inline uint32_t aot_rd32(const uint8_t* mb, uint32_t a) { uint32_t v; memcpy(&v, mb + a, 4); return v; }
static inline void aot_wr32(uint8_t* mb, uint32_t a, uint32_t v) { memcpy(mb + a, &v, 4); }
static inline float aot_rdf(const uint8_t* mb, uint32_t a) { float v; memcpy(&v, mb + a, 4); return v; }
static inline void aot_wrf(uint8_t* mb, uint32_t a, float v) { memcpy(mb + a, &v, 4); }

enum { FK_ADD, FK_SUB, FK_LOGIC, FK_LOGIC8, FK_INC, FK_DEC, FK_SHIFT, FK_IMUL };

static inline int aot_zf(int k, uint32_t r) { return k == FK_LOGIC8 ? (r & 0xffu) == 0 : r == 0; }
static inline int aot_sf(int k, uint32_t r) { return k == FK_LOGIC8 ? (int)((r >> 7) & 1u) : (int)(r >> 31); }
static inline int aot_of(int k, uint32_t a, uint32_t b, uint32_t r) {
    switch (k) {
    case FK_ADD: return (int)((((a ^ r) & (b ^ r)) >> 31) & 1u);
    case FK_SUB: return (int)((((a ^ b) & (a ^ r)) >> 31) & 1u);
    case FK_INC: return r == 0x80000000u;
    case FK_DEC: return r == 0x7fffffffu;
    default:     return 0;   // lógicas: OF=0; shifts/imul no alimentan jcc aquí (verificado al generar)
    }
}
#define AOT_SF_NE_OF (aot_sf(k, r) != aot_of(k, a, b, r))
static inline int aot_cond_je(int k, uint32_t a, uint32_t b, uint32_t r)  { (void)a; (void)b; return aot_zf(k, r); }
static inline int aot_cond_jne(int k, uint32_t a, uint32_t b, uint32_t r) { (void)a; (void)b; return !aot_zf(k, r); }
static inline int aot_cond_jl(int k, uint32_t a, uint32_t b, uint32_t r)  { return AOT_SF_NE_OF; }
static inline int aot_cond_jge(int k, uint32_t a, uint32_t b, uint32_t r) { return !AOT_SF_NE_OF; }
static inline int aot_cond_jle(int k, uint32_t a, uint32_t b, uint32_t r) { return aot_zf(k, r) || AOT_SF_NE_OF; }
static inline int aot_cond_jg(int k, uint32_t a, uint32_t b, uint32_t r)  { return !aot_zf(k, r) && !AOT_SF_NE_OF; }

// FCOM: C3 C2 C0 = 000 (>), 001 (<), 100 (=), 111 (no ordenado). Bits del SW:
// C0=0x0100, C2=0x0400, C3=0x4000.
static inline uint16_t aot_fcom(double a, double b, int sp) {
    (void)sp;
    if (a != a || b != b) return 0x4500;
    if (a < b) return 0x0100;
    if (a == b) return 0x4000;
    return 0;
}
// FNSTSW AX: C-bits + TOP (bits 11-13). TOP = (8 - profundidad) & 7 (entrada vacía).
static inline uint32_t aot_fsw_top(uint16_t fsw, int sp) {
    return (uint32_t)((fsw & ~0x3800u) | ((uint32_t)((8 - sp) & 7) << 11));
}
// __ftol (0x4521c4) == FISTP m64 con truncado (arm_fist64_3): NaN/±inf/fuera de
// rango → 0x8000000000000000 (integer indefinite).
static inline int64_t aot_ftol(double d) {
    if (!(d > -9223372036854775808.0 && d < 9223372036854775808.0)) return (int64_t)0x8000000000000000LL;
    return (int64_t)d;  // C trunca hacia cero
}
// 0x450a40(a, b): [0x4679cc] = (a>>16)&0xff; [0x4679c8] = rol(a,16) con low16 = (b>>8)&0xffff.
// EAX = ese valor; EBX lo salva/restaura el callee.
static inline void aot_span_setup(uint8_t* mb, uint32_t a, uint32_t b, uint32_t* eax, uint32_t* ebx) {
    (void)ebx;
    uint32_t x = (a << 16) | (a >> 16);
    mb[HLE_SPAN_USTEP_VA] = (uint8_t)x;
    x = (x & 0xffff0000u) | ((b >> 8) & 0xffffu);
    aot_wr32(mb, HLE_SPAN_STEP_VA, x);
    *eax = x;
}
