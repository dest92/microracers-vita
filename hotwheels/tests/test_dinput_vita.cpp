// tests/test_dinput_vita.cpp — criterios HW-022 de specs/007-dinput.md (host).
#include "dinput/vita_input.h"
#include "io/config.h"

#include <cstdint>
#include <cstdio>

using namespace hw::dinput;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
    // ---- CA1: fit_rect ----
    FitRect fit = fit_rect(640, 480, 960, 544, false);
    CHECK(fit.x == 117 && fit.y == 0 && fit.w == 725 && fit.h == 544);
    FitRect stretch_rect = fit_rect(640, 480, 960, 544, true);
    CHECK(stretch_rect.x == 0 && stretch_rect.y == 0 && stretch_rect.w == 960 && stretch_rect.h == 544);

    // ---- CA2: zona muerta de ejes ----
    CHECK(axis_from_raw(128, 8000) == 0);
    CHECK(axis_from_raw(126, 8000) == 0 && axis_from_raw(130, 8000) == 0);
    CHECK(axis_from_raw(255, 8000) == 32767);
    CHECK(axis_from_raw(0, 8000) == -32767);
    CHECK(axis_from_raw(129, 0) > 0);
    CHECK(axis_from_raw(128, 0) == 0);
    CHECK(axis_from_raw(160, 8000) > 0);  // fuera de la zona muerta

    // ---- CA3: display 960x544 -> juego 640x480 ----
    int gx = 0, gy = 0;
    touch_display_to_game(480, 272, gx, gy);
    CHECK(gx == 320 && gy == 240);
    touch_display_to_game(117, 0, gx, gy);
    CHECK(gx == 0 && gy == 0);
    touch_display_to_game(50, 100, gx, gy);
    CHECK(gx == 0);  // barra izquierda -> borde
    touch_display_to_game(842, 272, gx, gy);
    CHECK(gx == 639);
    touch_display_to_game(480, 543, gx, gy);
    CHECK(gy == 479);

    // ---- CA4: digitizer -> display ----
    int dx = -1, dy = -1;
    touch_raw_to_display(0, 0, 1919, 1087, dx, dy);
    CHECK(dx == 0 && dy == 0);
    touch_raw_to_display(1919, 1087, 1919, 1087, dx, dy);
    CHECK(dx == 959 && dy == 543);

    // ---- CA5: SCE_CTRL_* -> bits PadState ----
    CHECK(buttons_to_pad(0x10) == kPadUp);              // SCE_CTRL_UP
    CHECK(buttons_to_pad(0x40) == kPadDown);            // SCE_CTRL_DOWN
    CHECK(buttons_to_pad(0x4000) == kPadCross);         // SCE_CTRL_CROSS
    CHECK(buttons_to_pad(0x2000) == kPadCircle);        // SCE_CTRL_CIRCLE
    CHECK(buttons_to_pad(0x8) == kPadStart);            // SCE_CTRL_START
    uint32_t multi = buttons_to_pad(0x10 | 0x4000);
    CHECK((multi & kPadUp) && (multi & kPadCross));

    // ---- CA6: VitaInputSource -> DInput teclado (mapper de HW-016) ----
    MapperConfig vis_cfg;
    vis_cfg.pad[0].set(Button::Cross, dik::RETURN);
    vis_cfg.pad[1].set(Button::Cross, dik::LSHIFT);
    VitaInputSource vis(vis_cfg);
    RawPad p0;  p0.buttons = 0x4000;  // SCE_CTRL_CROSS
    vis.set_raw(p0, nullptr, RawTouch{});
    CHECK(vis.poll().pad_count == 1);
    DirectInput di(vis, vis_cfg);
    Device* kb = di.create_device(kGuidSysKeyboard);
    CHECK(kb->set_data_format(DataFormat::Keyboard) == DI_OK);
    CHECK(kb->acquire() == DI_OK);
    uint8_t st[kKeyboardStateBytes] = {};
    CHECK(kb->get_device_state(kKeyboardStateBytes, st) == DI_OK);
    CHECK(st[dik::RETURN] == 0x80);
    RawPad p1;  p1.buttons = 0x4000;
    vis.set_raw(p0, &p1, RawTouch{});
    CHECK(vis.poll().pad_count == 2);
    CHECK(kb->get_device_state(kKeyboardStateBytes, st) == DI_OK);
    CHECK(st[dik::RETURN] == 0x80 && st[dik::LSHIFT] == 0x80);

    // ---- CA7: touch -> mouse (delta + boton) ----
    VitaInputSource vt;
    RawTouch t;  t.active = true;  t.max_x = 959;  t.max_y = 543;
    t.x = 480;  t.y = 272;
    vt.set_raw(RawPad{}, nullptr, t);
    InputSnapshot a = vt.poll();
    CHECK((a.mouse_buttons & 0x1) != 0 && a.mouse_dx == 0 && a.mouse_dy == 0);
    t.x = 600;  // +120 en display -> +106 en juego
    vt.set_raw(RawPad{}, nullptr, t);
    InputSnapshot b = vt.poll();
    CHECK((b.mouse_buttons & 0x1) != 0 && b.mouse_dx == 106 && b.mouse_dy == 0);
    t.active = false;
    vt.set_raw(RawPad{}, nullptr, t);
    InputSnapshot c = vt.poll();
    CHECK((c.mouse_buttons & 0x1) == 0);

    // ---- CA8: mapa desde hwvita.ini ----
    hw::io::HwVitaConfig cfg;
    hw::io::parse("[buttons.pad0]\nup=SPACE\n\n[general]\nstick_deadzone=1000\n", cfg);
    CHECK(cfg.warnings.empty());
    MapperConfig m;
    hw::io::apply_to(m, cfg);
    CHECK(m.pad[0].get(Button::DpadUp) == dik::SPACE);
    CHECK(m.stick_deadzone == 1000);
    RawPad ly;  ly.ly = 130;  // dentro de la zona muerta del ini
    InputSnapshot sl = build_snapshot(ly, nullptr, RawTouch{}, m);
    CHECK(sl.pads[0].ly == 0);
    ly.ly = 145;  // fuera
    InputSnapshot sl2 = build_snapshot(ly, nullptr, RawTouch{}, m);
    CHECK(sl2.pads[0].ly > 0);
    // mapa end-to-end: up=SPACE del ini -> GetDeviceState
    VitaInputSource v2(m);
    RawPad up;  up.buttons = 0x10;  // SCE_CTRL_UP
    v2.set_raw(up, nullptr, RawTouch{});
    DirectInput di2(v2);
    di2.config() = m;
    Device* kb2 = di2.create_device(kGuidSysKeyboard);
    kb2->set_data_format(DataFormat::Keyboard);
    kb2->acquire();
    uint8_t st2[kKeyboardStateBytes] = {};
    CHECK(kb2->get_device_state(kKeyboardStateBytes, st2) == DI_OK);
    CHECK(st2[dik::SPACE] == 0x80);

    if (g_fail) {
        std::printf("DINPUT-VITA: %d fallas\n", g_fail);
        return 1;
    }
    std::printf("DINPUT-VITA: OK (CA1..CA8)\n");
    return 0;
}
