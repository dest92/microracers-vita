// hw_layout.h — layout de memoria invitada de hotwheels-vita (spec 002).
// HotWheels.exe no tiene relocs: carga fija en 0x00400000, así que el pack
// arranca DESPUÉS del exe. Todo debe caer en el bloque único WX86_ARENA de
// cpu_box86 (H(va) = va + membase).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace d2rt { struct Cpu; }

namespace hw {

struct Region { const char* name; uint32_t base; uint32_t size; };

struct Layout {
    bool ok = false;
    std::string why;            // motivo si !ok
    uint32_t exe_base = 0, exe_size = 0;
    Region heap{"heap", 0, 0}, modules{"modules", 0, 0}, misc{"misc", 0, 0}, va{"va", 0, 0},
           main_stack{"main_stack", 0, 0}, thread_stacks{"thread_stacks", 0, 0},
           thread_tibs{"thread_tibs", 0, 0}, traps{"traps", 0, 0};
    uint32_t thread_stack_each = 0;
    uint32_t main_stack_top = 0;
    uint32_t main_tib = 0;      // TIB del hilo principal (0 = FS lineal, página 0)
    uint32_t span = 0;          // fin del VA más alto usado
    uint32_t arena = 0;         // tamaño WX86_ARENA (span + 16 MiB de holgura de membase)
    std::vector<Region> regions() const;
};

Layout make_layout(uint32_t exe_base, uint32_t exe_size);

// Variables que deben estar en el entorno ANTES de construir Cpu y Bridge.
std::vector<std::pair<std::string, std::string>> layout_env(const Layout& l);
void apply_layout_env(const Layout& l);   // setenv(…, 1) de layout_env

// Mapea heap (RW), VA y MISC (RWX) y arma el scratch de winx86 en MISC.
bool map_runtime(d2rt::Cpu& cpu, const Layout& l, std::string& err);

} // namespace hw
