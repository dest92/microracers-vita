// src/audio/port_contract.h — contrato del puerto de salida con el driver.
//
// El sink real habla con sceAudioOutOutput (Vita), pero las reglas que importan
// — len múltiplo de 64, bloque igual al len, reutilización del buffer, rc<0,
// muestras en cola — se pueden ejercitar en host con un driver falso. El driver
// se inyecta; el Vita pasa las funciones de psp2/audioout.h.
#pragma once

#include "audio/output_contract.h"

namespace hw::audio {

// Driver del puerto. Los punteros son los de psp2/audioout.h en Vita.
struct PortDriver {
    int (*open)(int type, int len, int rate, int mode) = nullptr;
    int (*output)(int port, const void* buf) = nullptr;
    int (*rest)(int port) = nullptr;
    int (*release)(int port) = nullptr;
};

class PortOut {
public:
    explicit PortOut(const PortDriver& driver) : drv_(driver) {}

    // Abre el puerto: el len se alinea a 64 y se valida; se prueba MAIN y, si
    // no abre, BGM (el tipo elegido queda en type()). false si no abre ninguno.
    bool open(int type_main, int type_bgm, int rate, int mode, int grain) {
        const int len = output_block_frames(grain);
        if (!valid_output_block(len)) return false;
        port_ = drv_.open(type_main, len, rate, mode);
        type_ = type_main;
        if (port_ < 0) {
            port_ = drv_.open(type_bgm, len, rate, mode);
            type_ = type_bgm;
        }
        if (port_ < 0) return false;
        len_ = len;
        return true;
    }

    // Envía un bloque: lo copia al buffer del pingpong (alineado a 64, con la
    // cola en cero si viene corto) y lo pasa al driver. Devuelve su rc y deja
    // las muestras en cola antes y después del envío.
    int send(const int16_t* pcm, int frames) {
        if (port_ < 0 || !pcm || frames <= 0) return -1;
        int16_t* out = blocks_.prepare(pcm, frames, len_);
        if (!out) { ++errors_; return -1; }
        rest_before_ = drv_.rest ? drv_.rest(port_) : -1;
        const int rc = drv_.output(port_, out);
        rest_after_ = drv_.rest ? drv_.rest(port_) : -1;
        if (!output_succeeded(rc)) ++errors_;
        return rc;
    }

    void close() {
        if (port_ >= 0 && drv_.release) drv_.release(port_);
        port_ = -1;
    }

    int port() const { return port_; }
    int len() const { return len_; }
    int type() const { return type_; }
    int rest_before() const { return rest_before_; }
    int rest_after() const { return rest_after_; }
    unsigned errors() const { return errors_; }

private:
    PortDriver drv_;
    OutputBlocks blocks_;
    int port_ = -1, len_ = 0, type_ = 0;
    int rest_before_ = -1, rest_after_ = -1;
    unsigned errors_ = 0;
};

}  // namespace hw::audio
