// BUG-035: tamaños de pool vitaGL (defaults y knobs acotados).
#include "present/vgl_pools.h"
#include <cassert>
#include <map>
#include <string>

int main() {
    using hw::present::vgl_pools;
    std::map<std::string, std::string> env;
    auto get = [&](const char* k) -> const char* { auto it = env.find(k); return it == env.end() ? nullptr : it->second.c_str(); };
    auto p = vgl_pools(get);
    assert(p.ram == 24u << 20 && p.cdram == 64u << 20);      // defaults con margen
    env["HW_VGL_RAM_MB"] = "8"; env["HW_VGL_VRAM_MB"] = "16"; // valores viejos (bisección)
    p = vgl_pools(get);
    assert(p.ram == 8u << 20 && p.cdram == 16u << 20);
    env["HW_VGL_RAM_MB"] = "0"; env["HW_VGL_VRAM_MB"] = "500"; // fuera de rango → acotado
    p = vgl_pools(get);
    assert(p.ram == 4u << 20 && p.cdram == 96u << 20);
    env["HW_VGL_RAM_MB"] = "basura";
    p = vgl_pools(get);
    assert(p.ram == 24u << 20);
    return 0;
}
