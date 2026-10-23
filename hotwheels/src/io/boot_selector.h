// src/io/boot_selector.h — selector de arranque (HW-020, spec 010).
//
// Se abre manteniendo L al bootear el VPK. En Vita dibuja una pantalla propia
// (framebuffer sceDisplay + fuente 8x16 de winx86) y navega con los botones
// físicos; al confirmar guarda hwvita.ini. En host son no-ops para los tests.
#pragma once
#include <string>

namespace hw::io {

// true si el usuario mantiene L al bootear (o HW_CFG=1 para forzar).
bool boot_should_open();

// Muestra el selector, edita y (si se confirma) guarda `ini_path`.
// Devuelve 1 si guardó, 0 si canceló / no aplica (host).
int run_boot_selector(const std::string& ini_path);

}  // namespace hw::io
