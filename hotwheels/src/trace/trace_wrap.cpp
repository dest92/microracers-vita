// src/trace/trace_wrap.cpp — enganche al despachador de shims (BUG-002).
//
// Integ lo usa así, en el callback de registro de shims:
//     s.fn = hw::trace::wrap(dll, name, s.argc, s.fn);
// (ver la propuesta en coordination/integ.md). Este TU sí depende de winx86.
#include "trace/trace.h"

#include "runtime/cpu.h"

namespace hw::trace {

std::function<uint32_t(d2rt::Cpu&)> wrap(const std::string& dll, const std::string& func,
    uint32_t argc, std::function<uint32_t(d2rt::Cpu&)> fn) {
    if (!enabled() || !wants(func)) return fn;
    const std::string d = dll;
    const std::string f = func;
    const uint32_t n = argc > 8 ? 8 : argc;  // tope de argumentos a loguear
    return [fn, d, f, n](d2rt::Cpu& c) -> uint32_t {
        uint32_t a[8] = {};
        for (uint32_t i = 0; i < n; ++i) a[i] = c.arg(i);
        const uint32_t r = fn(c);
        emit(format_from_memory(d, f, a, static_cast<int>(n), r,
            [&c](uint32_t va, uint8_t* buf, uint32_t len) { return c.read(va, buf, len); }));
        return r;
    };
}

}  // namespace hw::trace
