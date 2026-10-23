// hw_layout.cpp — ver hw_layout.h y specs/002-vita-runtime.md (tabla de regiones).
#include "runtime/hw_layout.h"

#include "runtime/cpu.h"
#include "runtime/guest_scratch.h"

#include <cstdio>
#include <cstdlib>

namespace hw {

namespace {
constexpr uint32_t MiB = 0x100000;
constexpr uint32_t kArenaSlack = 16 * MiB;      // holgura de redondeo de membase (cpu_box86)
constexpr uint32_t kHeapBase = 0x00500000;      // primer MiB libre después del exe de HW
constexpr uint32_t kMaxArena = 0x10300000;      // techo = el de D2Vita (259 MiB), validado en consola
std::string hex(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%08x", v); return b; }
}

std::vector<Region> Layout::regions() const {
    return {heap, modules, misc, va, main_stack, thread_stacks, thread_tibs, traps};
}

Layout make_layout(uint32_t exe_base, uint32_t exe_size) {
    Layout l;
    l.exe_base = exe_base;
    l.exe_size = exe_size;
    // Pack contiguo; tamaños provisorios (spec 002, sin medición del juego aún).
    uint32_t at = kHeapBase;
    auto take = [&](Region& r, uint32_t size) { r.base = at; r.size = size; at += size; };
    take(l.heap, 64 * MiB);
    take(l.modules, 8 * MiB);
    take(l.misc, 2 * MiB);
    at = (at + 0xFFFFFF) & ~0xFFFFFFu;            // VA alineada a 16 MiB
    take(l.va, 96 * MiB);
    take(l.main_stack, 2 * MiB);                  // = Bridge::stack_size_
    l.thread_stack_each = 1 * MiB;
    take(l.thread_stacks, 16 * l.thread_stack_each);
    take(l.thread_tibs, 1 * MiB);
    take(l.traps, 1 * MiB);                       // = extensión que deriva el Bridge de WX86_TRAPBASE
    l.main_stack_top = l.main_stack.base + l.main_stack.size;
    l.main_tib = 0;
    l.span = at;
    l.arena = l.span + kArenaSlack;

    const uint64_t exe_end = (uint64_t)exe_base + exe_size;
    if (exe_base < 0x1000) { l.why = "exe en la página 0 (TIB principal)"; return l; }
    for (const Region& r : l.regions())
        if ((uint64_t)r.base < exe_end && (uint64_t)exe_base < (uint64_t)r.base + r.size) {
            l.why = std::string("el exe (base fija, sin relocs) se solapa con ") + r.name; return l;
        }
    if (exe_end > l.span) { l.why = "el exe queda fuera del span de la arena"; return l; }
    if (l.arena > kMaxArena) { l.why = "arena mayor que el presupuesto validado"; return l; }
    l.ok = true;
    return l;
}

std::vector<std::pair<std::string, std::string>> layout_env(const Layout& l) {
    return {
        {"WX86_MODBASE", hex(l.modules.base)},
        {"WX86_STACKBASE", hex(l.main_stack.base)},
        {"WX86_TRAPBASE", hex(l.traps.base)},
        {"WX86_ARENA", hex(l.arena)},
    };
}

void apply_layout_env(const Layout& l) {
    for (auto& kv : layout_env(l)) setenv(kv.first.c_str(), kv.second.c_str(), 1);
}

bool map_runtime(d2rt::Cpu& cpu, const Layout& l, std::string& err) {
    if (!l.ok) { err = "layout inválido: " + l.why; return false; }
    if (!cpu.map(l.heap.base, l.heap.size, nullptr, d2rt::P_RW)) { err = "map heap"; return false; }
    if (!cpu.map(l.va.base, l.va.size, nullptr, d2rt::P_RWX)) { err = "map VA"; return false; }
    // RWX: aloja stubs de callbacks invitados (como MISC en rt_boot).
    if (!cpu.map(l.misc.base, l.misc.size, nullptr, d2rt::P_RWX)) { err = "map MISC"; return false; }
    // +0x100: mismo desplazamiento inicial que rt_boot.
    wx86_scratch_init(l.misc.base + 0x100, l.misc.size - 0x100);
    return true;
}

} // namespace hw
