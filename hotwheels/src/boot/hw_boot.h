// hw_boot.h — esqueleto hotwheels-vita: lee el PE del juego (sin overlay) y
// deja la CPU invitada de winx86 parada en el entry point. Spec 001.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace d2rt { struct Cpu; class Bridge; }

namespace hw {

struct PeFileInfo {
    uint64_t file_size = 0;     // tamaño total en disco
    uint64_t bytes_read = 0;    // bytes FÍSICAMENTE leídos (cabeceras + secciones; nunca overlay)
    uint64_t overlay_size = 0;  // bytes después de la última sección (no leídos)
};

// Lee sólo cabeceras + datos crudos de secciones. Rechaza no-PE32/no-i386.
bool read_pe_file(const std::string& path, std::vector<uint8_t>& out,
                  PeFileInfo& info, std::string& err);

struct BootOptions {
    std::string module_key = "game.exe";
    bool log_unresolved = true;  // el shim por defecto loguea cada import sin shim
    // Si se da, reemplaza al shim por defecto (p.ej. parada controlada en Vita).
    std::function<uint32_t(d2rt::Cpu&, const std::string& tag)> on_unresolved;
    // Registro de shims del port: commit() ya mapeó guest/traps; link() aún no
    // resolvió imports. Devuelve false y escribe err si no se puede instalar.
    std::function<bool(d2rt::Bridge&, d2rt::Cpu&, std::string& err)> install_shims;
    // Tope de la pila principal. 0 = el que mapeó el Bridge (Bridge::stack_top(),
    // parche patches/winx86-0001-stack_top.patch), respeta D2LAYOUT/WX86_STACKBASE.
    uint32_t stack_top = 0;
};

struct BootResult {
    uint32_t load_base = 0;
    uint32_t entry_va = 0;
    uint32_t import_count = 0;
    uint32_t unresolved_count = 0;
    uint32_t sentinel = 0;       // retaddr centinela empujada en la pila
};

// add_module + commit + set_default_shim + link; EIP=entry, ESP en la pila con
// el centinela como dirección de retorno. No ejecuta x86. `br` debe estar
// construido sobre `cpu` y vivir mientras se use la CPU.
bool boot_to_entry(d2rt::Bridge& br, d2rt::Cpu& cpu, const std::vector<uint8_t>& pe,
                   const BootOptions& opt, BootResult& r, std::string& err);

} // namespace hw
