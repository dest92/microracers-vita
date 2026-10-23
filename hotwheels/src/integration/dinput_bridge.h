#pragma once
#include "dinput/dinput.h"
namespace d2rt { class Bridge; struct Cpu; }
namespace hw::integration {
// cfg traduce botones del pad a DIK; sin él el pad no llega al juego (BUG-046).
bool install_dinput(d2rt::Bridge& br, d2rt::Cpu& cpu, dinput::InputSource& source,
                    const dinput::MapperConfig& cfg = dinput::hotwheels_mapper());
}
