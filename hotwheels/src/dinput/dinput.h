// Compatibilidad temporal: el core DirectInput portable vive en winx86-dx.
#pragma once
#include "../../../winx86-dx/include/winx86-dx/dinput/dinput.h"

namespace hw { namespace dinput {
using ::winx86::dx::dinput::DI_OK;
using ::winx86::dx::dinput::DI_NOEFFECT;
using ::winx86::dx::dinput::DIERR_NOTACQUIRED;
using ::winx86::dx::dinput::DIERR_ACQUIRED;
using ::winx86::dx::dinput::DIERR_INVALIDPARAM;
using ::winx86::dx::dinput::DIERR_NOINTERFACE;
using ::winx86::dx::dinput::DIERR_UNSUPPORTED;
using ::winx86::dx::dinput::DIERR_NOTBUFFERED;
using ::winx86::dx::dinput::DIDEVTYPE_DEVICE;
using ::winx86::dx::dinput::DIDEVTYPE_MOUSE;
using ::winx86::dx::dinput::DIDEVTYPE_KEYBOARD;
using ::winx86::dx::dinput::DIDEVTYPE_JOYSTICK;
using ::winx86::dx::dinput::DI8DEVCLASS_ALL;
using ::winx86::dx::dinput::DIEDFL_ATTACHEDONLY;
using ::winx86::dx::dinput::DISCL_EXCLUSIVE;
using ::winx86::dx::dinput::DISCL_NONEXCLUSIVE;
using ::winx86::dx::dinput::DISCL_FOREGROUND;
using ::winx86::dx::dinput::DISCL_BACKGROUND;
using ::winx86::dx::dinput::DISCL_NOWINKEY;
using ::winx86::dx::dinput::DIPROP_BUFFERSIZE;
using ::winx86::dx::dinput::DIPROP_AXISMODE;
using ::winx86::dx::dinput::DIPROP_RANGE;
using ::winx86::dx::dinput::DIPROP_DEADZONE;
using ::winx86::dx::dinput::DIPROP_SATURATION;
using ::winx86::dx::dinput::Guid;
using ::winx86::dx::dinput::kGuidSysMouse;
using ::winx86::dx::dinput::kGuidSysKeyboard;
using ::winx86::dx::dinput::kGuidJoystick;
using ::winx86::dx::dinput::kIidIUnknown;
using ::winx86::dx::dinput::kIidDirectInputA;
using ::winx86::dx::dinput::kIidDeviceA;
using ::winx86::dx::dinput::kIidDevice2A;
using ::winx86::dx::dinput::DeviceType;
using ::winx86::dx::dinput::DataFormat;
using ::winx86::dx::dinput::kKeyboardStateBytes;
using ::winx86::dx::dinput::kMouseStateBytes;
using ::winx86::dx::dinput::kJoystickStateBytes;
using ::winx86::dx::dinput::kPadUp;
using ::winx86::dx::dinput::kPadDown;
using ::winx86::dx::dinput::kPadLeft;
using ::winx86::dx::dinput::kPadRight;
using ::winx86::dx::dinput::kPadCross;
using ::winx86::dx::dinput::kPadCircle;
using ::winx86::dx::dinput::kPadSquare;
using ::winx86::dx::dinput::kPadTriangle;
using ::winx86::dx::dinput::kPadL;
using ::winx86::dx::dinput::kPadR;
using ::winx86::dx::dinput::kPadL2;
using ::winx86::dx::dinput::kPadR2;
using ::winx86::dx::dinput::kPadStart;
using ::winx86::dx::dinput::kPadSelect;
using ::winx86::dx::dinput::PadState;
using ::winx86::dx::dinput::InputSnapshot;
using ::winx86::dx::dinput::Button;
using ::winx86::dx::dinput::bi;
using ::winx86::dx::dinput::kButtonCount;
using ::winx86::dx::dinput::ButtonMap;
using ::winx86::dx::dinput::MapperConfig;
namespace dik = ::winx86::dx::dinput::dik;
namespace slot = ::winx86::dx::dinput::slot;
// Mapeo por juego (HW-024). El core winx86-dx deja ButtonMap vacío: todo
// DirectInput de Hot Wheels debe construirse con esto (BUG-046).
::winx86::dx::dinput::ButtonMap hotwheels_button_map_for_pad(int pad);
::winx86::dx::dinput::MapperConfig hotwheels_mapper(bool swapped = false);
using ::winx86::dx::dinput::Device;
using ::winx86::dx::dinput::DirectInput;
using InputSource = ::winx86::dx::dinput::InputSource;
} }
