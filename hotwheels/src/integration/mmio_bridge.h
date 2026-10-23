// src/integration/mmio_bridge.h — WINMM!mmio* del invitado sobre el Mmio
// portable de HW-015 (src/winmm/mmio.*). Lee archivos vía el Vfs de HW-006.
#pragma once
#include <cstdint>
#include "shims/vfs.h"

namespace d2rt { class Bridge; }

namespace hw::integration {
// `vfs` debe vivir todo el proceso. `guest_alloc`: memoria guest permanente
// para los buffers de mmioGetInfo/mmioAdvance (uno por handle, se reusa).
void install_mmio(d2rt::Bridge& br, hw::shims::Vfs& vfs, uint32_t (*guest_alloc)(uint32_t));
}
