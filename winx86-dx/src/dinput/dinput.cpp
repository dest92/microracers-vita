// DirectInput portable implementation for PE32/i386 Win32 guests.
#include "../../include/winx86-dx/dinput/dinput.h"

#include <algorithm>
#include <cstring>

namespace winx86::dx::dinput {
namespace {

int32_t rd_i32(const uint8_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

void wr_i32(uint8_t* p, int32_t v) { std::memcpy(p, &v, 4); }

uint32_t property_header_min_size(uintptr_t prop, bool get) {
    if (prop == DIPROP_RANGE) return 24;  // DIPROPHEADER + lMin/lMax
    if (prop == DIPROP_DEADZONE || prop == DIPROP_SATURATION) return 20;
    if (!get && (prop == DIPROP_BUFFERSIZE || prop == DIPROP_AXISMODE)) return 20;
    return 0;
}

bool property_header_valid(const void* header, uint32_t min_size) {
    return header && static_cast<uint32_t>(rd_i32(static_cast<const uint8_t*>(header))) >= min_size;
}

}  // namespace

DirectInput::DirectInput(InputSource& src) : src_(src) {}
DirectInput::DirectInput(InputSource& src, const MapperConfig& cfg) : src_(src), cfg_(cfg) {}
DirectInput::~DirectInput() = default;

bool DirectInput::query_interface(const Guid& iid) const {
    return iid == kIidDirectInputA || iid == kIidIUnknown;
}

void DirectInput::refresh() { snap_ = src_.poll(); }

Device* DirectInput::create_device(const Guid& guid) {
    DeviceType type;
    if (guid == kGuidSysKeyboard) type = DeviceType::Keyboard;
    else if (guid == kGuidSysMouse) type = DeviceType::Mouse;
    else if (guid == kGuidJoystick) type = DeviceType::Joystick;
    else return nullptr;

    int joy_index = 0;
    for (const auto& d : devices_)
        if (d->type_ == DeviceType::Joystick) ++joy_index;
    devices_.push_back(std::unique_ptr<Device>(new Device(type, guid, *this, joy_index)));
    return devices_.back().get();
}

bool DirectInput::release_device(Device* device) {
    const auto it = std::find_if(devices_.begin(), devices_.end(),
        [device](const auto& owned) { return owned.get() == device; });
    if (it == devices_.end()) return false;
    devices_.erase(it);
    return true;
}

int DirectInput::enumerate(uint32_t devtype,
    const std::function<bool(const Guid&, const char*)>& cb) const {
    struct Entry {
        uint32_t type;
        const Guid* guid;
        const char* name;
    };
    static const Entry list[] = {
        {DIDEVTYPE_KEYBOARD, &kGuidSysKeyboard, "Keyboard"},
        {DIDEVTYPE_MOUSE, &kGuidSysMouse, "Mouse"},
        {DIDEVTYPE_JOYSTICK, &kGuidJoystick, "Joystick"},
    };
    const bool all = devtype == DI8DEVCLASS_ALL || devtype == DIDEVTYPE_DEVICE;
    int count = 0;
    for (const auto& e : list) {
        if (!all && e.type != devtype) continue;
        ++count;
        if (!cb(*e.guid, e.name)) break;
    }
    return count;
}

Device::Device(DeviceType t, const Guid& g, DirectInput& owner, int joy_index)
    : owner_(owner), type_(t), guid_(g), joy_index_(joy_index) {}

const char* Device::product_name() const {
    switch (type_) {
    case DeviceType::Keyboard: return "Keyboard";
    case DeviceType::Mouse: return "Mouse";
    case DeviceType::Joystick: return "Joystick";
    }
    return "Unknown";
}

bool Device::query_interface(const Guid& iid) const {
    return iid == kIidIUnknown || iid == kIidDeviceA || iid == kIidDevice2A;
}

uint32_t Device::set_data_format(DataFormat f) {
    if (f == DataFormat::None) return DIERR_INVALIDPARAM;
    format_ = f;
    return DI_OK;
}

uint32_t Device::set_cooperative_level(uint32_t flags) {
    coop_ = flags;
    return DI_OK;
}

uint32_t Device::acquire() {
    owner_.refresh();
    acquired_ = true;
    return DI_OK;
}

uint32_t Device::unacquire() {
    acquired_ = false;
    return DI_OK;
}

uint32_t Device::poll() {
    owner_.refresh();
    return DI_OK;
}

uint32_t Device::get_device_state(uint32_t cb, void* data) {
    if (!acquired_) return DIERR_NOTACQUIRED;
    if (!data) return DIERR_INVALIDPARAM;
    // Teclado/mouse son inmediatos (re-engine §5); el joystick usa el ultimo Poll.
    if (format_ == DataFormat::Keyboard || format_ == DataFormat::Mouse) owner_.refresh();
    const InputSnapshot& s = owner_.snapshot();

    switch (format_) {
    case DataFormat::Keyboard: {
        if (cb < kKeyboardStateBytes) return DIERR_INVALIDPARAM;
        uint8_t* out = static_cast<uint8_t*>(data);
        std::memset(out, 0, kKeyboardStateBytes);
        for (int i = 0; i < 256; ++i)
            if (s.keys[i] & 0x80) out[i] = 0x80;
        const int npads = s.pad_count < 2 ? s.pad_count : 2;
        const int dz = owner_.config().stick_deadzone;
        for (int p = 0; p < npads; ++p) {
            const PadState& pad = s.pads[p];
            const ButtonMap& m = owner_.config().pad[p];
            auto press = [&](Button b, bool on) {
                if (!on) return;
                const uint8_t k = m.get(b);
                if (k) out[k] = 0x80;
            };
            press(Button::DpadUp, pad.buttons & kPadUp);
            press(Button::DpadDown, pad.buttons & kPadDown);
            press(Button::DpadLeft, pad.buttons & kPadLeft);
            press(Button::DpadRight, pad.buttons & kPadRight);
            press(Button::Cross, pad.buttons & kPadCross);
            press(Button::Circle, pad.buttons & kPadCircle);
            press(Button::Square, pad.buttons & kPadSquare);
            press(Button::Triangle, pad.buttons & kPadTriangle);
            press(Button::LStickUp, pad.ly <= -dz);
            press(Button::LStickDown, pad.ly >= dz);
            press(Button::LStickLeft, pad.lx <= -dz);
            press(Button::LStickRight, pad.lx >= dz);
            press(Button::RStickUp, pad.ry <= -dz);
            press(Button::RStickDown, pad.ry >= dz);
            press(Button::RStickLeft, pad.rx <= -dz);
            press(Button::RStickRight, pad.rx >= dz);
            // Emitir todos los botones físicos configurados, no sólo los de cara.
            press(Button::L1, pad.buttons & kPadL);
            press(Button::R1, pad.buttons & kPadR);
            press(Button::L2, pad.buttons & kPadL2);
            press(Button::R2, pad.buttons & kPadR2);
            press(Button::Start, pad.buttons & kPadStart);
            press(Button::Select, pad.buttons & kPadSelect);
        }
        return DI_OK;
    }
    case DataFormat::Mouse: {
        if (cb < kMouseStateBytes) return DIERR_INVALIDPARAM;
        int32_t dx = s.mouse_dx, dy = s.mouse_dy;
        if (owner_.config().mouse_from_right_stick && s.pad_count > 0
            && owner_.config().mouse_scale > 0) {
            dx += s.pads[0].rx / owner_.config().mouse_scale;
            dy += s.pads[0].ry / owner_.config().mouse_scale;
        }
        uint8_t* out = static_cast<uint8_t*>(data);
        std::memset(out, 0, kMouseStateBytes);
        wr_i32(out + 0, dx);
        wr_i32(out + 4, dy);
        wr_i32(out + 8, s.mouse_dz);
        out[12] = (s.mouse_buttons & 0x1) ? 0x80 : 0;
        out[13] = (s.mouse_buttons & 0x2) ? 0x80 : 0;
        out[14] = (s.mouse_buttons & 0x4) ? 0x80 : 0;
        return DI_OK;
    }
    case DataFormat::Joystick: {
        if (cb < kJoystickStateBytes) return DIERR_INVALIDPARAM;
        uint8_t* out = static_cast<uint8_t*>(data);
        std::memset(out, 0, kJoystickStateBytes);
        const PadState* pad = nullptr;
        if (s.pad_count > joy_index_) pad = &s.pads[joy_index_];
        else if (s.pad_count > 0) pad = &s.pads[0];
        if (pad) {
            wr_i32(out + 0, pad->lx);
            wr_i32(out + 4, pad->ly);
            wr_i32(out + 8, 0);
            wr_i32(out + 12, pad->rx);
            wr_i32(out + 16, pad->ry);
            wr_i32(out + 20, 0);
            wr_i32(out + 32, pad->pov);  // rgdwPOV[0]
            auto btn = [&](int idx, uint32_t bit) { if (pad->buttons & bit) out[48 + idx] = 0x80; };
            btn(0, kPadCross); btn(1, kPadCircle); btn(2, kPadSquare); btn(3, kPadTriangle);
            btn(4, kPadL); btn(5, kPadR); btn(6, kPadL2); btn(7, kPadR2);
            btn(8, kPadStart); btn(9, kPadSelect);
        }
        return DI_OK;
    }
    case DataFormat::None:
    default:
        return DIERR_INVALIDPARAM;
    }
}

uint32_t Device::get_device_data(uint32_t, void*, uint32_t*, uint32_t) {
    return DIERR_NOTBUFFERED;
}

uint32_t Device::get_capabilities(uint32_t& axes, uint32_t& buttons, uint32_t& povs) const {
    axes = buttons = povs = 0;
    switch (type_) {
    case DeviceType::Keyboard: buttons = 256; break;
    case DeviceType::Mouse: axes = 3; buttons = 4; break;
    case DeviceType::Joystick: axes = 6; buttons = 32; povs = 4; break;
    }
    return DI_OK;
}

uint32_t Device::set_property(uintptr_t prop, const void* header) {
    const uint32_t min_size = property_header_min_size(prop, false);
    if (min_size && !property_header_valid(header, min_size)) return DIERR_INVALIDPARAM;
    switch (prop) {
    case DIPROP_RANGE:
        if (header) {
            const uint8_t* p = static_cast<const uint8_t*>(header);
            range_min_ = rd_i32(p + 16);
            range_max_ = rd_i32(p + 20);
            range_set_ = true;
        }
        return DI_OK;
    case DIPROP_DEADZONE:
        if (header) {
            deadzone_ = rd_i32(static_cast<const uint8_t*>(header) + 16);
            deadzone_set_ = true;
        }
        return DI_OK;
    case DIPROP_SATURATION:
        if (header) saturation_ = rd_i32(static_cast<const uint8_t*>(header) + 16);
        return DI_OK;
    case DIPROP_BUFFERSIZE:
    case DIPROP_AXISMODE:
        return DI_OK;
    default:
        return DIERR_UNSUPPORTED;
    }
}

uint32_t Device::get_property(uintptr_t prop, void* header) const {
    const uint32_t min_size = property_header_min_size(prop, true);
    if (min_size && !property_header_valid(header, min_size)) return DIERR_INVALIDPARAM;
    if (!header) return DIERR_INVALIDPARAM;
    uint8_t* p = static_cast<uint8_t*>(header);
    switch (prop) {
    case DIPROP_RANGE:
        wr_i32(p + 16, range_min_);
        wr_i32(p + 20, range_max_);
        return DI_OK;
    case DIPROP_DEADZONE:
        wr_i32(p + 16, deadzone_);
        return DI_OK;
    case DIPROP_SATURATION:
        wr_i32(p + 16, saturation_);
        return DI_OK;
    default:
        return DIERR_UNSUPPORTED;
    }
}

uint32_t Device::get_device_info(Guid& guid, std::string& name) const {
    guid = guid_;
    name = product_name();
    return DI_OK;
}

}  // namespace winx86::dx::dinput
