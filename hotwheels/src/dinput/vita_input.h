// src/dinput/vita_input.h — InputSource Vita para DINPUT (HW-022, spec 007b).
//
// La logica de mapeo (fit_rect, zona muerta, botones, touch) es pura y
// host-testeable; el acceso a sceCtrl/sceTouch queda detras de __vita__.
#pragma once

#include "dinput/dinput.h"

#include <cstdint>

namespace hw::dinput {

// Estado crudo de un pad (espejo de los campos utiles de SceCtrlData).
struct RawPad {
    uint32_t buttons = 0;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128, lt = 0, rt = 0;
};
struct RawTouch {
    bool active = false;
    int16_t x = 0, y = 0;
    int16_t max_x = 1919, max_y = 1087;  // rango del digitizer (ajustable)
};

struct FitRect { int x, y, w, h; };

constexpr int kVitaWidth = 960, kVitaHeight = 544, kGameWidth = 640, kGameHeight = 480;

// Identico a present_scale.cpp (winx86): 640x480 -> 960x544 = {117,0,725,544}.
FitRect fit_rect(int src_w, int src_h, int dst_w, int dst_h, bool stretch);

// Digitizer (0..max) -> pantalla 960x544 (0..959 x 0..543).
void touch_raw_to_display(int raw_x, int raw_y, int max_x, int max_y, int& dx, int& dy);
// Pantalla 960x544 -> coords de juego 640x480 dentro del fit_rect (recorta a bordes).
void touch_display_to_game(int dx, int dy, int& gx, int& gy);

// Eje sceCtrl (0..255, centro 128) -> -32767..32767 con zona muerta (y reescala).
int16_t axis_from_raw(uint8_t raw, int32_t deadzone);
// SCE_CTRL_* -> bits kPad* de PadState.
uint32_t buttons_to_pad(uint32_t sce_buttons);

// Arma un snapshot (pads + keys) desde los pads crudos y el touch.
InputSnapshot build_snapshot(const RawPad& p0, const RawPad* p1, const RawTouch& touch,
    const MapperConfig& cfg);

class VitaInputSource : public InputSource {
public:
    explicit VitaInputSource(const MapperConfig& cfg = MapperConfig{});
    // Inyeccion host (en Vita poll() lee hardware).
    void set_raw(const RawPad& p0, const RawPad* p1, const RawTouch& touch);
    InputSnapshot poll() override;
    const MapperConfig& config() const { return cfg_; }

private:
    void read_hardware();
    MapperConfig cfg_;
    RawPad p0_;
    bool has_p1_ = false;
    RawPad p1_{};
    RawTouch touch_;
    bool prev_touch_ = false;
    int last_gx_ = 0, last_gy_ = 0;
    bool inited_ = false;
};

}  // namespace hw::dinput
