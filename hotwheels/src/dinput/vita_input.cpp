// src/dinput/vita_input.cpp — InputSource Vita para DINPUT (HW-022, spec 007b).
#include "dinput/vita_input.h"

#include <cstdint>

#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#else
// Valores de psp2common/ctrl.h para compilar/tests host.
#define SCE_CTRL_SELECT 0x00000001u
#define SCE_CTRL_START 0x00000008u
#define SCE_CTRL_UP 0x00000010u
#define SCE_CTRL_RIGHT 0x00000020u
#define SCE_CTRL_DOWN 0x00000040u
#define SCE_CTRL_LEFT 0x00000080u
#define SCE_CTRL_LTRIGGER 0x00000100u
#define SCE_CTRL_RTRIGGER 0x00000200u
#define SCE_CTRL_L1 0x00000400u
#define SCE_CTRL_R1 0x00000800u
#define SCE_CTRL_TRIANGLE 0x00001000u
#define SCE_CTRL_CIRCLE 0x00002000u
#define SCE_CTRL_CROSS 0x00004000u
#define SCE_CTRL_SQUARE 0x00008000u
#endif

namespace hw::dinput {

FitRect fit_rect(int src_w, int src_h, int dst_w, int dst_h, bool stretch) {
    if (stretch || src_w <= 0 || src_h <= 0) return {0, 0, dst_w, dst_h};
    int w = static_cast<int>((static_cast<long long>(src_w) * dst_h) / src_h);
    int h = dst_h;
    if (w > dst_w) {
        w = dst_w;
        h = static_cast<int>((static_cast<long long>(src_h) * dst_w) / src_w);
    }
    return {(dst_w - w) / 2, (dst_h - h) / 2, w, h};
}

void touch_raw_to_display(int raw_x, int raw_y, int max_x, int max_y, int& dx, int& dy) {
    dx = max_x > 0 ? static_cast<int>(static_cast<int64_t>(raw_x) * kVitaWidth / (max_x + 1)) : 0;
    dy = max_y > 0 ? static_cast<int>(static_cast<int64_t>(raw_y) * kVitaHeight / (max_y + 1)) : 0;
    if (dx < 0) dx = 0;
    if (dx > kVitaWidth - 1) dx = kVitaWidth - 1;
    if (dy < 0) dy = 0;
    if (dy > kVitaHeight - 1) dy = kVitaHeight - 1;
}

void touch_display_to_game(int dx, int dy, int& gx, int& gy) {
    const FitRect r = fit_rect(kGameWidth, kGameHeight, kVitaWidth, kVitaHeight, false);
    if (dx < r.x) gx = 0;
    else if (dx >= r.x + r.w) gx = kGameWidth - 1;
    else gx = (dx - r.x) * kGameWidth / r.w;
    if (dy < r.y) gy = 0;
    else if (dy >= r.y + r.h) gy = kGameHeight - 1;
    else gy = (dy - r.y) * kGameHeight / r.h;
}

int16_t axis_from_raw(uint8_t raw, int32_t deadzone) {
    int32_t unit;
    if (raw >= 128) unit = static_cast<int32_t>(raw - 128) * 32767 / 127;
    else unit = -(static_cast<int32_t>(128 - raw) * 32767 / 128);
    const int32_t a = unit < 0 ? -unit : unit;
    if (a <= deadzone) return 0;
    const int32_t span = 32767 - deadzone;
    if (span <= 0) return static_cast<int16_t>(unit);
    int32_t scaled = static_cast<int64_t>(a - deadzone) * 32767 / span;
    if (scaled > 32767) scaled = 32767;
    return static_cast<int16_t>(unit < 0 ? -scaled : scaled);
}

uint32_t buttons_to_pad(uint32_t b) {
    uint32_t m = 0;
    if (b & SCE_CTRL_SELECT) m |= kPadSelect;
    if (b & SCE_CTRL_START) m |= kPadStart;
    if (b & SCE_CTRL_UP) m |= kPadUp;
    if (b & SCE_CTRL_RIGHT) m |= kPadRight;
    if (b & SCE_CTRL_DOWN) m |= kPadDown;
    if (b & SCE_CTRL_LEFT) m |= kPadLeft;
    if (b & SCE_CTRL_LTRIGGER) m |= kPadL2;
    if (b & SCE_CTRL_RTRIGGER) m |= kPadR2;
    if (b & SCE_CTRL_L1) m |= kPadL;
    if (b & SCE_CTRL_R1) m |= kPadR;
    if (b & SCE_CTRL_TRIANGLE) m |= kPadTriangle;
    if (b & SCE_CTRL_CIRCLE) m |= kPadCircle;
    if (b & SCE_CTRL_CROSS) m |= kPadCross;
    if (b & SCE_CTRL_SQUARE) m |= kPadSquare;
    return m;
}

InputSnapshot build_snapshot(const RawPad& p0, const RawPad* p1, const RawTouch&,
    const MapperConfig& cfg) {
    InputSnapshot s;
    s.pad_count = p1 ? 2 : 1;
    auto fill = [&](PadState& pad, const RawPad& r) {
        pad.buttons = buttons_to_pad(r.buttons);
        pad.lx = axis_from_raw(r.lx, cfg.stick_deadzone);
        pad.ly = axis_from_raw(r.ly, cfg.stick_deadzone);
        pad.rx = axis_from_raw(r.rx, cfg.stick_deadzone);
        pad.ry = axis_from_raw(r.ry, cfg.stick_deadzone);
        pad.pov = -1;
    };
    fill(s.pads[0], p0);
    if (p1) fill(s.pads[1], *p1);
    return s;
}

VitaInputSource::VitaInputSource(const MapperConfig& cfg) : cfg_(cfg) {}

void VitaInputSource::set_raw(const RawPad& p0, const RawPad* p1, const RawTouch& touch) {
    p0_ = p0;
    touch_ = touch;
    if (p1) {
        p1_ = *p1;
        has_p1_ = true;
    } else {
        has_p1_ = false;
    }
}

void VitaInputSource::read_hardware() {
#ifdef __vita__
    if (!inited_) {
        sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
        inited_ = true;
    }
    SceCtrlData d{};
    if (sceCtrlPeekBufferPositiveExt2(0, &d, 1) > 0) {
        p0_.buttons = d.buttons;
        p0_.lx = d.lx; p0_.ly = d.ly; p0_.rx = d.rx; p0_.ry = d.ry;
        p0_.lt = d.lt; p0_.rt = d.rt;
    }
    SceCtrlData d2{};
    if (sceCtrlPeekBufferPositiveExt2(2, &d2, 1) > 0) {
        p1_.buttons = d2.buttons;
        p1_.lx = d2.lx; p1_.ly = d2.ly; p1_.rx = d2.rx; p1_.ry = d2.ry;
        p1_.lt = d2.lt; p1_.rt = d2.rt;
        has_p1_ = true;
    } else {
        has_p1_ = false;
    }
    SceTouchData t{};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) > 0 && t.reportNum > 0) {
        touch_.active = true;
        touch_.x = t.report[0].x;
        touch_.y = t.report[0].y;
        SceTouchPanelInfo pi{};
        if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &pi) == 0) {
            touch_.max_x = pi.maxAaX;
            touch_.max_y = pi.maxAaY;
        }
    } else {
        touch_.active = false;
    }
#endif
}

InputSnapshot VitaInputSource::poll() {
#ifdef __vita__
    read_hardware();
#endif
    InputSnapshot s = build_snapshot(p0_, has_p1_ ? &p1_ : nullptr, touch_, cfg_);

    if (touch_.active) {
        int dx = 0, dy = 0;
        touch_raw_to_display(touch_.x, touch_.y, touch_.max_x, touch_.max_y, dx, dy);
        int gx = 0, gy = 0;
        touch_display_to_game(dx, dy, gx, gy);
        if (prev_touch_) {
            s.mouse_dx = gx - last_gx_;
            s.mouse_dy = gy - last_gy_;
        } else {
            s.mouse_dx = 0;
            s.mouse_dy = 0;
        }
        s.mouse_buttons |= 0x1u;
        last_gx_ = gx;
        last_gy_ = gy;
        prev_touch_ = true;
    } else {
        prev_touch_ = false;
    }
    return s;
}

}  // namespace hw::dinput
