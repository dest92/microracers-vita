// boot.cpp — cablea el PE del juego en winx86 hasta dejar EIP en el entry
// point. Misma secuencia que D2Vita tools/rt_boot.cpp: add_module → commit →
// default shim → link. Ejecutar es cosa del backend (Vita: make_cpu_box86).
#include "boot/hw_boot.h"

#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/pe_image.h"

#include <cstdio>

namespace hw {

bool boot_to_entry(d2rt::Bridge& br, d2rt::Cpu& cpu, const std::vector<uint8_t>& pe,
                   const BootOptions& opt, BootResult& r, std::string& err) {
    r = BootResult{};
    d2rt::PeImage* im = br.add_module(opt.module_key, pe, err);
    if (!im) return false;
    if (!br.commit(err)) return false;
    if (opt.install_shims && !opt.install_shims(br, cpu, err)) return false;
    if (opt.on_unresolved) {
        br.set_default_shim(opt.on_unresolved);
    } else {
        const bool log = opt.log_unresolved;
        br.set_default_shim([log](d2rt::Cpu&, const std::string& tag) -> uint32_t {
            if (log) std::printf("[hw] import sin shim: %s\n", tag.c_str());
            return 0;
        });
    }
    if (!br.link(err)) return false;

    r.load_base = im->load_base();
    r.entry_va = im->entry_va();
    r.import_count = (uint32_t)im->imports().size();
    r.unresolved_count = br.unresolved_count();
    r.sentinel = br.sentinel();

    // Pila: tope de la pila que mapeó commit(); retaddr = centinela (el entry
    // de un EXE no recibe argumentos).
    const uint32_t top = opt.stack_top ? opt.stack_top : br.stack_top();
    const uint32_t esp = top - 0x10;
    if (!cpu.mapped(esp) || !cpu.mapped(top - 1)) {
        err = "stack_top no cae en la pila mapeada por el Bridge"; return false;
    }
    cpu.set_reg(d2rt::R_ESP, esp);
    cpu.write_u32(esp, r.sentinel);
    cpu.set_reg(d2rt::R_EBP, 0);
    cpu.set_reg(d2rt::R_EIP, r.entry_va);
    return true;
}

} // namespace hw
