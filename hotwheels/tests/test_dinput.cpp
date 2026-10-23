// tests/test_dinput.cpp — criterios CA1..CA12 de specs/007-dinput.md (host).
#include "dinput/dinput.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace hw::dinput;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// ---- CA12 ----
static_assert(DI_OK == 0 && DI_NOEFFECT == 1, "DI result");
static_assert(DIERR_NOTACQUIRED == 0x8007000C && DIERR_ACQUIRED == 0x800700AA
    && DIERR_INVALIDPARAM == 0x80070057 && DIERR_NOINTERFACE == 0x80004002
    && DIERR_UNSUPPORTED == 0x80004001 && DIERR_NOTBUFFERED == 0x80040207, "DIERR");
static_assert(DIDEVTYPE_DEVICE == 1 && DIDEVTYPE_MOUSE == 2 && DIDEVTYPE_KEYBOARD == 3
    && DIDEVTYPE_JOYSTICK == 4 && DI8DEVCLASS_ALL == 0, "devtypes");
static_assert(DISCL_EXCLUSIVE == 1 && DISCL_NONEXCLUSIVE == 2 && DISCL_FOREGROUND == 4
    && DISCL_BACKGROUND == 8, "coop");
static_assert(DIPROP_BUFFERSIZE == 1 && DIPROP_AXISMODE == 2 && DIPROP_RANGE == 4
    && DIPROP_DEADZONE == 5 && DIPROP_SATURATION == 6, "props");
static_assert(kKeyboardStateBytes == 256 && kMouseStateBytes == 20 && kJoystickStateBytes == 80, "sizes");
static_assert(slot::di_create_device == 3 && slot::di_enum_devices == 4
    && slot::dev_get_device_state == 9 && slot::dev_get_device_data == 10
    && slot::dev_set_data_format == 11 && slot::dev_set_cooperative_level == 13
    && slot::dev2_poll == 25, "vtable slots");
static_assert(kGuidSysKeyboard.d1 == 0x6F1D2B61u && kGuidSysMouse.d1 == 0x6F1D2B60u
    && kGuidJoystick.d1 == 0x6F1D2B70u, "GUIDs");
static_assert(kIidDirectInputA.d1 == 0x89521360u && kIidDeviceA.d1 == 0x5944E680u
    && kIidDevice2A.d1 == 0x5944E682u, "IIDs");

// ---- helpers ----
struct FakeSource : InputSource {
    InputSnapshot snap;
    int polls = 0;
    InputSnapshot poll() override { ++polls; return snap; }
};

static int32_t rd_i32(const uint8_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

static void wr_u32(uint8_t* p, uint32_t value) {
    std::memcpy(p, &value, sizeof(value));
}

int main() {
    FakeSource src;
    MapperConfig cfg;
    cfg.pad[0].set(Button::DpadUp, dik::UP);
    cfg.pad[0].set(Button::DpadDown, dik::DOWN);
    cfg.pad[0].set(Button::DpadLeft, dik::LEFT);
    cfg.pad[0].set(Button::DpadRight, dik::RIGHT);
    cfg.pad[0].set(Button::Cross, dik::RETURN);
    cfg.pad[0].set(Button::Start, dik::ESCAPE);
    cfg.pad[0].set(Button::Select, dik::PAUSE);
    cfg.pad[0].set(Button::L1, dik::RSHIFT);
    cfg.pad[0].set(Button::L2, dik::RSHIFT);
    cfg.pad[0].set(Button::R1, dik::RCONTROL);
    cfg.pad[0].set(Button::R2, dik::RCONTROL);
    DirectInput di(src, cfg);

    // ---- CA1 ----
    CHECK(di.query_interface(kIidDirectInputA));
    CHECK(di.query_interface(kIidIUnknown));
    CHECK(!di.query_interface(kIidDeviceA));

    // ---- CA2 ----
    Device* kb = di.create_device(kGuidSysKeyboard);
    Device* mouse = di.create_device(kGuidSysMouse);
    Device* joy = di.create_device(kGuidJoystick);
    CHECK(kb && kb->type() == DeviceType::Keyboard);
    CHECK(mouse && mouse->type() == DeviceType::Mouse);
    CHECK(joy && joy->type() == DeviceType::Joystick);
    Guid bogus{0xDEAD, 0xBEEF, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};
    CHECK(di.create_device(bogus) == nullptr);

    // ---- CA3 ----
    std::vector<std::string> names;
    int all = di.enumerate(DI8DEVCLASS_ALL, [&](const Guid&, const char* n) { names.emplace_back(n); return true; });
    CHECK(all == 3 && names.size() == 3);
    int only_kb = di.enumerate(DIDEVTYPE_KEYBOARD, [](const Guid& g, const char*) { return g == kGuidSysKeyboard; });
    CHECK(only_kb == 1);
    int joy_only = di.enumerate(DIDEVTYPE_JOYSTICK, [](const Guid& g, const char*) { return g == kGuidJoystick; });
    CHECK(joy_only == 1);

    // ---- CA4 ----
    CHECK(kb->set_data_format(DataFormat::Keyboard) == DI_OK);
    CHECK(kb->set_data_format(DataFormat::None) == DIERR_INVALIDPARAM);
    CHECK(kb->set_data_format(DataFormat::Keyboard) == DI_OK);
    uint8_t state[kKeyboardStateBytes] = {};
    CHECK(kb->get_device_state(kKeyboardStateBytes, state) == DIERR_NOTACQUIRED);

    // ---- CA5 ----
    src.snap.pad_count = 1;
    src.snap.pads[0].buttons = kPadUp | kPadCross;
    CHECK(kb->acquire() == DI_OK);
    std::memset(state, 0, sizeof(state));
    CHECK(kb->get_device_state(kKeyboardStateBytes, state) == DI_OK);
    CHECK(state[dik::UP] == 0x80 && state[dik::RETURN] == 0x80);
    CHECK(state[dik::DOWN] == 0 && state[dik::LEFT] == 0 && state[dik::RIGHT] == 0);
    src.snap.pads[0].buttons = 0;
    std::memset(state, 0xFF, sizeof(state));
    CHECK(kb->get_device_state(kKeyboardStateBytes, state) == DI_OK);
    CHECK(state[dik::UP] == 0 && state[dik::RETURN] == 0 && state[dik::DOWN] == 0);

    // ---- CA6 ----
    CHECK(kb->set_cooperative_level(DISCL_FOREGROUND | DISCL_NONEXCLUSIVE) == DI_OK);
    CHECK(kb->coop_flags() == (DISCL_FOREGROUND | DISCL_NONEXCLUSIVE));
    int before = src.polls;
    CHECK(kb->acquire() == DI_OK);
    CHECK(src.polls > before);
    CHECK(kb->unacquire() == DI_OK && !kb->acquired());

    // ---- CA7 ----
    CHECK(mouse->set_data_format(DataFormat::Mouse) == DI_OK);
    CHECK(mouse->acquire() == DI_OK);
    src.snap.mouse_dx = 11; src.snap.mouse_dy = -7; src.snap.mouse_dz = 0; src.snap.mouse_buttons = 0x1;
    uint8_t mstate[kMouseStateBytes] = {};
    CHECK(mouse->get_device_state(kMouseStateBytes, mstate) == DI_OK);
    CHECK(rd_i32(mstate) == 11 && rd_i32(mstate + 4) == -7);
    CHECK(mstate[12] == 0x80 && mstate[13] == 0);  // boton izquierdo

    // ---- CA8 ----
    CHECK(joy->query_interface(kIidDevice2A));
    CHECK(joy->query_interface(kIidDeviceA));
    CHECK(joy->set_data_format(DataFormat::Joystick) == DI_OK);
    uint8_t range[24] = {};  // DIPROPHEADER(16) + lMin + lMax
    uint8_t short_range[24] = {};
    wr_u32(short_range, 20);
    CHECK(joy->set_property(DIPROP_RANGE, short_range) == DIERR_INVALIDPARAM);
    CHECK(joy->get_property(DIPROP_RANGE, short_range) == DIERR_INVALIDPARAM);
    wr_u32(range, sizeof(range));
    CHECK(joy->set_property(DIPROP_RANGE, range) == DI_OK);
    uint8_t dz[24] = {};  // DIPROPHEADER(16) + dwData(4) + padding
    uint8_t short_dword[24] = {};
    wr_u32(short_dword, 16);
    CHECK(joy->set_property(DIPROP_DEADZONE, short_dword) == DIERR_INVALIDPARAM);
    CHECK(joy->get_property(DIPROP_DEADZONE, short_dword) == DIERR_INVALIDPARAM);
    wr_u32(dz, sizeof(dz));
    CHECK(joy->set_property(DIPROP_DEADZONE, dz) == DI_OK);
    uint8_t short_property[20] = {};
    uint8_t dword_property[20] = {};
    wr_u32(short_property, 16);
    wr_u32(dword_property, sizeof(dword_property));
    CHECK(joy->set_property(DIPROP_SATURATION, short_property) == DIERR_INVALIDPARAM);
    CHECK(joy->get_property(DIPROP_SATURATION, short_property) == DIERR_INVALIDPARAM);
    wr_u32(dword_property + 16, 75);
    CHECK(joy->set_property(DIPROP_SATURATION, dword_property) == DI_OK);
    std::memset(dword_property, 0, sizeof(dword_property));
    wr_u32(dword_property, sizeof(dword_property));
    CHECK(joy->get_property(DIPROP_SATURATION, dword_property) == DI_OK);
    CHECK(rd_i32(dword_property + 16) == 75);
    CHECK(joy->set_property(DIPROP_BUFFERSIZE, short_property) == DIERR_INVALIDPARAM);
    CHECK(joy->set_property(DIPROP_BUFFERSIZE, dword_property) == DI_OK);
    CHECK(joy->set_property(DIPROP_AXISMODE, short_property) == DIERR_INVALIDPARAM);
    CHECK(joy->set_property(DIPROP_AXISMODE, dword_property) == DI_OK);
    uint8_t got_range[24] = {};
    wr_u32(got_range, sizeof(got_range));
    CHECK(joy->get_property(DIPROP_RANGE, got_range) == DI_OK);
    uint8_t got_dz[20] = {};
    wr_u32(got_dz, sizeof(got_dz));
    CHECK(joy->get_property(DIPROP_DEADZONE, got_dz) == DI_OK);
    CHECK(joy->acquire() == DI_OK);
    src.snap.pad_count = 1;
    src.snap.pads[0].lx = 20000; src.snap.pads[0].ly = -20000;
    src.snap.pads[0].rx = -10000; src.snap.pads[0].ry = 10000;
    src.snap.pads[0].pov = 9000;
    src.snap.pads[0].buttons = kPadCross;
    int before_poll = src.polls;
    CHECK(joy->poll() == DI_OK);
    CHECK(src.polls > before_poll);
    uint8_t jstate[kJoystickStateBytes] = {};
    CHECK(joy->get_device_state(kJoystickStateBytes, jstate) == DI_OK);
    CHECK(rd_i32(jstate) == 20000 && rd_i32(jstate + 4) == -20000);
    CHECK(rd_i32(jstate + 12) == -10000 && rd_i32(jstate + 16) == 10000);
    CHECK(rd_i32(jstate + 32) == 9000);
    CHECK(jstate[48] == 0x80);  // Cross -> boton 0

    // ---- CA9 ----
    uint32_t inout = 8;
    uint8_t events[128] = {};
    CHECK(joy->get_device_data(16, events, &inout, 0) == DIERR_NOTBUFFERED);
    CHECK(kb->acquire() == DI_OK);  // doble Acquire no rompe
    CHECK(kb->acquire() == DI_OK);

    // ---- CA10 ----
    di.config().pad[0].set(Button::Cross, dik::SPACE);
    src.snap.pad_count = 1;
    src.snap.pads[0].buttons = kPadCross;
    std::memset(state, 0, sizeof(state));
    CHECK(kb->get_device_state(kKeyboardStateBytes, state) == DI_OK);
    CHECK(state[dik::SPACE] == 0x80 && state[dik::RETURN] == 0);

    // ---- CA14 (BUG-004): cada boton fisico de HW-024 emite su DIK ----
    {
        const struct { uint32_t bit; uint8_t dik_code; } cases[] = {
            {kPadStart, dik::ESCAPE}, {kPadSelect, dik::PAUSE},
            {kPadL, dik::RSHIFT}, {kPadL2, dik::RSHIFT},
            {kPadR, dik::RCONTROL}, {kPadR2, dik::RCONTROL},
        };
        src.snap.pad_count = 1;
        for (const auto& tc : cases) {
            src.snap.pads[0].buttons = tc.bit;
            std::memset(state, 0, sizeof(state));
            CHECK(kb->get_device_state(kKeyboardStateBytes, state) == DI_OK);
            CHECK(state[tc.dik_code] == 0x80);
        }
        src.snap.pads[0].buttons = 0;
    }

    // ---- CA11 ----
    uint32_t axes = 0, buttons = 0, povs = 0;
    CHECK(kb->get_capabilities(axes, buttons, povs) == DI_OK && axes == 0 && povs == 0);
    CHECK(joy->get_capabilities(axes, buttons, povs) == DI_OK && axes == 6 && buttons == 32 && povs == 4);
    CHECK(mouse->get_capabilities(axes, buttons, povs) == DI_OK && axes == 3 && buttons == 4);
    Guid gi{}; std::string nm;
    CHECK(kb->get_device_info(gi, nm) == DI_OK && gi == kGuidSysKeyboard && !nm.empty());
    CHECK(joy->get_device_info(gi, nm) == DI_OK && gi == kGuidJoystick && !nm.empty());

    if (g_fail) {
        std::printf("DINPUT: %d fallas\n", g_fail);
        return 1;
    }
    std::printf("DINPUT: OK (CA1..CA12, CA14)\n");
    return 0;
}
