// src/integration/mci_bridge.h — WINMM!mciSendCommandA del invitado sobre el
// Cdaudio portable de HW-015 (src/winmm/mci.*). Traduce los structs MCI_*_PARMS
// de memoria guest (PE32) a los portables y escribe de vuelta las salidas.
#pragma once
#include "winmm/winmm.h"

namespace d2rt { struct Cpu; class Bridge; }

namespace hw::integration {
// `sink` debe vivir todo el proceso (p. ej. hw_dsound_music()).
void install_mci(d2rt::Bridge& br, d2rt::Cpu& cpu, hw::winmm::MusicSink& sink);
}
