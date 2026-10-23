// integrate.cpp — ver integrate.h. Orden calcado de rt_boot (D2Vita):
// memoria → kernel32 genérico → locale/wait/sync/psapi/advapi32/shell32/
// user32/gdi32/window/misc → shims del port (HW-006) → DirectDraw (HW-013).
#include "host/integrate.h"

#include "ddraw/bridge.h"
#include "d3d/bridge.h"
#include "ddraw/png.h"
#include "integration/dinput_bridge.h"
#include "integration/dsound_bridge.h"
#include "integration/mci_bridge.h"
#include "integration/mmio_bridge.h"
#include "dinput/dinput.h"
#include "shims/shims.h"

#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_region.h"
#include "runtime/pe_image.h"
#include "runtime/win32_shims_advapi32.h"
#include "runtime/win32_shims_gdi32.h"
#include "runtime/win32_shims_kernel32.h"
#include "runtime/win32_shims_locale.h"
#include "runtime/win32_shims_memory.h"
#include "runtime/win32_shims_misc.h"
#include "runtime/win32_shims_psapi.h"
#include "runtime/win32_shims_shell32.h"
#include "runtime/win32_shims_sync.h"
#include "runtime/win32_shims_user32.h"
#include "runtime/win32_shims_version.h"
#include "runtime/win32_shims_wait.h"
#include "runtime/win32_shims_window.h"
#include "runtime/ds_emul.h"          // d2rt::dsound::frame_pump

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <strings.h>

namespace hw::integrate {

namespace {
struct ArgcRow { const char* dll; const char* func; int bytes; };
const ArgcRow kArgc[] = {
#include "host/import_argc.inc"
};

wx86::GuestRegion g_heap, g_va;
hw::shims::Context* g_ctx = nullptr;     // reloj del guest para ds_emul
uint32_t hw_tick_ms(d2rt::Cpu&) { return g_ctx ? g_ctx->clock.ms() : 0u; }
hw::Layout g_L;
std::unique_ptr<hw::d3d::ComBridge> g_d3d;

bool vquery(uint32_t addr, uint32_t& base, uint32_t& size, uint32_t& state, uint32_t& type,
            uint32_t& protect, uint32_t& allocbase) {
    struct R { uint32_t b, s, prot, type; } regs[] = {
        {g_L.exe_base, g_L.exe_size, 0x40u, 0x1000000u},           // MEM_IMAGE
        {g_L.heap.base, g_L.heap.size, 0x04u, 0x20000u},
        {g_L.misc.base, g_L.misc.size, 0x40u, 0x20000u},
        {g_L.va.base, g_L.va.size, 0x40u, 0x20000u},
        {g_L.main_stack.base, g_L.main_stack.size, 0x04u, 0x20000u},
    };
    if (addr >= g_L.span) return false;
    for (auto& r : regs)
        if (addr >= r.b && addr < r.b + r.s) {
            base = r.b; size = r.s; state = 0x1000u; type = r.type; protect = r.prot; allocbase = r.b;
            return true;
        }
    uint32_t next = g_L.span;
    for (auto& r : regs) if (r.b > addr && r.b < next) next = r.b;
    base = addr & ~0xFFFu; size = next - base; state = 0x10000u; type = 0; protect = 0x01u; allocbase = 0;
    return true;
}
} // namespace

void shutdown_d3d() { g_d3d.reset(); }

int import_arg_bytes(const std::string& dll, const std::string& func) {
    for (const ArgcRow& r : kArgc)
        if (!strcasecmp(r.dll, dll.c_str()) && func == r.func) return r.bytes;
    return -2;
}

bool install_all(d2rt::Bridge& br, d2rt::Cpu& cpu, const d2rt::PeImage& exe, const hw::Layout& L,
                 const Config& cfg, Stats& st, std::string& err) {
    g_L = L;
    auto log = cfg.log ? cfg.log : [](const std::string& s) { std::printf("%s\n", s.c_str()); };

    // 1) stub-logger para cada import: devuelve 0 y limpia la pila con el argc real.
    for (const auto& imp : exe.imports()) {
        if (imp.name.empty()) continue;   // por ordinal: queda en el default shim
        const int bytes = import_arg_bytes(imp.dll, imp.name);
        const std::string tag = imp.dll + "!" + imp.name;
        d2rt::Shim s;
        s.tag = tag;
        s.argc = bytes > 0 ? (uint32_t)bytes / 4 : 0;
        s.stdcall_cleanup = bytes >= 0;
        s.fn = [tag, bytes, log, &st](d2rt::Cpu&) -> uint32_t {
            if (st.missing.insert(tag).second)
                log("MISSING " + tag + (bytes == -2 ? " (argc DESCONOCIDO: pila en riesgo)" : ""));
            return 0;
        };
        br.register_shim(imp.dll, imp.name, s);
    }

    // 1b) imports por ordinal con política conocida. DSOUND #1 = DirectSoundCreate
    // (lpGuid, ppDS, pUnkOuter): DSERR_NODRIVER, como una PC sin placa de sonido,
    // hasta que exista el shim de audio (HW-015/HW-016).
    {
        d2rt::Shim s;
        s.tag = "DSOUND.dll!DirectSoundCreate(#1)";
        s.argc = 3;
        s.fn = [log, &st](d2rt::Cpu&) -> uint32_t {
            if (st.missing.insert("DSOUND.dll!#1").second)
                log("STUB DSOUND.dll!#1 DirectSoundCreate -> DSERR_NODRIVER");
            return 0x88780078u;
        };
        br.register_shim_ordinal("DSOUND.dll", 1, s);
    }

    // 2) memoria (heap/VirtualAlloc sobre las regiones del layout).
    g_heap.init(L.heap.base + 0x1000, L.heap.size - 0x1000, 16, "heap");
    g_va.init(L.va.base + 0x10000, L.va.size - 0x10000, 0x10000, "va");
    wx86_mem_set_total_phys_mb(64);   // máquina de época; el juego dimensiona cachés con esto
    Wx86MemoryPlan mp; mp.heap = &g_heap; mp.va = &g_va; mp.vquery = vquery;
    win32_shims_memory_install(br, mp);

    // 3) shims genéricos de winx86 (mismo orden que rt_boot).
    win32_shims_kernel32_install(br);
    win32_shims_locale_install(br);
    win32_shims_wait_install(br);
    win32_shims_sync_install(br);
    win32_shims_psapi_install(br);
    win32_shims_version_install(br);
    win32_shims_advapi32_install(br);
    win32_shims_shell32_install(br);
    win32_shims_user32_install(br);
    win32_shims_gdi32_install(br);
    win32_shims_window_install(br);
    win32_shims_misc_install(br);

    // 4) shims del port (HW-006, dueño re). Igual que hw_register_shims, pero
    // vuelvo a construirlos con un registrador que agrega traza opcional
    // (HW_TRACE=Func1,Func2 o '*'): args crudos + cadena si el arg apunta a ASCII.
    static hw::shims::Context ctx;
    ctx.trace_settings = cfg.image_trace;
    ctx.vfs.mount('C', cfg.game_root, cfg.save_root);
    ctx.vfs.mount('D', cfg.cd_root);
    ctx.vfs.set_cwd("C:\\");
    ctx.cd.audio_track_frames.clear();
    for (auto& e : ctx.vfs.find("C:\\MUSIC\\Track *.wav"))
        ctx.cd.audio_track_frames.push_back(uint32_t(e.size * 75 / 176400) + 1);
    ctx.log = [log](const std::string& s) { log("shims: " + s); };
    hw::shims::install(br, cpu, ctx);   // completa servicios (trap_for, call_guest, …)
    g_ctx = &ctx;
    const char* tr = std::getenv("HW_TRACE");
    const std::string trace = tr ? std::string(",") + tr + "," : "";
    hw::shims::build(ctx, [&br, trace, log](const std::string& dll, const std::string& name, d2rt::Shim s) {
        // Chequeo de ABI contra la decoración @N de mingw (stdcall): un argc mal
        // corre la pila del invitado.
        const int want = import_arg_bytes(dll, name);
        if (want >= 0 && (!s.stdcall_cleanup || s.argc * 4 != (uint32_t)want))
            log("ABI " + dll + "!" + name + ": shim argc=" + std::to_string(s.argc) +
                (s.stdcall_cleanup ? "" : " (cdecl)") + ", mingw @" + std::to_string(want));
        if (!trace.empty() && (trace == ",*," || trace.find("," + name + ",") != std::string::npos)) {
            auto inner = s.fn;
            const uint32_t n = s.argc;
            const std::string tag = dll + "!" + name;
            s.fn = [inner, n, tag, log](d2rt::Cpu& c) -> uint32_t {
                std::string a;
                for (uint32_t k = 0; k < n && k < 6; ++k) {
                    char b[16]; std::snprintf(b, sizeof b, "%s%08x", k ? " " : "", c.arg(k)); a += b;
                    std::string str; uint32_t p = c.arg(k);
                    if (p >= 0x10000) for (uint32_t i = 0; i < 64; ++i) {
                        uint8_t ch = 0; if (!c.read(p + i, &ch, 1) || !ch) break;
                        if (ch < 0x20 || ch > 0x7e) { str.clear(); break; }
                        str += (char)ch;
                    }
                    if (str.size() >= 2) a += "\"" + str + "\"";
                }
                log("trace > " + tag + "(" + a + ")");
                uint32_t r = inner(c);
                char b[24]; std::snprintf(b, sizeof b, " -> %08x", r);
                log("trace " + tag + "(" + a + ")" + b);
                return r;
            };
        }
        br.register_shim(dll, name, std::move(s));
    });

    // 5) DirectDraw software 8 bpp (HW-013, dueño sol) con presentación a PNG.
    g_d3d.reset();
    hw::ddraw::HostOps ops;
    if (cfg.image_trace) ops.mode_changed=[log](uint32_t w,uint32_t h,uint32_t bpp,bool accepted,uint32_t ew,uint32_t eh,uint32_t ebpp) {
        char msg[144];
        std::snprintf(msg,sizeof(msg),"DDraw SetDisplayMode request=%ux%ux%u accepted=%u effective=%ux%ux%u",
                      w,h,bpp,unsigned(accepted),ew,eh,ebpp);
        log(msg);
    };
    if (cfg.d3d_backend) {
        ops.d3d3_object = [] { return g_d3d ? g_d3d->acquire_d3d3_object() : 0u; };
        ops.create_texture = [](uint32_t surface) { return g_d3d ? g_d3d->create_texture(surface) : 0u; };
        ops.flip_3d = [](uint32_t surface) {
            return g_d3d && g_d3d->flip(surface);
        };
    }
    ops.alloc_pixels = [](size_t n) -> uint32_t { return g_va.alloc((uint32_t)n); };
    const std::string out = cfg.out_dir;
    const uint32_t every = cfg.png_every ? cfg.png_every : 1;
    ops.present_sized = [out, every, &st, log, &cpu, present = cfg.present](const uint8_t* idx, uint32_t pitch, const uint32_t* pal, uint32_t w, uint32_t h) {
        d2rt::dsound::frame_pump(cpu);   // reloj del guest -> dsound.wav (efectos+música)
        const uint32_t n = st.frames++;
        if (present) { present(idx, pitch, pal, w, h); return; }
        if (n % every) return;
        char name[64]; std::snprintf(name, sizeof name, "/frame_%05u.png", n);
        hw::ddraw::PngPresenter(out + name, w, h).present(idx, pitch, pal);
        if (n < 3 || n % 100 == 0) log("frame " + std::to_string(n) + " -> " + out + name);
    };
    if (!hw::ddraw::install(br, cpu, ops)) { err = "ddraw::install falló"; return false; }
    if (cfg.d3d_backend) {
        hw::d3d::ComHostOps dops;
        dops.render_target = hw::ddraw::render_target;
        dops.texture_source = hw::ddraw::texture_source;
        dops.is_primary = hw::ddraw::is_primary;
        dops.frame_closed = cfg.d3d_frame_closed;
        dops.present_frame = cfg.d3d_present_frame;
        dops.producer_frame = [&st, &cpu, submitted = cfg.d3d_submitted] {
            d2rt::dsound::frame_pump(cpu);
            ++st.frames;
            if (submitted) submitted();
        };
        dops.log = log;
        dops.image_log = cfg.image_trace ? log : nullptr;
        dops.consumer_start = cfg.d3d_consumer_start;
        dops.lock_backend = cfg.d3d_lock_backend;
        dops.unlock_backend = cfg.d3d_unlock_backend;
        dops.frame_presented = cfg.d3d_present;
        g_d3d = std::make_unique<hw::d3d::ComBridge>(br, cpu, *cfg.d3d_backend, std::move(dops));
        if (!g_d3d->install()) { err = "ComBridge::install falló"; return false; }
        log("D3D6 COM registrado (backend host)");
    }
    struct EmptyInput : hw::dinput::InputSource {
        hw::dinput::InputSnapshot poll() override { return {}; }
    };
    static EmptyInput input;
    if (!hw::integration::install_dinput(br, cpu, cfg.input ? *cfg.input : input,
                                             cfg.mapper ? *cfg.mapper : hw::dinput::hotwheels_mapper())) {
        err = "dinput::install falló"; return false;
    }
    // Audio (HW-020 ports + HW-015 deepseek): ds_emul sobre el backend HW-018
    // (pisa el stub DSOUND #1 de 1b) y MCI cdaudio -> música del mismo Engine.
    hw::integration::DsoundConfig dcfg;
    dcfg.vita = cfg.vita;
    const char* dump_wav = std::getenv("HW_WAV");
    dcfg.wav_path = dump_wav && std::strcmp(dump_wav, "1") == 0
                        ? cfg.out_dir + "/dsound.wav" : std::string{};
    dcfg.write_root = cfg.out_dir;
    dcfg.game_root = cfg.game_root;
    dcfg.va_alloc = [](uint32_t n) -> uint32_t { return g_va.alloc(n); };
    dcfg.tick_ms = &hw_tick_ms;
    if (cfg.enable_audio && !hw::integration::hw_register_dsound(br, cpu, dcfg, err)) return false;
    hw::integration::install_mci(br, cpu, *hw::integration::hw_dsound_music());
    hw::integration::install_mmio(br, g_ctx->vfs, [](uint32_t n) -> uint32_t { return g_va.alloc(n); });
    return true;
}

} // namespace hw::integrate
