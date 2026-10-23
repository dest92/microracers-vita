// src/hle/span_ft3.c — ver span_ft3.h. Transcripción instrucción por
// instrucción de 0x450af3..0x450b82; los comentarios citan la VA.
#include "hle/span_ft3.h"
#include <string.h>

static inline uint32_t rd32(const uint8_t* mb, uint32_t va) { uint32_t v; memcpy(&v, mb + va, 4); return v; }
static inline void wr32(uint8_t* mb, uint32_t va, uint32_t v) { memcpy(mb + va, &v, 4); }
// jl/jge tras sub/add: SF != OF.
static inline int lt_after_sub(uint32_t a, uint32_t b) { return (int32_t)a < (int32_t)b; }
static inline int lt_after_add(uint32_t a, uint32_t b) {
    const uint32_t r = a + b;
    const int sf = (int32_t)r < 0, of = (int)((~(a ^ b) & (a ^ r)) >> 31);
    return sf != of;
}

static void span_exact(uint8_t* mb, uint32_t dst, uint32_t u, uint32_t v, uint32_t n,
                  uint32_t tex, uint32_t eax, hle_span_regs* out) {
    uint32_t edi = dst, ecx = (u << 16) | (u >> 16), edx = v, ebx = tex, esi, c;
    ebx = (ebx & ~0xffu) | (ecx & 0xffu);                         // b08 mov bl,cl
    edx >>= 8;                                                    // b0a
    ecx = (ecx & 0xffff0000u) | (edx & 0xffffu);                  // b0d mov cx,dx
    edx = n << 16;                                                // b10/b13
    const uint32_t ebp = rd32(mb, HLE_SPAN_STEP_VA);              // b16
    const uint8_t dl = mb[HLE_SPAN_USTEP_VA];                     // b1c
    edx = (edx & ~0xffu) | dl;
    ebx = (ebx & ~0xff00u) | (ecx & 0xff00u);                     // b22 mov bh,ch
#define ADD()  do { const uint32_t t_ = ecx + ebp; c = t_ < ecx; ecx = t_; } while (0)
#define ADC()  do { ebx = (ebx & ~0xffu) | ((ebx + dl + c) & 0xffu); } while (0)
#define BHCH() do { ebx = (ebx & ~0xff00u) | (ecx & 0xff00u); } while (0)
    const int skip4 = lt_after_sub(edx, 0x40000u); edx -= 0x40000u;   // b24/b2a
    if (!skip4) {
        for (;;) {
            eax = (eax & ~0xff00u) | ((uint32_t)mb[ebx] << 8);    // b2c mov ah,[ebx]
            ADD(); ADC();                                         // b2e/b30
            edi -= 4;                                             // b32
            BHCH(); ADD();                                        // b35/b37
            esi = ebx; BHCH(); ADC();                             // b39/b3b/b3d
            ADD();                                                // b3f
            eax = (eax & ~0xffu) | mb[esi];                       // b41 mov al,[esi]
            esi = ebx; ADC();                                     // b43/b45
            eax <<= 16;                                           // b47
            BHCH();                                               // b4a
            eax = (eax & ~0xff00u) | ((uint32_t)mb[esi] << 8);    // b4c mov ah,[esi]
            ADD(); esi = ebx; BHCH(); ADC();                      // b4e/b50/b52/b54
            eax = (eax & ~0xffu) | mb[esi];                       // b57 mov al,[esi]
            const int done = lt_after_sub(edx, 0x40000u); edx -= 0x40000u;  // b59
            wr32(mb, edi, eax);                                   // b5f
            if (done) break;                                      // b61 jge
        }
    }
    const int skip1 = lt_after_add(edx, 0x30000u); edx += 0x30000u;  // b63/b69
    if (!skip1) {
        for (;;) {
            eax = (eax & ~0xffu) | mb[ebx];                       // b6b mov al,[ebx]
            ADD(); ADC();                                         // b6d/b6f
            --edi; mb[edi] = (uint8_t)eax;                        // b71/b72
            BHCH();                                               // b74
            const int done = lt_after_sub(edx, 0x10000u); edx -= 0x10000u;  // b76
            if (done) break;                                      // b7c jge
        }
    }
#undef ADD
#undef ADC
#undef BHCH
    out->eax = eax; out->ecx = ecx; out->edx = edx;
}

// Transcripción de 0x450c19..0x450c89.
static void semi_exact(uint8_t* mb, uint32_t dst, uint32_t u, uint32_t v, uint32_t n,
                       uint32_t tex, uint32_t blend, hle_span_regs* out) {
    uint32_t edi = dst, ecx = (u << 16) | (u >> 16), edx = v, ebx = tex, eax, c;
    ebx = (ebx & ~0xffu) | (ecx & 0xffu);                         // c2e mov bl,cl
    edx >>= 8;                                                    // c30
    ecx = (ecx & 0xffff0000u) | (edx & 0xffffu);                  // c33 mov cx,dx
    edx = n << 16;                                                // c36/c39
    eax = blend;                                                  // c3c
    const uint32_t ebp = rd32(mb, HLE_SPAN_STEP_VA);              // c3f
    const uint8_t dl = mb[HLE_SPAN_USTEP_VA];                     // c45
    edx = (edx & ~0xffu) | dl;
    ebx = (ebx & ~0xff00u) | (ecx & 0xff00u);                     // c4b mov bh,ch
#define ADD()  do { const uint32_t t_ = ecx + ebp; c = t_ < ecx; ecx = t_; } while (0)
#define ADC()  do { ebx = (ebx & ~0xffu) | ((ebx + dl + c) & 0xffu); } while (0)
#define BHCH() do { ebx = (ebx & ~0xff00u) | (ecx & 0xff00u); } while (0)
#define SETDH(x) do { edx = (edx & ~0xff00u) | ((uint32_t)(x) << 8); } while (0)
    int lt = lt_after_sub(edx, 0x10000u); edx -= 0x10000u;       // c4d/c53
    if (!lt) {
        eax = (eax & ~0xffu) | mb[ebx];                           // c55 mov al,[ebx]
        ADD();                                                    // c57
        eax = (eax & ~0xff00u) | ((uint32_t)mb[edi - 1] << 8);    // c59 mov ah,[edi-1]
        BHCH(); ADC();                                            // c5c/c5e
        --edi;                                                    // c60
        SETDH(mb[eax]);                                           // c61 mov dh,[eax]
        lt = lt_after_sub(edx, 0x10000u); edx -= 0x10000u;       // c63/c69
        if (!lt) {
            for (;;) {
                eax = (eax & ~0xffu) | mb[ebx];                   // c6b
                ADD();                                            // c6d
                eax = (eax & ~0xff00u) | ((uint32_t)mb[edi - 1] << 8);  // c6f
                BHCH(); ADC();                                    // c72/c74
                mb[edi] = (uint8_t)(edx >> 8);                    // c76 mov [edi],dh
                SETDH(mb[eax]);                                   // c78
                --edi;                                            // c7a
                lt = lt_after_sub(edx, 0x10000u); edx -= 0x10000u;  // c7b/c81
                if (lt) break;
            }
        }
        mb[edi] = (uint8_t)(edx >> 8);                            // c83 mov [edi],dh
    }
#undef ADD
#undef ADC
#undef BHCH
#undef SETDH
    out->eax = eax; out->ecx = ecx; out->edx = edx;
}

// ---------------------------------------------------------------------------
// Camino rápido (HW-052, medido: la transcripción literal era más lenta que la
// traducción de box86). Formulación cerrada, equivalente bit a bit:
//   add ecx,ebp ; adc bl,dl  ==  X += S  (40 bits: X = bl:ecx, S = dl:ebp)
//   offset del texel        ==  (X & 0xff00) | ((X >> 32) & 0xff)   (bh=ch, bl)
//   píxel i (i-ésimo leído)  →  dst[-1-i]
// Registros finales en forma cerrada (n < 32768): EDX = 0xffff0000 | dl (opaco)
// y 0xffff0000 | dh<<8 | dl (semi); ECX = low32(X_n); EAX según el último grupo.
// Si destino y textura/tabla se solapan, el orden lectura/escritura del original
// importa: se usa la transcripción exacta.
static inline int overlap(uint32_t dst, uint32_t n, uint32_t page) {
    return dst - n < page + 0x10000u && page < dst;
}

static inline uint64_t x_init(uint32_t u, uint32_t v) {
    const uint32_t ecx = (u << 16) | ((v >> 8) & 0xffffu);       // rol(u,16) con cx = v>>8
    return ((uint64_t)((u >> 16) & 0xffu) << 32) | ecx;          // bl = (u>>16)&0xff
}

void hle_span_ft3(uint8_t* mb, uint32_t dst, uint32_t u, uint32_t v, uint32_t n,
                  uint32_t tex, uint32_t eax, hle_span_regs* out) {
    const uint32_t page = tex & 0xffff0000u;
    if (n >= 32768u || overlap(dst, n, page)) { span_exact(mb, dst, u, v, n, tex, eax, out); return; }
    const uint32_t ebp = rd32(mb, HLE_SPAN_STEP_VA);
    const uint8_t dl = mb[HLE_SPAN_USTEP_VA];
    const uint64_t S = ((uint64_t)dl << 32) | ebp;
    const uint8_t* t = mb + page;
    uint8_t* d = mb + dst;
    uint64_t X = x_init(u, v);
    uint8_t p0 = 0, p1 = 0, p2 = 0, p3 = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t px = t[(X & 0xff00u) | ((X >> 32) & 0xffu)];
        d[-1 - (int32_t)i] = px;
        p0 = p1; p1 = p2; p2 = p3; p3 = px;                      // últimos 4 leídos
        X = (X + S) & 0xffffffffffull;
    }
    const uint32_t g = n >> 2, tail = n & 3u;
    if (n) {
        if (tail) {
            uint32_t hi = eax;
            if (g) {  // EAX del último grupo completo: píxeles 4g-4..4g-1
                const uint32_t b = 4 * g - 4;
                hi = ((uint32_t)d[-1 - (int32_t)b] << 24) | ((uint32_t)d[-2 - (int32_t)b] << 16) |
                     ((uint32_t)d[-3 - (int32_t)b] << 8) | d[-4 - (int32_t)b];
            }
            eax = (hi & ~0xffu) | p3;
        } else {
            eax = ((uint32_t)p0 << 24) | ((uint32_t)p1 << 16) | ((uint32_t)p2 << 8) | p3;
        }
    }
    out->eax = eax; out->ecx = (uint32_t)X; out->edx = 0xffff0000u | dl;
}

void hle_span_ft3_semi(uint8_t* mb, uint32_t dst, uint32_t u, uint32_t v, uint32_t n,
                       uint32_t tex, uint32_t blend, hle_span_regs* out) {
    const uint32_t page = tex & 0xffff0000u, bpage = blend & 0xffff0000u;
    if (n >= 32768u || overlap(dst, n, page) || overlap(dst, n, bpage) || !n) {
        semi_exact(mb, dst, u, v, n, tex, blend, out); return;
    }
    const uint32_t ebp = rd32(mb, HLE_SPAN_STEP_VA);
    const uint8_t dl = mb[HLE_SPAN_USTEP_VA];
    const uint64_t S = ((uint64_t)dl << 32) | ebp;
    const uint8_t* t = mb + page;
    const uint8_t* bt = mb + bpage;
    uint8_t* d = mb + dst;
    uint64_t X = x_init(u, v);
    uint32_t al = 0, ah = 0, dh = 0;
    for (uint32_t i = 0; i < n; ++i) {
        al = t[(X & 0xff00u) | ((X >> 32) & 0xffu)];
        ah = d[-1 - (int32_t)i];
        dh = bt[(ah << 8) | al];
        d[-1 - (int32_t)i] = (uint8_t)dh;
        X = (X + S) & 0xffffffffffull;
    }
    out->eax = bpage | (ah << 8) | al;
    out->ecx = (uint32_t)X;
    out->edx = 0xffff0000u | (dh << 8) | dl;
}
