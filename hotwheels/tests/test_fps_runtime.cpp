#include "fps/runtime_mode.h"

#include <cassert>

int main() {
    using Saved = hw::io::HwVitaConfig::FpsMode;
    using Effective = hw::fps::EffectiveRuntimeMode;

    // SPEC-056 CA1: interp60/native60 guardados se ignoran; el runtime corre siempre en original.
    auto original = hw::fps::select_runtime_mode(Saved::Original);
    assert(original.effective == Effective::Original && !original.ignored_saved);
    for (Saved saved : {Saved::Interp60, Saved::Native60}) {
        auto r = hw::fps::select_runtime_mode(saved);
        assert(r.effective == Effective::Original);
        assert(r.ignored_saved);
    }
}
