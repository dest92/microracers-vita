// integrate.h — HW-007: arma el proceso completo del juego sobre winx86
// (memoria, shims genéricos de winx86, shims del port, DirectDraw y un
// stub-logger para todo import todavía sin shim). Común a hotwheels-host
// (qemu-arm) y, después, a hotwheels-vita.
#pragma once
#include "runtime/hw_layout.h"
#include "dinput/dinput.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace d2rt { struct Cpu; class Bridge; class PeImage; }

namespace hw::d3d { struct Backend; class Ring; }

namespace hw::integrate {

struct Config {
    std::string game_root;   // C:\ (dir del exe real)
    std::string cd_root;     // D:\ (CD del juego, hwicon.bmp)
    std::string save_root;   // overlay de escritura de C:
    std::string out_dir;     // frames PNG + logs
    hw::d3d::Backend* d3d_backend = nullptr; // lifetime del proceso, host HW_D3D=1
    std::function<void(std::shared_ptr<const hw::d3d::Ring>)> d3d_frame_closed;
    std::function<bool(std::shared_ptr<const hw::d3d::Ring>)> d3d_present_frame;
    std::function<void(uint32_t)> d3d_present;
    std::function<void()> d3d_submitted;
    std::function<void()> d3d_consumer_start;
    std::function<void()> d3d_lock_backend;
    std::function<void()> d3d_unlock_backend;
    hw::dinput::InputSource* input = nullptr; // lifetime del proceso
    const hw::dinput::MapperConfig* mapper = nullptr; // hwvita.ini; nullptr = preset Hot Wheels
    bool vita = false;
    bool image_trace = false; // HW_IMAGE_TRACE=1: cadena de resolución/filtro, sin logs por draw
    bool enable_audio = true; // HW_AUDIO=0: diagnóstico gráfico con DSERR_NODRIVER
    std::function<void(const uint8_t*, uint32_t, const uint32_t*, uint32_t, uint32_t)> present;
    uint32_t png_every = 1;  // guardar 1 de cada N Flip
    std::function<void(const std::string&)> log;
};

struct Stats {
    std::set<std::string> missing;   // dll!func sin shim llamados (una vez cada uno)
    uint32_t frames = 0;
};

// Antes de link(): registra stubs (argc real) para TODOS los imports de `exe`,
// después los shims reales, que los pisan (en el Bridge gana la última inscripción).
void shutdown_d3d();
bool install_all(d2rt::Bridge& br, d2rt::Cpu& cpu, const d2rt::PeImage& exe,
                 const hw::Layout& L, const Config& cfg, Stats& st, std::string& err);

// argc stdcall en bytes (tabla generada); -1 cdecl, -2 desconocido.
int import_arg_bytes(const std::string& dll, const std::string& func);

} // namespace hw::integrate
