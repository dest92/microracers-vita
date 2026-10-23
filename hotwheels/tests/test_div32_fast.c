// winx86-0007: camino de 32 bits en div32/idiv32 de box86 (A9 sin división
// hardware: el original siempre divide 64 bits). Equivalencia de cociente,
// resto y #DE contra la implementación original.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint32_t q, r; int de; } res_t;
static res_t idiv_ref(uint32_t edx, uint32_t eax, uint32_t s) {
    res_t o = {0, 0, 0};
    int64_t dvd = (int64_t)(int32_t)edx * 0x100000000LL + eax;
    if (!s) { o.de = 1; return o; }
    if (dvd == INT64_MIN && (int32_t)s == -1) { o.de = 1; return o; }  // UB en C; en x86 es #DE
    lldiv_t p = lldiv(dvd, (int32_t)s);
    if (p.quot < INT32_MIN || p.quot > INT32_MAX) { o.de = 1; return o; }
    o.q = (uint32_t)p.quot; o.r = (uint32_t)p.rem; return o;
}
static res_t div_ref(uint32_t edx, uint32_t eax, uint32_t s) {
    res_t o = {0, 0, 0};
    uint64_t dvd = (((uint64_t)edx) << 32) | eax;
    if (!s) { o.de = 1; return o; }
    uint64_t d = dvd / s, m = dvd % s;
    if (d > 0xffffffff) { o.de = 1; return o; }
    o.q = (uint32_t)d; o.r = (uint32_t)m; return o;
}
// Debe coincidir con patches/winx86-0007-div32-fast.patch.
static res_t idiv_fast(uint32_t edx, uint32_t eax, uint32_t s) {
    // eax == INT32_MIN excluido: cubre INT32_MIN/-1 (desborde); el fallback
    // conserva el rango IDIV x86 [-2^31, 2^31-1].
    if (s && edx == (uint32_t)((int32_t)eax >> 31) && eax != 0x80000000u) {
        res_t o = {(uint32_t)((int32_t)eax / (int32_t)s), (uint32_t)((int32_t)eax % (int32_t)s), 0}; return o;
    }
    return idiv_ref(edx, eax, s);
}
static res_t div_fast(uint32_t edx, uint32_t eax, uint32_t s) {
    if (s && !edx) { res_t o = {eax / s, eax % s, 0}; return o; }
    return div_ref(edx, eax, s);
}
static int eq(res_t a, res_t b) { return a.de == b.de && (a.de || (a.q == b.q && a.r == b.r)); }
int main(void) {
    const uint32_t v[] = {0, 1, 2, 3, 7, 0x7fffffff, 0x80000000u, 0x80000001u, 0xfffffffe, 0xffffffff, 12345, 0x10000};
    long n = 0, bad = 0;
    const res_t min_quotient = idiv_ref(0xffffffffu, 0x80000000u, 1);
    const res_t min_quotient_fast = idiv_fast(0xffffffffu, 0x80000000u, 1);
    if (min_quotient.de || min_quotient.q != 0x80000000u || min_quotient.r != 0 || !eq(min_quotient, min_quotient_fast)) {
        printf("FAIL IDIV signed minimum quotient reference=%d:%08x/%08x optimized=%d:%08x/%08x\n",
            min_quotient.de, min_quotient.q, min_quotient.r,
            min_quotient_fast.de, min_quotient_fast.q, min_quotient_fast.r);
        return 1;
    }
    for (size_t a = 0; a < 12; ++a) for (size_t b = 0; b < 12; ++b) for (size_t c = 0; c < 12; ++c, ++n) {
        if (!eq(idiv_ref(v[a], v[b], v[c]), idiv_fast(v[a], v[b], v[c]))) { printf("IDIV %x:%x/%x\n", v[a], v[b], v[c]); ++bad; }
        if (!eq(div_ref(v[a], v[b], v[c]), div_fast(v[a], v[b], v[c]))) { printf("DIV %x:%x/%x\n", v[a], v[b], v[c]); ++bad; }
    }
    srand(9);
    for (int i = 0; i < 3000000; ++i, ++n) {
        uint32_t eax = ((uint32_t)rand() << 16) ^ (uint32_t)rand(), s = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
        if (i & 1) s >>= (rand() % 31);
        uint32_t edx = (i % 3 == 0) ? (uint32_t)((int32_t)eax >> 31) : (i % 3 == 1) ? 0 : (uint32_t)rand() % 5;
        if (!eq(idiv_ref(edx, eax, s), idiv_fast(edx, eax, s))) { if (bad < 10) printf("IDIV %x:%x/%x\n", edx, eax, s); ++bad; }
        if (!eq(div_ref(edx, eax, s), div_fast(edx, eax, s))) { if (bad < 10) printf("DIV %x:%x/%x\n", edx, eax, s); ++bad; }
    }
    printf("%s test_div32_fast: %ld casos, %ld diferencias\n", bad ? "FAIL" : "PASS", n, bad);
    return bad != 0;
}
