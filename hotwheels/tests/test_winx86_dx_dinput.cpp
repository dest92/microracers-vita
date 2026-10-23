#include <winx86-dx/dinput/dinput.h>

#include <cassert>
#include <type_traits>

using namespace winx86::dx::dinput;

static_assert(!std::is_copy_constructible_v<DirectInput>);
static_assert(!std::is_move_constructible_v<DirectInput>);

class FakeInput final : public InputSource {
public:
    InputSnapshot poll() override {
        InputSnapshot s{};
        s.pad_count = 1;
        s.pads[0].buttons = kPadCross;
        return s;
    }
};

int main() {
    FakeInput input;
    MapperConfig config;
    // Core genérico no impone el mapeo de botones de un juego.
    assert(config.pad[0].get(Button::Cross) == 0);
    assert(config.pad[1].get(Button::Cross) == 0);
    config.pad[0].set(Button::Cross, 0x2c);
    DirectInput di(input, config);
    assert(di.query_interface(kIidDirectInputA));

    Device* keyboard = di.create_device(kGuidSysKeyboard);
    assert(keyboard);
    assert(keyboard->set_data_format(DataFormat::Keyboard) == DI_OK);
    assert(keyboard->acquire() == DI_OK);
    uint8_t state[kKeyboardStateBytes]{};
    assert(keyboard->get_device_state(sizeof(state), state) == DI_OK);
}
