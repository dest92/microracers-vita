// host_main.cpp — hotwheels-host (HW-007, tier 1): corre el HotWheels.exe REAL
// bajo winx86 en ARMv7 Linux (qemu-arm), con todos los shims del port y un
// stub-logger para lo que falta. Mismo layout de memoria que la Vita (spec 002).
//
//   hotwheels-host <exe> <C:-root> <D:-root> <out-dir> [segundos]
//
// Log: <out>/run.log (también stdout). Frames: <out>/frame_NNNNN.png.
#include "boot/hw_boot.h"
#include "host/integrate.h"
#include "host/d3d_shutdown_guard.h"
#include "host/watchdog_shutdown.h"
#include "integration/scripted_input.h"
#include "d3d/d3d.h"
#include "d3d/ring_stats.h"
#include <fstream>
#include <filesystem>
#include "runtime/hw_layout.h"
#include "runtime/hw_intrin.h"
extern "C" int hw_pcprof_start(void);
extern "C" unsigned hw_pcprof_report(void (*logline)(const char*));
#include "runtime/guest_eip_profile.h"
#include "runtime/guest_dt_profile.h"

#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_thread.h"
#include "runtime/guest_thread_ctx.h"
#include "runtime/pe_image.h"
#include "runtime/gil.h"
#include "runtime/sched_cooperative.h"
#include "runtime/sched_native.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace d2rt;

extern "C" void dyn86_set_native(int on);

static FILE* g_log = nullptr;
static void LOG(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap);
    if (g_log) { va_start(ap, fmt); std::vfprintf(g_log, fmt, ap); va_end(ap); std::fflush(g_log); }
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "uso: %s <exe> <C:-root> <D:-root> <out-dir> [segundos]\n", argv[0]);
        return 2;
    }
    const std::string exe = argv[1], croot = argv[2], droot = argv[3], out = argv[4];
    const int secs = argc > 5 ? std::atoi(argv[5]) : 30;
    mkdir(out.c_str(), 0755);
    g_log = std::fopen((out + "/run.log").c_str(), "w");
    LOG("[hw] hotwheels-host (HW-007) exe=%s C:=%s D:=%s límite=%ds\n", exe.c_str(), croot.c_str(),
        droot.c_str(), secs);

    std::vector<uint8_t> pe; hw::PeFileInfo info; std::string err;
    if (!hw::read_pe_file(exe, pe, info, err)) { LOG("[hw] read_pe_file: %s\n", err.c_str()); return 1; }
    uint32_t pe_off = 0, img_base = 0, img_size = 0;
    std::memcpy(&pe_off, &pe[0x3c], 4);
    std::memcpy(&img_base, &pe[pe_off + 24 + 28], 4);
    std::memcpy(&img_size, &pe[pe_off + 24 + 56], 4);
    hw::Layout L = hw::make_layout(img_base, img_size);
    if (!L.ok) { LOG("[hw] layout: %s\n", L.why.c_str()); return 1; }
    hw::apply_layout_env(L);
    setenv("WX86_B5", "1", 0);
    setenv("WX86_YIELD", "2", 0);
    wx86_set_main_tib(L.main_tib);

    Cpu* cpu = make_cpu_box86();
    if (!hw::map_runtime(*cpu, L, err)) { LOG("[hw] map_runtime: %s\n", err.c_str()); return 1; }
    Bridge br(cpu);
    br.set_module_limit(L.misc.base);

    hw::integrate::Config cfg;
    hw::integrate::Stats st;
    cfg.game_root = croot; cfg.cd_root = droot; cfg.save_root = out + "/save"; cfg.out_dir = out;
    cfg.log = [](const std::string& s) { LOG("[hw] %s\n", s.c_str()); };
    if (const char* path = getenv("HW_SETTINGS_FILE")) {
        std::error_code ec;
        const auto destination = std::filesystem::path(cfg.save_root) / "data/menu/settings.dat";
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (!ec) std::filesystem::copy_file(path, destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { LOG("[hw] settings overlay: %s\n", ec.message().c_str()); return 1; }
        LOG("[hw] settings overlay: %s\n", path);
    }
    hw::d3d::RefRaster raster;
    if (getenv("HW_D3D") && !std::strcmp(getenv("HW_D3D"), "1")) {
        cfg.d3d_backend = &raster;
        cfg.d3d_frame_closed = [closed = uint32_t{0}](std::shared_ptr<const hw::d3d::Ring> ring) mutable {
            uint32_t draws=0, uploads=0;
            ring->for_each([&](uint16_t op, const uint32_t*, uint32_t) {
                if (op == uint16_t(hw::d3d::Op::Draw)) ++draws;
                if (op == uint16_t(hw::d3d::Op::TexUpload)) ++uploads;
            });
            if (closed < 3 || closed % 100 == 0)
                LOG("[hw] D3D escena=%u draws=%u uploads=%u words=%zu\n", closed, draws, uploads, ring->words().size());
            static const bool stats = getenv("HW_D3D_STATS") && *getenv("HW_D3D_STATS") == '1';
            if (stats) {   // HW-067: geometría/recursos por escena para el oráculo de BUG-047
                char line[512];
                hw::d3d::format_ring_stats(hw::d3d::ring_stats(*ring), line, sizeof line);
                LOG("[hw] %s scene=%u\n", line, closed);
            }
            static const char* dump_dir = getenv("HW_D3D_DUMP_DIR");   // HW-067 R1: ring crudo por escena
            static const uint32_t dump_max = getenv("HW_D3D_DUMP_MAX") ? uint32_t(atoi(getenv("HW_D3D_DUMP_MAX"))) : 0;
            if (dump_dir && closed < dump_max) {
                char path[512]; std::snprintf(path, sizeof path, "%s/scene_%05u.bin", dump_dir, closed);
                if (FILE* f = std::fopen(path, "wb")) {
                    std::fwrite(ring->words().data(), 4, ring->words().size(), f);
                    std::fclose(f);
                }
            }
            ++closed;
        };
        cfg.d3d_present = [&raster, out](uint32_t frame) {
            char name[64]; std::snprintf(name, sizeof name, "/frame_%05u.png", frame);
            if (!raster.write_png(out + name)) LOG("[hw] D3D PNG falló frame=%u\n", frame);
            if (frame < 3 || frame % 100 == 0)
                LOG("[hw] D3D frame=%u %ux%u presents=%u uploads=%u\n", frame,
                    raster.width(), raster.height(), raster.presents, raster.uploads);
        };
    }
    hw::integration::ScriptedInput scripted([&st] { return st.frames; });
    if (const char* path = getenv("HW_INPUT_SCRIPT")) {
        std::ifstream script(path);
        if (!script || !scripted.load(script, err)) {
            LOG("[hw] guion %s: %s\n", path, script ? err.c_str() : "no se pudo abrir");
            return 1;
        }
        cfg.input = &scripted;
        LOG("[hw] input por frames: %s\n", path);
    }

    hw::BootOptions opt; hw::BootResult r;
    opt.module_key = "HotWheels.exe";
    opt.install_shims = [&](Bridge& b, Cpu& c, std::string& e) {
        PeImage* im = b.module(opt.module_key);
        if (!im) { e = "módulo no cargado"; return false; }
        return hw::integrate::install_all(b, c, *im, L, cfg, st, e);
    };
    opt.on_unresolved = [&](Cpu&, const std::string& tag) -> uint32_t {
        if (st.missing.insert(tag).second) LOG("[hw] MISSING %s (default, sin argc)\n", tag.c_str());
        return 0;
    };
    if (!hw::boot_to_entry(br, *cpu, pe, opt, r, err)) { LOG("[hw] boot_to_entry: %s\n", err.c_str()); return 1; }
    // HW-052: HLE (spans del raster SW) registrados antes de la 1.ª traducción.
    hw::intrin::install(hw::intrin::mode_from_env(), L.span,
                        [](const std::string& s) { LOG("[hw] %s\n", s.c_str()); });
    LOG("[hw] base=0x%08x entry=0x%08x imports=%u\n", r.load_base, r.entry_va, r.import_count);

    // HW_SCHED=coop: scheduler cooperativo (determinista, banco de pruebas como
    // en D2Vita). Default: nativo (el de la Vita).
    const bool coop = getenv("HW_SCHED") && !std::strcmp(getenv("HW_SCHED"), "coop");
    ThreadScheduler* ns = nullptr;
    if (coop) {
        ns = new CooperativeScheduler(cpu, &br, L.thread_stacks.base, L.thread_stack_each, L.thread_tibs.base);
    } else {
        if (!cpu->thread_emu_create()) { LOG("[hw] FATAL: backend sin multi-emu\n"); return 1; }
        dyn86_set_native(1);
        NativeScheduler* n = new NativeScheduler(cpu, &br, L.thread_stacks.base, L.thread_stack_each,
                                                 L.thread_tibs.base);
        g_native_sched = n;
        ns = n;
    }
    wx86_set_scheduler(ns);
    LOG("[hw] sched: %s\n", coop ? "coop" : "nativo");

    // HW-072 (SPEC-055 CA2): observación del `dt` que el invitado usa para el
    // frame (timerWaitFrame40Hz 0x412df0 → DAT_00468f8c). Es lectura pura del
    // espacio del invitado: no se parchea ni el exe ni el codegen.
    hw::runtime::GuestDtProfile dt_profile(
        [cpu, L] { return cpu->read_u32(L.exe_base + hw::runtime::kGuestPrevRva); },
        [cpu, L] { return cpu->read_u32(L.exe_base + hw::runtime::kGuestDtRva); });
    auto dump_dt_profile = [&dt_profile] {
        if (!dt_profile.running()) return;
        const hw::runtime::GuestDtResult r = dt_profile.stop();
        LOG("[hw] dt-profile waits=%llu samples=%llu min=%u max=%u mean=%.2f p50=%u p90=%u p99=%u "
            "out_of_range=%u distinct=%u\n",
            (unsigned long long)r.waits, (unsigned long long)r.samples, r.min_dt, r.max_dt,
            r.mean(), r.percentile(0.50), r.percentile(0.90), r.percentile(0.99),
            r.out_of_range, r.distinct);
        size_t shown = 0;
        for (const auto& row : r.histogram) {
            if (shown++ >= 64) break;   // el log no crece sin límite
            LOG("[hw] dt-hist dt=%u n=%llu\n", row.first, (unsigned long long)row.second);
        }
        if (shown < r.histogram.size()) LOG("[hw] dt-hist ... %u valores más\n",
            (unsigned)(r.histogram.size() - shown));
    };
    if (getenv("HW_DT_PROFILE")) {
        if (dt_profile.start()) LOG("[hw] dt-profile: muestreo activo\n");
        else LOG("[hw] dt-profile: no se pudo arrancar\n");
    }

    // Corte por tiempo: el juego no termina solo.
    std::thread([ns, secs, cpu, L, &dump_dt_profile] {
        // Muestreo de EIP invitado (aprox.: el dynarec sólo sincroniza IP en
        // bordes de bloque) en los últimos segundos: ubica bucles sin imports.
        const int samples = secs > 4 ? 8 : 0;
        std::this_thread::sleep_for(std::chrono::seconds(secs - (samples ? 2 : 0)));
        for (int i = 0; i < samples; ++i) {
            const uint32_t* ip = cpu->ip_ptr();
            LOG("[hw] muestra EIP: %08x\n", ip ? *ip : 0u);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        LOG("[hw] límite de %ds: hilos invitados:\n", secs);
        {   // Palabras de la pila principal que parecen retaddr en .text del exe.
            std::string chain;
            for (uint32_t a = L.main_stack_top - 4; a >= L.main_stack_top - 0x2000; a -= 4) {
                uint32_t v = cpu->read_u32(a);
                if (v >= L.exe_base + 0x1000 && v < L.exe_base + 0x5d000) {
                    char b[32]; std::snprintf(b, sizeof b, " [%05x]=%08x", L.main_stack_top - a, v); chain += b;
                }
            }
            LOG("[hw] pila principal (desde el tope):%s\n", chain.c_str());
        }
        ns->dump_threads_to([](const char* l) { LOG("[hw]   %s\n", l); });
        dump_dt_profile();   // el dt del invitado, justo antes de apagar
        LOG("[hw] apagando\n");
        if (!hw::host::request_shutdown_bounded(ns, 100)) {
            LOG("[hw] watchdog: GIL ocupado; no bloqueo, salida forzada en 3 s\n");
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
        LOG("[hw] el scheduler no paró en 3 s: salida forzada\n");
        std::_Exit(3);
    }).detach();

    GuestThread* mainT = ns->set_main(r.entry_va, L.main_stack_top, L.main_tib);
    hw::runtime::GuestEipProfile guest_profile;
    if (guest_profile.start_from_env()) LOG("[hw] guest EIP sampler opt-in activo\n");
    // Perfil fiel por PC host (HW_PC_PROFILE=1, sólo qemu): ver src/diag/pc_profile.c.
    if (hw_pcprof_start()) LOG("[hw] pcprof: SIGPROF 1 ms activo\n");
    ns->run();
    dump_dt_profile();   // por si el invitado terminó antes que el watchdog
    // g_d3d is process-global but its consumer references the local backend.
    // Declared after backend setup, this guard closes/drains and joins before
    // RefRaster's earlier local lifetime ends.
    hw::host::D3DShutdownGuard d3d_shutdown([] {
        hw::integrate::shutdown_d3d();
    });
    hw_pcprof_report([](const char* s) { LOG("[hw] %s\n", s); });
    const auto guest_profile_result = guest_profile.stop();
    if (guest_profile_result.total || guest_profile_result.idle) {
        const uint64_t active = guest_profile_result.guest + guest_profile_result.shim;
        const double guest_pct = active ? 100.0 * guest_profile_result.guest / active : 0.0;
        const double shim_pct = active ? 100.0 * guest_profile_result.shim / active : 0.0;
        LOG("[hw] guest-profile scene=%s interval_us=1000 start_ms=%u duration_ms=%u guest=%llu shim=%llu idle=%llu guest_pct_active=%.2f shim_pct_active=%.2f map_functions=%zu\n",
            guest_profile_result.scene.c_str(), guest_profile_result.window_start_ms,
            guest_profile_result.duration_ms, (unsigned long long)guest_profile_result.guest,
            (unsigned long long)guest_profile_result.shim, (unsigned long long)guest_profile_result.idle,
            guest_pct, shim_pct, guest_profile_result.mapped_functions);
        for (size_t i = 0; i < guest_profile_result.top.size(); ++i) {
            const auto& row = guest_profile_result.top[i];
            const double pct = guest_profile_result.guest
                ? 100.0 * row.samples / guest_profile_result.guest : 0.0;
            LOG("[hw] guest-profile-top scene=%s rank=%zu start=0x%08x end=0x%08x name=%s samples=%llu guest_pct=%.2f\n",
                guest_profile_result.scene.c_str(), i + 1, row.start, row.end, row.name.c_str(),
                (unsigned long long)row.samples, pct);
        }
    }
    LOG("[hw] fin: %s exit=0x%08x frames=%u missing=%zu\n", ns->stop_reason(),
        mainT ? mainT->exit_code : 0, st.frames, st.missing.size());
    for (const auto& m : st.missing) LOG("[hw]   missing %s\n", m.c_str());
    return 0;
}
