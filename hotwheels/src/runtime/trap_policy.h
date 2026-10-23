// src/runtime/trap_policy.h — ¿el runtime se detiene en el primer import sin shim?
//
// El host NUNCA se detiene: el juego sigue con stubs que devuelven 0 y así llega
// a DirectSoundCreate (o sea, hay audio). En Vita el default era detenerse, así
// que sin env.txt el juego paraba en el primer import sin shim
// (KERNEL32!GetVersion, medido en Vita3K) y se quedaba SIN audio: un usuario
// con env.txt limpio no escuchaba nada. La parada controlada queda como
// diagnóstico opt-in (HW_STOP_AT_FIRST_TRAP=1).
#pragma once

#include <cstring>

namespace hw::runtime {

// true sólo si el entorno pide la parada (valor presente y distinto de "0").
inline bool stop_at_first_trap(const char* env_value) {
    return env_value && std::strcmp(env_value, "0") != 0;
}

}  // namespace hw::runtime
