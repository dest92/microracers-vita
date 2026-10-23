// Stub host de dyn86_intrin para test_hw_intrin (sin winx86).
int dyn86_intrin_on; int dyn86_intrin_add(unsigned long a, void* f, unsigned short r, unsigned short i, unsigned short m, const char* n){(void)a;(void)f;(void)r;(void)i;(void)m;(void)n;return 1;}
unsigned long dyn86_membase;
unsigned hw_emu_reg(void* e, int r){(void)e;(void)r;return 0;}
void hw_emu_set_reg(void* e, int r, unsigned v){(void)e;(void)r;(void)v;}
int dyn86_intrin_n; struct { unsigned long va; void* fn; unsigned short a,b,c; const char* n; unsigned long long calls, served; } dyn86_intrin_tbl[16];
int dyn86_intrin_report(char* o, unsigned c){(void)o;(void)c;return 0;}
int hw_hle_ftol(void* e, unsigned esp){(void)e;(void)esp;return 0;}
int hw_ftol_check_mode;
struct { unsigned long long a,b,c,d; unsigned e; long long f; int g; } hw_ftol_stats;
int dyn86_jitprof; unsigned long long dyn86_jp_lookup_calls, dyn86_jp_lookup_hits, dyn86_jp_lookup_miss, dyn86_jp_recompiles, dyn86_jp_revalidations;
int hw_hle_raster_ft3(void* e, unsigned s){(void)e;(void)s;return 0;}
int hw_hle_raster_ft3_semi(void* e, unsigned s){(void)e;(void)s;return 0;}
