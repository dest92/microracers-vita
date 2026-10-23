// src/runtime/hw_intrin.h — HLE de funciones guest calientes (HW-052).
// Objetivos registrados en dyn86_intrin (winx86) tras boot_to_entry y antes de
// la primera traducción. HW_INTRIN: 0 apagado (default), 1 nativo, 2 sólo
// plomería (helper siempre cae al cuerpo guest: mide el costo del round-trip).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace hw::intrin {

int parse_mode(const char* env);  // inválido/ausente → 0
// HW_INTRIN (0/1/2) o, si no está, el alias HW_HLE_RASTER (0/1/2).
int mode_from_env();

struct Target {
    uint32_t va;
    int (*fn)(void* emu, uint32_t esp);
    uint16_t retn, inmask, regmask;
    const char* name;
};

// Objetivos de HotWheels.exe (md5 4fbf44e9…); vacío hasta HW-051.
const Target* targets(unsigned* n);

// Arma dyn86_intrin_on y registra los objetivos. `arena_span`: tamaño del
// espacio guest válido [0, span); los helpers rechazan (fallback) accesos fuera.
int install(int mode, uint32_t arena_span, const std::function<void(const std::string&)>& log);

// Validación de rangos de spanFT3 (pura, testeable): destino [dst-n, dst) y
// página de textura de 64 KiB dentro de [0, span).
bool span_ft3_in_bounds(uint32_t dst, uint32_t n, uint32_t tex, uint32_t span);

// Línea de prueba de armado (dyn86_intrin_report); vacía si HW_INTRIN=0.
std::string report();
std::string report_intrin();

}  // namespace hw::intrin
