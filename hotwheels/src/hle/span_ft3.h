// src/hle/span_ft3.h — HLE de spanFT3 (0x450af3, HotWheels.exe md5 4fbf44e9…), HW-052.
// Span texturizado afín 8 bpp escrito a mano en x86: textura en página de 64 KiB
// indexada por bl (u) / bh (v), pasos en punto fijo con acarreo propagado
// (add ecx,ebp ; adc bl,dl), 4 píxeles por vuelta de derecha a izquierda.
// Estado global armado por 0x450a40: paso empaquetado en [0x4679c8], paso
// entero de u en [0x4679cc]. Reproducción EXACTA de memoria y de EAX/ECX/EDX
// (EBX/ESI/EDI/EBP los restaura el guest). Test diferencial contra los bytes
// x86 originales: tests/test_hle_span_ft3.py (Unicorn).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t eax, ecx, edx; } hle_span_regs;

// mb: base host del espacio guest (host = mb + va). Args = los 5 cdecl de la
// función: dst (fin, exclusivo), u, v, n (píxeles), tex (página 64 KiB).
void hle_span_ft3(uint8_t* mb, uint32_t dst, uint32_t u, uint32_t v, uint32_t n,
                  uint32_t tex, uint32_t eax_in, hle_span_regs* out);

// spanFT3Semi 0x450c19: igual que spanFT3 + 6.º arg `blend` (tabla 64 KiB
// indexada por al=texel, ah=píxel destino); 1 px/vuelta con escritura en
// pipeline. No lee EAX de entrada.
void hle_span_ft3_semi(uint8_t* mb, uint32_t dst, uint32_t u, uint32_t v, uint32_t n,
                       uint32_t tex, uint32_t blend, hle_span_regs* out);

#define HLE_SPAN_FT3_VA        0x00450af3u
#define HLE_SPAN_FT3_SEMI_VA   0x00450c19u
#define HLE_SPAN_STEP_VA       0x004679c8u
#define HLE_SPAN_USTEP_VA      0x004679ccu

#ifdef __cplusplus
}
#endif
