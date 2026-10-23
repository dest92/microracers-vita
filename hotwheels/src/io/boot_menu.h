// src/io/boot_menu.h — selector de arranque portable (HW-020, spec 010).
//
// Modelo de menú sin dependencia de render/input: el backend Vita dibuja
// `lines()` en una pantalla propia y traduce sceCtrl a MenuAction. Se abre
// manteniendo L al bootear; al confirmar, `save()` escribe hwvita.ini.
#pragma once
#include <string>
#include <vector>

#include "io/config.h"

namespace hw::io {

enum class MenuAction { Up, Down, Left, Right, Confirm, Cancel };
enum class MenuResult { Continue, SaveAndExit, CancelExit };

class BootMenu {
public:
    explicit BootMenu(HwVitaConfig& cfg) : cfg_(cfg) {}

    int count() const;
    int selected() const { return sel_; }
    std::string label(int i) const;
    std::string value(int i) const;

    MenuResult handle(MenuAction a);
    std::vector<std::string> lines() const;

    // Guarda el estado actual (para SaveAndExit) y expone la config mutada.
    bool save(const std::string& path) { return hw::io::save(path, cfg_); }
    HwVitaConfig& config() { return cfg_; }

private:
    // SPEC-056: sin ítem de FPS; el runtime corre siempre en original.
    enum Item { kScheme = 0, kScaling, kFilter, kItemCount };
    void cycle(int dir);

    HwVitaConfig& cfg_;
    int sel_ = 0;
};

}  // namespace hw::io
