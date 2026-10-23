// src/io/config.h — hwvita.ini: botones, escalado, filtro, IO (HW-020, spec 010).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dinput/dinput.h"

namespace hw::io {

struct HwVitaConfig {
    std::string data_root = "ux0:data/hotwheels";
    // 60 fps: lo consume HW-023 (luna). original = timing del juego (~40 Hz).
    enum class FpsMode { Original, Interp60, Native60 } fps_mode = FpsMode::Original;
    enum class Scaling { Fit, Stretch } scaling = Scaling::Fit;
    // Native maps D3D XYZRHW directly to the Vita output viewport; Original
    // keeps the legacy 4:3 viewport mapping.
    enum class RenderScale { Original, Native } render_scale = RenderScale::Native;
    enum class Filter { Point, Linear, Game } filter = Filter::Point;
    bool force_d3d = true;
    // Preset de mapa de botones (HW-024, deepseek). Selector entre scheme_names().
    std::string control_scheme = "original";
    bool preload = true;
    size_t max_file_kb = 2048;
    size_t max_total_mb = 32;
    bool mouse_from_right_stick = true;
    int stick_deadzone = 8000;
    // Overrides de botones por pad, en términos de HW-016 (Button/DIK).
    bool btn_set[2][hw::dinput::kButtonCount] = {};
    uint8_t btn_dik[2][hw::dinput::kButtonCount] = {};
    std::vector<std::string> warnings;
};

const char* fps_mode_name(HwVitaConfig::FpsMode m);
bool fps_mode_parse(const std::string& s, HwVitaConfig::FpsMode& out);

// Presets registrados de mapa de botones (mecanismo; HW-024 define el set final).
std::vector<std::string> scheme_names();
// Vuelca un preset sobre el MapperConfig (original/swapped; desconocido = original).
void apply_control_scheme(hw::dinput::MapperConfig& m, const std::string& scheme);

// No lanzan. `out` se resetea a defaults; claves desconocidas/valores inválidos
// quedan en `out.warnings`. `load` de un archivo ausente = defaults, true.
bool parse(const std::string& text, HwVitaConfig& out);
bool load(const std::string& path, HwVitaConfig& out);
bool write_default(const std::string& path);  // sólo si no existe
std::string defaults_ini();

// Serializa el estado ACTUAL (lo usa el selector de arranque para guardar).
std::string to_ini(const HwVitaConfig& cfg);
bool save(const std::string& path, const HwVitaConfig& cfg);

// Apply the persistent D3D preference to the game settings before guest startup.
bool apply_display_defaults(const std::string& game_root, const std::string& save_root, bool force_d3d);

// Vuelca el preset + overrides sobre el MapperConfig de HW-016.
void apply_to(hw::dinput::MapperConfig& m, const HwVitaConfig& cfg);

}  // namespace hw::io
