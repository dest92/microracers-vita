// src/present/vgl_pools.h — tamaños de pools vitaGL (BUG-035).
// Medido en consola (menú, 2026-10-03): con RAM 8 MiB / CDRAM 16 MiB vitaGL
// quedaba con 1.4 / 3.6 MiB libres antes de cargar texturas de carrera, con
// 72 MiB de usuario y 80 MiB de CDRAM libres en el sistema. Defaults con
// margen; HW_VGL_RAM_MB / HW_VGL_VRAM_MB los ajustan (acotados) para bisecar.
#pragma once
#include <cstdint>
#include <cstdlib>

namespace hw::present {

struct VglPools { uint32_t ram, cdram; };

template <class Getenv>
VglPools vgl_pools(Getenv getenv_fn) {
    auto mb = [&](const char* key, uint32_t def, uint32_t lo, uint32_t hi) -> uint32_t {
        const char* e = getenv_fn(key);
        if (!e || !*e) return def << 20;
        char* end = nullptr;
        const unsigned long v = std::strtoul(e, &end, 10);
        if (end == e || *end) return def << 20;
        const uint32_t c = v < lo ? lo : v > hi ? hi : uint32_t(v);
        return c << 20;
    };
    return {mb("HW_VGL_RAM_MB", 24, 4, 48), mb("HW_VGL_VRAM_MB", 64, 8, 96)};
}

}  // namespace hw::present
