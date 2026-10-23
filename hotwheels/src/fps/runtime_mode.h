#pragma once

#include "io/config.h"

namespace hw::fps {

enum class EffectiveRuntimeMode { Original, Interp60 };

struct RuntimeModeSelection {
    EffectiveRuntimeMode effective = EffectiveRuntimeMode::Original;
    bool ignored_saved = false;   // el ini pedía interp60/native60
};

// SPEC-056 (BUG-047): el runtime corre siempre en original. interp60 empareja draws de frames consecutivos
// por firma y posición; en movimiento empareja polígonos distintos y deja agujeros. El código de interp60
// queda compilado pero inalcanzable: revertir es devolver Interp60 acá cuando saved != Original y hay backend.
inline RuntimeModeSelection select_runtime_mode(hw::io::HwVitaConfig::FpsMode saved) {
    RuntimeModeSelection result;
    result.ignored_saved = saved != hw::io::HwVitaConfig::FpsMode::Original;
    return result;
}

} // namespace hw::fps
