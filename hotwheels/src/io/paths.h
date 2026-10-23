// src/io/paths.h — política de rutas del juego en Vita (HW-020, spec 010).
//
// REUSA hw::shims::Vfs (HW-006): acá sólo se decide qué carpeta del host se
// monta como C:/D: y cuál es el overlay de escritura. No se reimplementa el FS.
#pragma once
#include <string>

#include "shims/vfs.h"

namespace hw::io {

// game = datos de sólo lectura (+ save para escritura), cd = carpeta con hwicon.bmp.
struct Roots {
    std::string game, cd, save;
};

// Vita: ux0:data/hotwheels (game=cd) + ux0:data/hotwheels/save.
Roots vita_roots(const std::string& data_root = "ux0:data/hotwheels");
// Host (tests/qemu): <extracted> + <extracted>/save.
Roots host_roots(const std::string& extracted_root);

// Crea el directorio de escritura. true si quedó usable (o no hacía falta).
bool ensure_save(const Roots& r);

// Monta C: (game + save) y D: (cd), cwd C:\ — usa Vfs::mount/set_cwd.
void mount(hw::shims::Vfs& vfs, const Roots& r);

// Archivos que el juego reescribe (FINDINGS #16). Acepta path o nombre.
bool is_writable_game_file(const std::string& win_path);

}  // namespace hw::io
