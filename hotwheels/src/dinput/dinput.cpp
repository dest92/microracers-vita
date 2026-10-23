// Compatibilidad temporal para los builds existentes de Hot Wheels.
#include "../../../winx86-dx/src/dinput/dinput.cpp"

#include "dinput/dinput.h"

namespace hw { namespace dinput {
using ::winx86::dx::dinput::ButtonMap;
using ::winx86::dx::dinput::MapperConfig;

ButtonMap hotwheels_button_map_for_pad(int pad) {
    using ::winx86::dx::dinput::ButtonMap;
    using ::winx86::dx::dinput::dik::BACK;
    using ::winx86::dx::dinput::dik::DOWN;
    using ::winx86::dx::dinput::dik::ESCAPE;
    using ::winx86::dx::dinput::dik::F;
    using ::winx86::dx::dinput::dik::G;
    using ::winx86::dx::dinput::dik::H;
    using ::winx86::dx::dinput::dik::LCONTROL;
    using ::winx86::dx::dinput::dik::LSHIFT;
    using ::winx86::dx::dinput::dik::LEFT;
    using ::winx86::dx::dinput::dik::PAUSE;
    using ::winx86::dx::dinput::dik::RCONTROL;
    using ::winx86::dx::dinput::dik::RETURN;
    using ::winx86::dx::dinput::dik::RIGHT;
    using ::winx86::dx::dinput::dik::RSHIFT;
    using ::winx86::dx::dinput::dik::T;
    using ::winx86::dx::dinput::dik::UP;
    using ::winx86::dx::dinput::Button;
    ButtonMap m;
    auto set = [&](Button b, uint8_t k) { m.set(b, k); };
    if (pad == 0) {
        set(Button::DpadUp, UP); set(Button::DpadDown, DOWN);
        set(Button::DpadLeft, LEFT); set(Button::DpadRight, RIGHT);
        set(Button::LStickUp, UP); set(Button::LStickDown, DOWN);
        set(Button::LStickLeft, LEFT); set(Button::LStickRight, RIGHT);
        set(Button::Cross, RETURN); set(Button::Triangle, RETURN);
        set(Button::Circle, BACK); set(Button::Square, BACK);
        set(Button::Start, ESCAPE); set(Button::Select, PAUSE);
        set(Button::L1, RSHIFT); set(Button::L2, RSHIFT);
        set(Button::R1, RCONTROL); set(Button::R2, RCONTROL);
    } else {
        set(Button::DpadUp, T); set(Button::DpadDown, G);
        set(Button::DpadLeft, F); set(Button::DpadRight, H);
        set(Button::LStickUp, T); set(Button::LStickDown, G);
        set(Button::LStickLeft, F); set(Button::LStickRight, H);
        set(Button::Cross, LSHIFT); set(Button::Circle, LCONTROL);
    }
    return m;
}

MapperConfig hotwheels_mapper(bool swapped) {
    MapperConfig m;
    m.pad[0] = hotwheels_button_map_for_pad(swapped ? 1 : 0);
    m.pad[1] = hotwheels_button_map_for_pad(swapped ? 0 : 1);
    return m;
}
} }
