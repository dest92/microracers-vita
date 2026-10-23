// DirectInput portable API for PE32/i386 Win32 guests.
//
// Modelo COM de IDirectInputA / IDirectInputDevice(A|2A) con una fuente de
// input abstracta. El backend real (Vita: sceCtrl/sceTouch) implementa
// InputSource; el mapeo boton->DIK_* es configurable.
//
// Constantes/GUIDs/IIDs/vtable order segun MinGW dinput.h y el patron COM de
// d2vita/D2Vita/third_party/winx86/src/runtime/ds_emul.cpp (shims stdcall con
// `this` primero).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace winx86::dx::dinput {

// ---- HRESULTs (dinput.h) ----
constexpr uint32_t DI_OK = 0x00000000u;
constexpr uint32_t DI_NOEFFECT = 0x00000001u;
constexpr uint32_t DIERR_NOTACQUIRED = 0x8007000Cu;    // ERROR_INVALID_ACCESS
constexpr uint32_t DIERR_ACQUIRED = 0x800700AAu;       // ERROR_BUSY
constexpr uint32_t DIERR_INVALIDPARAM = 0x80070057u;   // E_INVALIDARG
constexpr uint32_t DIERR_NOINTERFACE = 0x80004002u;    // E_NOINTERFACE
constexpr uint32_t DIERR_UNSUPPORTED = 0x80004001u;    // E_NOTIMPL
constexpr uint32_t DIERR_NOTBUFFERED = 0x80040207u;

// ---- tipos y flags ----
constexpr uint32_t DIDEVTYPE_DEVICE = 1;
constexpr uint32_t DIDEVTYPE_MOUSE = 2;
constexpr uint32_t DIDEVTYPE_KEYBOARD = 3;
constexpr uint32_t DIDEVTYPE_JOYSTICK = 4;
constexpr uint32_t DI8DEVCLASS_ALL = 0;
constexpr uint32_t DIEDFL_ATTACHEDONLY = 0x00000001u;

constexpr uint32_t DISCL_EXCLUSIVE = 0x00000001u;
constexpr uint32_t DISCL_NONEXCLUSIVE = 0x00000002u;
constexpr uint32_t DISCL_FOREGROUND = 0x00000004u;
constexpr uint32_t DISCL_BACKGROUND = 0x00000008u;
constexpr uint32_t DISCL_NOWINKEY = 0x00000010u;

// Propiedades (id = valor entero del REFGUID, como MAKEDIPROP en Win32).
constexpr uintptr_t DIPROP_BUFFERSIZE = 1;
constexpr uintptr_t DIPROP_AXISMODE = 2;
constexpr uintptr_t DIPROP_RANGE = 4;
constexpr uintptr_t DIPROP_DEADZONE = 5;
constexpr uintptr_t DIPROP_SATURATION = 6;

// ---- GUIDs/IIDs ----
struct Guid {
    uint32_t d1;
    uint16_t d2;
    uint16_t d3;
    uint8_t d4[8];
};
constexpr bool operator==(const Guid& a, const Guid& b) {
    return a.d1 == b.d1 && a.d2 == b.d2 && a.d3 == b.d3
        && a.d4[0] == b.d4[0] && a.d4[1] == b.d4[1] && a.d4[2] == b.d4[2] && a.d4[3] == b.d4[3]
        && a.d4[4] == b.d4[4] && a.d4[5] == b.d4[5] && a.d4[6] == b.d4[6] && a.d4[7] == b.d4[7];
}
constexpr bool operator!=(const Guid& a, const Guid& b) { return !(a == b); }

inline constexpr Guid kGuidSysMouse{0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr Guid kGuidSysKeyboard{0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr Guid kGuidJoystick{0x6F1D2B70, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr Guid kIidIUnknown{0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
inline constexpr Guid kIidDirectInputA{0x89521360, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr Guid kIidDeviceA{0x5944E680, 0xC92E, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr Guid kIidDevice2A{0x5944E682, 0xC92E, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// ---- formatos de datos ----
enum class DeviceType { Keyboard, Mouse, Joystick };
enum class DataFormat { None, Keyboard, Mouse, Joystick };

constexpr uint32_t kKeyboardStateBytes = 256;
constexpr uint32_t kMouseStateBytes = 20;    // DIMOUSESTATE
constexpr uint32_t kJoystickStateBytes = 80; // DIJOYSTATE

// ---- fuente abstracta de input ----
// bits de PadState::buttons
constexpr uint32_t kPadUp = 1u << 0, kPadDown = 1u << 1, kPadLeft = 1u << 2, kPadRight = 1u << 3;
constexpr uint32_t kPadCross = 1u << 4, kPadCircle = 1u << 5, kPadSquare = 1u << 6, kPadTriangle = 1u << 7;
constexpr uint32_t kPadL = 1u << 8, kPadR = 1u << 9, kPadL2 = 1u << 10, kPadR2 = 1u << 11;
constexpr uint32_t kPadStart = 1u << 12, kPadSelect = 1u << 13;

struct PadState {
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;  // -32767..32767
    uint32_t buttons = 0;
    int32_t pov = -1;  // centesimas de grado; -1 = centrado
};
struct InputSnapshot {
    uint8_t keys[256] = {};  // DIK crudo opcional (0x80 = presionada)
    int32_t mouse_dx = 0, mouse_dy = 0, mouse_dz = 0;
    uint32_t mouse_buttons = 0;  // bit0 left, bit1 right, bit2 middle
    int pad_count = 0;
    PadState pads[2];
};
struct InputSource {
    // Prestada por DirectInput; debe vivir más que éste. poll() se llama desde
    // el hilo consumidor; esta interfaz no añade sincronización.
    virtual ~InputSource() = default;
    virtual InputSnapshot poll() = 0;
};

// ---- mapeo boton -> DIK ----
enum class Button {
    DpadUp, DpadDown, DpadLeft, DpadRight,
    Cross, Circle, Square, Triangle, L1, L2, R1, R2, Start, Select,
    LStickUp, LStickDown, LStickLeft, LStickRight,
    RStickUp, RStickDown, RStickLeft, RStickRight,
    Count
};
inline int bi(Button b) { return static_cast<int>(b); }
constexpr int kButtonCount = static_cast<int>(Button::Count);

struct ButtonMap {
    uint8_t dik[kButtonCount] = {};
    void set(Button b, uint8_t code) { dik[bi(b)] = code; }
    uint8_t get(Button b) const { return dik[bi(b)]; }
};

struct MapperConfig {
    ButtonMap pad[2];
    bool mouse_from_right_stick = true;
    int32_t stick_deadzone = 8000;  // 0..32767
    int32_t mouse_scale = 4096;     // divisor stick -> delta de mouse
};

// scancodes DIK usados por los defaults (dinput.h).
namespace dik {
constexpr uint8_t ESCAPE = 0x01, BACK = 0x0E, TAB = 0x0F, T = 0x14;
constexpr uint8_t RETURN = 0x1C, LCONTROL = 0x1D;
constexpr uint8_t F = 0x21, G = 0x22, H = 0x23, LSHIFT = 0x2A, Z = 0x2C, X = 0x2D;
constexpr uint8_t RSHIFT = 0x36, SPACE = 0x39, RCONTROL = 0x9D;
constexpr uint8_t PAUSE = 0xC5, UP = 0xC8, LEFT = 0xCB, RIGHT = 0xCD, DOWN = 0xD0;
}  // namespace dik

// Indices de los slots COM contados desde IUnknown (0-based); los consume el
// futuro shim winx86 al armar las vtables (patron ds_emul.cpp).
namespace slot {
constexpr unsigned di_create_device = 3, di_enum_devices = 4, di_get_device_status = 5;
constexpr unsigned dev_get_capabilities = 3, dev_get_property = 5, dev_set_property = 6,
                   dev_acquire = 7, dev_unacquire = 8, dev_get_device_state = 9,
                   dev_get_device_data = 10, dev_set_data_format = 11,
                   dev_set_event_notification = 12, dev_set_cooperative_level = 13,
                   dev_get_object_info = 14, dev_get_device_info = 15;
constexpr unsigned dev2_create_effect = 18, dev2_poll = 25, dev2_send_device_data = 26;
}  // namespace slot

// ---- objetos COM ----
struct Device;
class DirectInput {
public:
    explicit DirectInput(InputSource& src);
    DirectInput(InputSource& src, const MapperConfig& cfg);
    ~DirectInput();
    DirectInput(const DirectInput&) = delete;
    DirectInput& operator=(const DirectInput&) = delete;
    DirectInput(DirectInput&&) = delete;
    DirectInput& operator=(DirectInput&&) = delete;

    bool query_interface(const Guid& iid) const;  // kIidDirectInputA / kIidIUnknown

    // IDirectInputA::CreateDevice (guid desconocido -> nullptr)
    Device* create_device(const Guid& guid);
    // Destruye el objeto cuando su referencia COM llega a cero.
    bool release_device(Device* device);
    // IDirectInputA::EnumDevices; devuelve la cantidad recorrida (cb puede cortar con false)
    int enumerate(uint32_t devtype, const std::function<bool(const Guid&, const char*)>& cb) const;

    MapperConfig& config() { return cfg_; }
    InputSnapshot& snapshot() { return snap_; }

private:
    friend struct Device;
    void refresh();
    InputSource& src_;
    MapperConfig cfg_;
    std::vector<std::unique_ptr<Device>> devices_;
    InputSnapshot snap_;
};

struct Device {
    DeviceType type() const { return type_; }
    const Guid& instance_guid() const { return guid_; }
    const char* product_name() const;
    Guid instance_guid_value() const { return guid_; }

    bool query_interface(const Guid& iid) const;

    // IDirectInputDeviceA
    uint32_t set_data_format(DataFormat f);
    uint32_t set_cooperative_level(uint32_t flags);
    uint32_t acquire();
    uint32_t unacquire();
    uint32_t get_device_state(uint32_t cb, void* data);
    uint32_t get_device_data(uint32_t cb, void* data, uint32_t* inout, uint32_t flags);
    uint32_t get_capabilities(uint32_t& axes, uint32_t& buttons, uint32_t& povs) const;
    uint32_t get_property(uintptr_t prop, void* header) const;
    uint32_t set_property(uintptr_t prop, const void* header);
    uint32_t get_device_info(Guid& guid, std::string& name) const;

    // IDirectInputDevice2A
    uint32_t poll();

    bool acquired() const { return acquired_; }
    uint32_t coop_flags() const { return coop_; }
    DataFormat data_format() const { return format_; }

private:
    friend class DirectInput;
    Device(DeviceType t, const Guid& g, DirectInput& owner, int joy_index);
    DirectInput& owner_;
    DeviceType type_;
    Guid guid_;
    int joy_index_ = 0;
    DataFormat format_ = DataFormat::None;
    uint32_t coop_ = 0;
    bool acquired_ = false;
    bool range_set_ = false, deadzone_set_ = false;
    int32_t range_min_ = -32767, range_max_ = 32767, deadzone_ = 0, saturation_ = 10000;
};

}  // namespace winx86::dx::dinput
