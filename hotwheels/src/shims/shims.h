// shims.h — HW-006: shims base para que HotWheels.exe arranque (spec 006).
// Núcleo portable (Vfs, CdDrive, GuestClock, MsgQueue) + build() que produce
// los d2rt::Shim por (dll, nombre). install() los registra en el Bridge.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "runtime/bridge.h"  // d2rt::Shim, d2rt::Cpu (tipos)
#include "shims/vfs.h"
#include "../../../winx86-dx/include/winx86-dx/shims/base.h"

namespace hw::shims {

// Hot Wheels profile: the generic library requires explicit CD identity/config.
class CdDrive final : public winx86::dx::shims::CdDrive {
public:
    CdDrive() : winx86::dx::shims::CdDrive(
        winx86::dx::shims::CdDriveConfig{'D', "HWMR", "CDFS", 0x1F2E3D4Cu, 75 * 60 * 10, {}}) {}
};

class GuestClock final : public winx86::dx::shims::GuestClock {
public:
    bool take_stats_line(std::string& out);
};
using Msg = winx86::dx::shims::Msg;
using MsgQueue = winx86::dx::shims::MsgQueue;

struct Context {
    Vfs vfs;
    CdDrive cd;
    GuestClock clock;
    MsgQueue queue;
    uint32_t exe_base = 0x400000;
    std::string exe_path = "C:\\HotWheels.exe";
    uint32_t pid = 0x1A0;
    // Servicios inyectados (install() pone los del Bridge/host).
    std::function<uint32_t(const std::string& dll, const std::string& name)> trap_for;
    std::function<uint32_t(uint32_t va, const std::vector<uint32_t>& args)> call_guest;
    std::function<uint32_t(uint32_t bytes)> alloc_guest;  // memoria invitada persistente
    std::function<void(const std::string&)> log;
    std::function<void(uint32_t code)> exit;
    std::function<void()> wait;                           // WaitMessage con cola vacía
    std::function<void()> empty_peek;                     // política de polling vacío USER32
    std::function<void(uint32_t)> set_last_error;
    std::function<int64_t()> wall_ms;                     // ms desde 1970 (UTC)
    bool trace_settings = false;                           // HW_IMAGE_TRACE: log de path resuelto
    std::shared_ptr<winx86::dx::shims::BaseContext> portable_context;
};

using Registrar = std::function<void(const std::string& dll, const std::string& name, d2rt::Shim)>;

// Produce todos los shims de HW-006. `ctx` debe vivir mientras se usen.
void build(Context& ctx, const Registrar& reg);
// Registra en el Bridge y completa los servicios no inyectados con los de winx86.
void install(d2rt::Bridge& br, d2rt::Cpu& cpu, Context& ctx);

} // namespace hw::shims

namespace hw::shims {
// Atajo para el boot (HW-007/HW-009): arma un Context estático con
//   C: = game_root (+ save_root para escrituras), D: = cd_root (con hwicon.bmp),
// cwd C:\, pistas de audio = cantidad de "Track *.wav" en game_root/MUSIC,
// y llama install(). Llamar DESPUÉS de win32_shims_*_install y ANTES de br.link().
Context& hw_register_shims(d2rt::Bridge& br, d2rt::Cpu& cpu, const std::string& game_root,
                           const std::string& cd_root, const std::string& save_root = "");
} // namespace hw::shims
