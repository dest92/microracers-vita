// winx86-0006: camino rápido de arm_fist64_{0..3} (box86 FISTP m64) —
// equivalencia contra la implementación original sobre bordes + aleatorios.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t ref(double d, int mode) {
    if (isgreater(d, (double)(int64_t)0x7fffffffffffffffLL) || isless(d, -(double)(uint64_t)0x8000000000000000LL) || !isfinite(d))
        return (int64_t)0x8000000000000000LL;
    switch (mode) { case 0: return nearbyint(d); case 1: return floor(d); case 2: return ceil(d); default: return trunc(d); }
}
// Debe coincidir textualmente con patches/winx86-0006-fist64-fast.patch.
#define FIST_FAST(expr) do { if (d > -2147483649.0 && d < 2147483648.0) { const double r_ = (expr); \
    if (r_ >= -2147483648.0 && r_ <= 2147483647.0) return (int64_t)(int32_t)r_; } } while (0)
static int64_t fast(double d, int mode) {
    switch (mode) { case 0: FIST_FAST(nearbyint(d)); break; case 1: FIST_FAST(floor(d)); break;
                    case 2: FIST_FAST(ceil(d)); break; default: FIST_FAST(d); break; }
    return ref(d, mode);
}
int main(void) {
    const double edge[] = {0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 2147483647.0, 2147483647.4, 2147483647.6,
        2147483648.0, -2147483648.0, -2147483648.4, -2147483648.6, -2147483649.0, 1e18, -1e18, 9.3e18, -9.3e18,
        INFINITY, -INFINITY, NAN, 4503599627370496.5, 1e-300, -1e-300};
    long bad = 0, n = 0;
    for (int m = 0; m < 4; ++m) {
        for (size_t i = 0; i < sizeof edge / sizeof edge[0]; ++i, ++n)
            if (ref(edge[i], m) != fast(edge[i], m)) { printf("DIFF mode=%d d=%.17g\n", m, edge[i]); ++bad; }
        srand(42 + m);
        for (int i = 0; i < 2000000; ++i, ++n) {
            uint64_t bits = ((uint64_t)rand() << 34) ^ ((uint64_t)rand() << 3) ^ (uint64_t)rand();
            double d; memcpy(&d, &bits, 8);
            if (i & 1) d = ((double)rand() / RAND_MAX - 0.5) * 6e9;   // rango int32 ± margen
            if (ref(d, m) != fast(d, m)) { if (bad < 10) printf("DIFF mode=%d d=%.17g\n", m, d); ++bad; }
        }
    }
    printf("%s test_fist64_fast: %ld casos, %ld diferencias\n", bad ? "FAIL" : "PASS", n, bad);
    return bad != 0;
}
