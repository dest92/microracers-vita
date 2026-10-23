// tests/test_audio_port_contract.cpp — SPEC-048 CA11: el contrato del puerto de
// salida con el driver, ejercitado en host con un driver falso.
//
// Comprueba lo que el test anterior (OutputBlocks solo) no cubría: el len que
// pide al driver, el bloque que se le entrega, la reutilización (pingpong), el
// rc<0 y las muestras en cola antes/después.
#include "audio/port_contract.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace hw::audio;

namespace {
// Recorder global: el driver es un conjunto de punteros a función, así que el
// estado vive fuera (un std::function con captura alocaría en la ruta realtime).
struct Rec {
    int main_rc = 0;            // <0 simula que MAIN no abre
    int open_len = 0, open_type = 0, open_calls = 0;
    int output_rc = 0;
    std::vector<const void*> sent;
    std::vector<int> rests;
    size_t rest_at = 0;

    void reset() { *this = Rec(); }
};
Rec g;

int fake_open(int type, int len, int, int) {
    ++g.open_calls;
    g.open_type = type;
    g.open_len = len;
    if (type == 1 && g.main_rc < 0) return g.main_rc;
    return 3;
}
int fake_output(int, const void* buf) { g.sent.push_back(buf); return g.output_rc; }
int fake_rest(int) { return g.rest_at < g.rests.size() ? g.rests[g.rest_at++] : -1; }
int fake_release(int) { return 0; }

PortDriver fake_driver() {
    PortDriver d;
    d.open = &fake_open;
    d.output = &fake_output;
    d.rest = &fake_rest;
    d.release = &fake_release;
    return d;
}
}  // namespace

int main() {
    // 1) El len que se pide al driver es múltiplo de 64 y el puerto queda abierto.
    {
        g.reset();
        PortOut port(fake_driver());
        assert(port.open(1, 2, 48000, 1, 512));
        assert(port.len() == 512);
        assert(port.port() == 3);
        assert(port.type() == 1);
    }

    // 2) Un grano que no es múltiplo de 64 se alinea (y el motor usa la misma
    //    función, así que produce lo mismo que el puerto consume); uno menor de
    //    64 se sube al mínimo del driver.
    {
        g.reset();
        PortOut port(fake_driver());
        assert(port.open(1, 2, 48000, 1, 500));
        assert(port.len() == 512);
        g.reset();
        PortOut small(fake_driver());
        assert(small.open(1, 2, 48000, 1, 32));
        assert(small.len() == 64);        // SCE_AUDIO_MIN_LEN
        g.reset();
        PortOut none(fake_driver());
        assert(!none.open(1, 2, 48000, 1, 0));
    }

    // 3) Si MAIN no abre, se usa BGM y el tipo queda registrado.
    {
        g.reset();
        g.main_rc = -1;
        PortOut port(fake_driver());
        assert(port.open(1, 2, 48000, 1, 512));
        assert(port.type() == 2);
        assert(port.port() == 3);
        assert(g.open_calls == 2);
    }

    // 4) Envíos: el bloque alterna (pingpong), llega al driver con el len del
    //    puerto y las muestras en cola se leen antes y después.
    {
        g.reset();
        g.rests = {10, 9, 8, 7};
        PortOut port(fake_driver());
        assert(port.open(1, 2, 48000, 1, 512));
        int16_t pcm[1024];
        for (int i = 0; i < 1024; ++i) pcm[i] = int16_t(i);
        assert(port.send(pcm, 512) == 0);
        assert(g.sent.size() == 1);
        assert(port.rest_before() == 10 && port.rest_after() == 9);
        assert(port.send(pcm, 512) == 0);
        assert(g.sent.size() == 2);
        assert(g.sent[0] != g.sent[1]);                 // pingpong: no se reescribe el bloque
        assert(reinterpret_cast<uintptr_t>(g.sent[0]) % 64 == 0);
        assert(port.rest_before() == 8 && port.rest_after() == 7);
    }

    // 5) Un bloque corto: la cola hasta el len del puerto va en cero.
    {
        g.reset();
        PortOut port(fake_driver());
        assert(port.open(1, 2, 48000, 1, 512));
        int16_t pcm[4] = {1, 2, 3, 4};
        assert(port.send(pcm, 2) == 0);
        const int16_t* got = static_cast<const int16_t*>(g.sent[0]);
        for (int i = 0; i < 4; ++i) assert(got[i] == pcm[i]);
        for (int i = 4; i < 1024; ++i) assert(got[i] == 0);
    }

    // 6) rc<0 del driver: se cuenta como error y no se pierde el estado.
    {
        g.reset();
        g.output_rc = -1;
        PortOut port(fake_driver());
        assert(port.open(1, 2, 48000, 1, 512));
        int16_t pcm[1024] = {};
        assert(port.send(pcm, 512) == -1);
        assert(port.errors() == 1);
        assert(!output_succeeded(port.send(pcm, 512)));
        assert(port.errors() == 2);
    }

    std::puts("AUDIO-PORT-CONTRACT: PASS");
}
