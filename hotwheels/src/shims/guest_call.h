// guest_call.h — BUG-001: invocar código del invitado DESDE un shim.
// Un shim corre con el GIL tomado (TrapGuard de cpu_box86). call_va anida
// cpu->run(); bajo el scheduler nativo, la primera trampa del callback (como
// mínimo la del centinela) vuelve a gil::lock() y el GIL NO es recursivo →
// auto-deadlock (EIP=centinela, "GIL busy"). Se libera el GIL alrededor del
// anidado; en coop gil::active()==false y Release es un no-op.
#pragma once
#include <cstdint>
#include <vector>
#include "runtime/bridge.h"
#include "runtime/gil.h"

namespace hw {
inline bool guest_call(d2rt::Bridge& br, uint32_t va, const std::vector<uint32_t>& args,
                       uint32_t& result, const char** fault) {
    d2rt::gil::Release r;
    return br.call_va(va, args, result, fault);
}
}
