// src/trace/trace.h — log de trazas de llamadas Win32 (BUG-002, HW-007).
//
// Formato consumido por tools/diff_trace.py (ver coordination/wine.md):
//   [Dll.]Func(arg0,arg1,...) = ret
//   - arg string -> "texto"; puntero -> P; escalar -> 0x..
//   - ret -> 0x........ (el oráculo sólo compara la clase 0/-1/otro)
//
// Habilitación por env HW_TRACE: unset/vacío = off; '*' = todos; si no, lista
// de nombres separados por coma (match sobre el nombre sin DLL).
//
// Este TÚ es puro (sin winx86): el envoltorio de shims está en trace_wrap.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace d2rt { class Cpu; }

namespace hw::trace {

// HW_TRACE no vacío.
bool enabled();
// true si el nombre debe trazarse (HW_TRACE=* o el nombre está en la lista).
bool wants(const std::string& func);

// Formatea "Dll.Func(args) = ret". `dll` vacío => "Func(args)".
// `args` son tokens ya listos ("\"texto\"", "P", "0x10", "00001000").
// `ret` vacío => sin "= ret".
std::string format_call(const std::string& dll, const std::string& func,
    const std::vector<std::string>& args, const std::string& ret);

// Deriva tokens de argumentos crudos: VA que apunta a ASCII >=2 chars => "texto";
// si no, >=0x10000 => P; si no, 0x..  `read(ctx,va,buf,n)` devuelve false si no es legible.
std::string format_from_memory(const std::string& dll, const std::string& func,
    const uint32_t* args, int nargs, uint32_t ret,
    const std::function<bool(uint32_t, uint8_t*, uint32_t)>& read);

// Escribe la línea a stdout (agrega '\n').
void emit(const std::string& line);

// Envuelve `fn` para trazar cada llamada si enabled()/wants(func). Si no, devuelve `fn`.
std::function<uint32_t(d2rt::Cpu&)> wrap(const std::string& dll, const std::string& func,
    uint32_t argc, std::function<uint32_t(d2rt::Cpu&)> fn);

}  // namespace hw::trace
