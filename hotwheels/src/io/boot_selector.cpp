// src/io/boot_selector.cpp — selector de arranque (HW-020).
//
// Vita: pantalla propia con sceDisplay (sin vitaGL/GXM) y fuente 8x16 generada
// por winx86 (`platform/vita_kb_font.h`). Host: no-op (los CA del modelo están
// en tests/test_io.cpp sobre BootMenu).
#include "io/boot_selector.h"

#if defined(__vita__)

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdlib>
#include <cstring>

#include "io/boot_menu.h"
#include "io/config.h"
#include "platform/vita_kb_font.h"  // g_kb_font95[95][16], generado por winx86

namespace hw::io {
namespace {

constexpr int W = 960, H = 544;
constexpr uint32_t kBg = 0xFF101418u, kFg = 0xFFE8ECF0u, kSel = 0xFF30C8FFu, kAccent = 0xFFC81E1Eu;
alignas(64) uint32_t g_fb[W * H];

void draw_char(uint32_t* fb, int x, int y, char ch, uint32_t col, int s) {
    if (ch < 32 || ch > 126) return;
    const unsigned char* g = g_kb_font95[(int)ch - 32];
    for (int row = 0; row < 16; ++row) {
        const unsigned bits = g[row];
        for (int c = 0; c < 8; ++c) {
            if (!(bits & (0x80u >> c))) continue;
            for (int sy = 0; sy < s; ++sy)
                for (int sx = 0; sx < s; ++sx) {
                    const int px = x + c * s + sx, py = y + row * s + sy;
                    if (px >= 0 && px < W && py >= 0 && py < H) fb[(size_t)py * W + px] = col;
                }
        }
    }
}
void draw_text(uint32_t* fb, int x, int y, const char* txt, uint32_t col, int s) {
    for (; *txt; ++txt) { draw_char(fb, x, y, *txt, col, s); x += 8 * s; }
}

}  // namespace

bool boot_should_open() {
    const char* e = std::getenv("HW_CFG");
    if (e && *e && *e != '0') return true;
    SceCtrlData p;
    std::memset(&p, 0, sizeof p);
    if (sceCtrlPeekBufferPositive(0, &p, 1) < 0) return false;
    return (p.buttons & SCE_CTRL_LTRIGGER) != 0;  // "L" = gatillo izquierdo
}

int run_boot_selector(const std::string& ini_path) {
    HwVitaConfig cfg;
    load(ini_path, cfg);
    BootMenu menu(cfg);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    SceDisplayFrameBuf fbp;
    std::memset(&fbp, 0, sizeof fbp);
    fbp.size = sizeof fbp;
    fbp.base = g_fb;
    fbp.pitch = W;
    fbp.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fbp.width = W;
    fbp.height = H;

    for (;;) {
        std::memset(g_fb, 0, sizeof g_fb);
        for (int x = 0; x < W; ++x) { g_fb[x] = kAccent; g_fb[(size_t)(H - 1) * W + x] = kAccent; }
        const auto lines = menu.lines();
        int y = 60;
        for (size_t i = 0; i < lines.size(); ++i) {
            const int s = (i == 0) ? 3 : 2;
            const uint32_t col = (i == 0) ? kFg : (lines[i][0] == '>' ? kSel : kFg);
            draw_text(g_fb, 48, y, lines[i].c_str(), col, s);
            y += (i == 0) ? 64 : 40;
        }
        sceDisplaySetFrameBuf(&fbp, SCE_DISPLAY_SETBUF_NEXTFRAME);
        sceKernelDelayThread(30000);

        // Conservar el estado presionado mientras esperamos que se suelte.
        SceCtrlData pressed;
        do {
            std::memset(&pressed, 0, sizeof pressed);
            sceCtrlPeekBufferPositive(0, &pressed, 1);
            if (!pressed.buttons) sceKernelDelayThread(30000);
        } while (!pressed.buttons);
        SceCtrlData released{};
        do { sceKernelDelayThread(40000); sceCtrlPeekBufferPositive(0, &released, 1); }
        while (released.buttons);

        MenuAction a;
        if (pressed.buttons & SCE_CTRL_UP) a = MenuAction::Up;
        else if (pressed.buttons & SCE_CTRL_DOWN) a = MenuAction::Down;
        else if (pressed.buttons & SCE_CTRL_LEFT) a = MenuAction::Left;
        else if (pressed.buttons & SCE_CTRL_RIGHT) a = MenuAction::Right;
        else if (pressed.buttons & SCE_CTRL_CROSS) a = MenuAction::Confirm;
        else if (pressed.buttons & SCE_CTRL_CIRCLE) a = MenuAction::Cancel;
        else continue;

        const MenuResult r = menu.handle(a);
        if (r == MenuResult::SaveAndExit) { menu.save(ini_path); return 1; }
        if (r == MenuResult::CancelExit) return 0;
    }
}

}  // namespace hw::io

#else  // host: no-op

#include <cstdlib>

namespace hw::io {

bool boot_should_open() {
    const char* e = std::getenv("HW_CFG");
    return e && *e && *e != '0';
}

int run_boot_selector(const std::string& /*ini_path*/) { return 0; }

}  // namespace hw::io

#endif
