// runtime_vita.cpp — main de hotwheels-vita (spec 002, HW-009). Paridad con el
// arranque de D2Vita (tools/rt_boot.cpp + src/platform/d2_boot_config.cpp +
// src/runtime/scheduler_backend.cpp), recortado a lo genérico:
//   env.txt → layout propio (exe fijo en 0x400000) → env del Bridge/arena →
//   make_cpu_box86 → kubridge → mapeo heap/VA/MISC → Bridge → boot_to_entry →
//   scheduler nativo → set_main(entry) → run.
// Registro común de shims HW-007 y presentación Vita HW-021.
#include "boot/hw_boot.h"
#include "d3d/d3d.h"
#include "dinput/dinput.h"
#include "dinput/vita_input.h"
#include "fps/interp60.h"
#include "fps/runtime_mode.h"
#include "host/integrate.h"
#include "integration/scripted_input.h"
#include "io/boot_selector.h"
#include "io/config.h"
#include "io/paths.h"
#include "present/vita_present.h"
#include "runtime/pe_image.h"
#include "runtime/hw_layout.h"
#include "runtime/heartbeat.h"
#include "runtime/hw_intrin.h"
#include "runtime/release_profile.h"
#include "runtime/guest_eip_profile.h"
#include "runtime/trap_policy.h"
#include "diag/thread_cpu.h"

#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_thread.h"
#include "runtime/guest_thread_ctx.h"
#include "runtime/sched_native.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#ifdef __vita__
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <pthread.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <vitaGL.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#endif

#include <atomic>
#include <exception>
#include <functional>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

using namespace d2rt;

extern "C" {
// Dimensionado del runtime VitaSDK (como D2Vita vita_present.cpp). La arena
// del dynarec es un memblock aparte; este heap es sólo C++/STL + bytes del PE.
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
unsigned int _newlib_heap_size_user = 32 * 1024 * 1024;
int dyn86_vita_kubridge(void);
void dyn86_set_native(int on);
}

// Hook de winx86 (platform/vita_host.h): dónde escribe su journal de progreso.
// Forma de declaración única: dentro de un bloque extern "C" {} un const
// quedaría con enlace interno.
extern "C" const char* const wx86_vita_progress_path = "ux0:data/hotwheels/progress.txt";

static const char* kDataDir = "ux0:data/hotwheels/";

static uint64_t runtime_time_us() {
#ifdef __vita__
    return sceKernelGetProcessTimeWide();
#else
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

static bool write_image_capture(const std::string& path,
                                const std::vector<std::vector<uint32_t>>& frames,
                                uint32_t target) {
    if (frames.empty() || target >= frames.size()) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const char magic[8] = {'H','W','R','I','N','G','0','1'};
    const uint32_t count = uint32_t(frames.size());
    out.write(magic, sizeof magic);
    out.write(reinterpret_cast<const char*>(&count), sizeof count);
    out.write(reinterpret_cast<const char*>(&target), sizeof target);
    for (const auto& frame : frames) {
        if (frame.size() > UINT32_MAX) return false;
        const uint32_t n = uint32_t(frame.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        out.write(reinterpret_cast<const char*>(frame.data()), std::streamsize(frame.size() * sizeof(uint32_t)));
        if (!out) return false;
    }
    out.flush();
    return bool(out);
}

#ifdef __vita__
struct ImageCaptureJob {
    const std::string* path;
    const std::vector<std::vector<uint32_t>>* frames;
    const uint32_t* target;
    std::atomic<bool>* written;
};

static void* write_image_capture_worker(void* opaque) {
    const auto& job = *static_cast<ImageCaptureJob*>(opaque);
    const int affinity_rc = sceKernelChangeThreadCpuAffinityMask(
        sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
    if (affinity_rc != 0) {
        // USER_2 es precondición: sin migrar, el hilo queda en el core del
        // consumer GXM y su I/O frena el único replay D3D antes de presentar.
        sceClibPrintf("[hw-image] writer affinity USER_2 rc=0x%x; captura no escrita\n",
                      affinity_rc);
        return nullptr;
    }
    job.written->store(write_image_capture(*job.path, *job.frames, *job.target),
                       std::memory_order_release);
    return nullptr;
}

// BUG-026: migas de arranque síncronas, sin heap ni libstdc++. Distinguen
// "el loader no inició el proceso" / "murió en ctors estáticos" / "llegó a
// main" cuando hotwheels.log ni siquiera se trunca (no hay coredump).
static void boot_probe(const char* tag, int flags) {
    const int fd = sceIoOpen("ux0:data/hotwheels/boot_probe.txt", SCE_O_WRONLY | SCE_O_CREAT | flags, 0666);
    if (fd < 0) return;
    char b[64];
    const int n = std::snprintf(b, sizeof b, "%s t=%u\n", tag, (unsigned)sceKernelGetProcessTimeLow());
    if (n > 0) sceIoWrite(fd, b, (unsigned)n);
    sceIoClose(fd);
}
// Prioridad 101: corre antes que todo ctor C++ sin prioridad (lb, globals).
__attribute__((constructor(101))) static void boot_probe_ctor() { boot_probe("ctor101", SCE_O_TRUNC); }
#endif

// Log durable para watchdog: IO Vita directo, evita buffering de newlib.
#ifdef __vita__
static std::string g_log_path;
#else
static FILE* g_log = nullptr;
#endif
#include <cstdarg>
#include "io/log_buffer.h"
#ifdef __vita__
static uint32_t hb_now_ms() { return sceKernelGetProcessTimeLow() / 1000u; }
static hw::io::LogBuffer lb([](const std::string& s) {
    hw::hb::mark(hw::hb::Slot::LogSink, 1, hb_now_ms());
    const int fd = sceIoOpen(g_log_path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, s.data(), s.size()); sceIoClose(fd); }
    hw::hb::mark(hw::hb::Slot::LogSink, 2, hb_now_ms());
});

// Spec 023 / BUG-035: vigía interno. Escribe hb.txt con sceIo directo (nunca
// vía LogBuffer, cuyo mutex puede ser parte del cuelgue): cada 30 s y al
// detectar un hilo trabado nuevo (>5 s). En un trabado no toca vitaGL.
static std::atomic<int> hb_last_open_rc{0}, hb_last_udp_rc{0};
static void hb_append(const char* s, size_t n) {
    const int fd = sceIoOpen("ux0:data/hotwheels/hb.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    hb_last_open_rc.store(fd < 0 ? fd : 0);
    if (fd >= 0) { sceIoWrite(fd, s, n); sceIoClose(fd); }
}
// Latido UDP opt-in (HW_HB_UDP=ip:puerto): canal fuera de la memory card para
// distinguir "proceso vivo con E/S trabada" de "proceso detenido".
static int hb_udp_sock = -1;
static SceNetSockaddrIn hb_udp_to{};
static void hb_udp_init() {
    const char* e = std::getenv("HW_HB_UDP");
    if (!e || !*e) return;
    char ip[32]{}; unsigned port = 0;
    if (std::sscanf(e, "%31[^:]:%u", ip, &port) != 2) return;
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    static char net_mem[64 * 1024];
    SceNetInitParam p{}; p.memory = net_mem; p.size = sizeof net_mem; p.flags = 0;
    const int irc = sceNetInit(&p);
    if (irc < 0 && irc != (int)0x80410201 /* ya inicializada */) return;
    hb_udp_sock = sceNetSocket("hb", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    // No bloqueante: el canal de diagnóstico nunca debe trabar al vigía.
    int nb = 1;
    if (hb_udp_sock >= 0)
        sceNetSetsockopt(hb_udp_sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nb, sizeof nb);
    hb_udp_to.sin_family = SCE_NET_AF_INET;
    hb_udp_to.sin_port = sceNetHtons(uint16_t(port));
    sceNetInetPton(SCE_NET_AF_INET, ip, &hb_udp_to.sin_addr);
}
static void hb_udp(const char* s, size_t n) {
    if (hb_udp_sock >= 0)
        hb_last_udp_rc.store(sceNetSendto(hb_udp_sock, s, unsigned(n), 0, (SceNetSockaddr*)&hb_udp_to, sizeof hb_udp_to));
}
// Salidas silenciosas: excepción C++ no capturada (terminate→abort→_exit) o
// exit() de cualquier hilo terminan el proceso sin "[hw] fin:" ni coredump.
static void hb_last_words(const char* what) {
    char m[256];
    const int n = std::snprintf(m, sizeof m, "LAST t=%u %s\n", hb_now_ms(), what);
    if (n <= 0) return;
    const int fd = sceIoOpen("ux0:data/hotwheels/boot_probe.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, m, unsigned(n)); sceIoClose(fd); }
    hb_udp(m, size_t(n));
}
static void hb_on_terminate() {
    const char* what = "terminate (sin excepción activa)";
    char buf[200];
    if (std::exception_ptr ep = std::current_exception()) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { std::snprintf(buf, sizeof buf, "terminate: %s", e.what()); what = buf; }
        catch (...) { what = "terminate: excepción no std"; }
    }
    hb_last_words(what);
    std::abort();
}
static void hb_on_exit() { hb_last_words("exit()"); }
extern "C" void hw_hb_udp_send(const char* s, unsigned n) { hb_udp(s, n); }
static void hb_watch_loop(std::atomic<bool>& running) {
    constexpr uint32_t kStallMs = 5000;
    // Período corto por defecto: el cuelgue de BUG-035 llega a los ~3 s del
    // arranque. HW_HB_PERIOD_MS lo ajusta para soaks largos.
    const char* period_env = std::getenv("HW_HB_PERIOD_MS");
    const uint32_t kPeriodMs = period_env ? uint32_t(std::strtoul(period_env, nullptr, 10)) : 5000u;
    // Prioridad usuario más alta: duerme casi siempre; así un hilo guest en
    // espera activa no le quita CPU y el vigía distingue inanición de
    // proceso suspendido (si ni él escribe, el proceso entero está detenido).
    const int prio_rc = sceKernelChangeThreadPriority(0, 64);
    // Fuera de USER_0 (runners guest + main): si ese core queda tomado por el
    // kernel (p.ej. tormenta de excepciones), el vigía sigue informando.
    const int aff_rc = sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_1);
    unsigned reported = 0;
    uint32_t last_periodic = hb_now_ms();
    char line[512];
    { const int n = std::snprintf(line, sizeof line, "hb start t=%u prio_rc=0x%x aff_rc=0x%x\n", last_periodic, prio_rc, aff_rc);
      hb_append(line, size_t(n)); }
    bool udp_tried = false;
    // Canal 3: fd abierto UNA vez (sceIoWrite no reserva). Si este sigue
    // creciendo cuando hb.txt/UDP se callan, el proceso vive y fallan las
    // reservas del kernel (open/sendto), no la planificación.
    const int pfd = sceIoOpen("ux0:data/hotwheels/hb_fd.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    while (running.load()) {
        sceKernelDelayThread(500 * 1000);
        const uint32_t now = hb_now_ms();
        if (!udp_tried && now > 2000) { udp_tried = true; hb_udp_init(); }  // env.txt ya cargado
        // La cola de una ráfaga de LOG no espera a la próxima línea (try_lock:
        // el vigía nunca se bloquea en el mutex del logger).
        lb.tick(now);
        if (pfd >= 0) {
            size_t k = hw::hb::format(line, sizeof line - 64, now, kStallMs);
            k += size_t(std::snprintf(line + k, sizeof line - k, " open_rc=0x%x udp_rc=0x%x\n",
                                      unsigned(hb_last_open_rc.load()), unsigned(hb_last_udp_rc.load())));
            sceIoWrite(pfd, line, k);
        }
        if (hb_udp_sock >= 0) {
            size_t u = hw::hb::format(line, sizeof line - 96, now, kStallMs);
            SceKernelFreeMemorySizeInfo ufi; ufi.size = sizeof ufi;
            if (sceKernelGetFreeMemorySize(&ufi) == 0)
                u += size_t(std::snprintf(line + u, sizeof line - u, " user_k=%d cdram_k=%d phy_k=%d",
                                          ufi.size_user >> 10, ufi.size_cdram >> 10, ufi.size_phycont >> 10));
            hb_udp(line, u);
        }
        const unsigned mask = hw::hb::any_stalled(now, kStallMs);
        const bool new_stall = (mask & ~reported) != 0;
        reported = mask;
        if (!new_stall && now - last_periodic < kPeriodMs) continue;
        size_t n = hw::hb::format(line, sizeof line - 160, now, kStallMs);
        n += std::snprintf(line + n, sizeof line - n, new_stall ? " STALL\n" : "\n");
        hb_append(line, std::min(n, sizeof line - 1));
        // Cada syscall con su marcador: si el cuelgue está en sysmem, la línea
        // queda en "mem?" (la de arriba ya se escribió).
        hb_append("mem?", 4);
        SceKernelFreeMemorySizeInfo fi; fi.size = sizeof fi;
        const int mrc = sceKernelGetFreeMemorySize(&fi);
        n = size_t(std::snprintf(line, sizeof line, " rc=0x%x user_k=%d cdram_k=%d phy_k=%d\n", mrc,
                                 fi.size_user >> 10, fi.size_cdram >> 10, fi.size_phycont >> 10));
        hb_append(line, n);
        if (!new_stall) last_periodic = now;
        // vitaGL puede ser el colgado (lock interno tomado): la línea de arriba
        // ya quedó escrita; "vgl?" sin respuesta = vglMemFree no retornó.
        if (!new_stall && mask == 0) {
            hb_append("vgl?", 4);
            const int m = std::snprintf(line, sizeof line, " ram_k=%u vram_k=%u\n",
                                        unsigned(vglMemFree(VGL_MEM_RAM) >> 10),
                                        unsigned(vglMemFree(VGL_MEM_VRAM) >> 10));
            hb_append(line, size_t(m));
        }
    }
}
static void LOG_FLUSH() { lb.flush(); }
static const bool g_lb_atexit = (std::atexit(LOG_FLUSH), true);
#endif
static void LOG(const char* fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt); std::vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
#ifdef __vita__
    // Por lotes (hw::io::LogBuffer): antes 3 syscalls a la memory card por línea.
    lb.write(line, sceKernelGetProcessTimeLow() / 1000u);
    sceClibPrintf("%s", line);
#else
    if (g_log) { std::fputs(line, g_log); std::fflush(g_log); }
    std::fputs(line, stdout);
#endif
}
// SPEC-052: línea de telemetría de diagnóstico. Los contadores están acumulados desde el
// arranque (el lector no los reinicia: así no hay carrera con el hilo consumer) y el usuario
// saca los deltas entre dos líneas consecutivas.
static void log_diag_line(hw::d3d::Backend* backend, const char* context) {
    if (!backend) return;
    hw::diag::DiagWindow* w = backend->diag_window();
    if (!w) return;
    LOG("%s\n", hw::diag::format_diag_line(*w, context, 10000).c_str());
}

// env.txt: líneas KEY=VAL que pisan defaults (mismo formato que D2Vita).
static void load_env_txt(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        line[std::strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        setenv(line, eq + 1, 1);
        LOG("[hw] env.txt: %s=%s\n", line, eq + 1);
    }
    std::fclose(f);
}

static void set_default(const char* k, const char* v) { setenv(k, v, 0); }

int main(int argc, char** argv) {
#ifdef __vita__
    boot_probe("main", SCE_O_APPEND);
#endif
    std::string base = kDataDir;
#ifdef __vita__
    g_log_path = base + "hotwheels.log";
    const int log_fd = sceIoOpen(g_log_path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (log_fd >= 0) sceIoClose(log_fd);
#else
    g_log = std::fopen((base + "hotwheels.log").c_str(), "w");
#endif
    LOG("[hw] hotwheels-vita runtime (spec 002)\n");
#ifdef __vita__
    // Conserva la evidencia de la instancia anterior (el relanzamiento del
    // watchdog llega justo después de un cuelgue).
    sceIoRemove("ux0:data/hotwheels/hb.prev.txt");
    sceIoRename("ux0:data/hotwheels/hb.txt", "ux0:data/hotwheels/hb.prev.txt");
    std::set_terminate(hb_on_terminate);
    std::atexit(hb_on_exit);
    static std::atomic<bool> hb_running{true};
    std::thread(hb_watch_loop, std::ref(hb_running)).detach();
#endif

    load_env_txt(base + "env.txt");
    {   // SPEC-052: los toggles de diagnóstico se leen al arranque; un valor inválido avisa
        // por el log y manda el default (el parser no recorta ni adivina).
        std::vector<std::string> warns;
        hw::diag::parse_toggles_env(&warns);
        for (const std::string& w : warns) LOG("[hw] toggle invalido: %s\n", w.c_str());
    }
    hw::runtime::apply_console_release_defaults([](const char* key, const char* value) {
        set_default(key, value);
    });
    const char* hw_pmu = std::getenv("HW_PMU");
    const bool pmu_profile = hw_pmu && *hw_pmu && *hw_pmu != '0';
    if (pmu_profile) {
        // These engine counters are opt-in; normal runs do not enable PMU
        // reads or the extra shim/GIL diagnostic counters.
        set_default("WX86_FILSTAT", "1");
        set_default("WX86_NATPROF", "1");
        LOG("[hw] PMU perfilado activo; escena=%s\n",
            std::getenv("HW_PMU_SCENE") ? std::getenv("HW_PMU_SCENE") : "unknown");
    }
#ifdef __vita__
    // Keep the release clock contract explicit.  The PMU samples are only
    // comparable when ARM, bus, GPU and GPU crossbar are held at the same
    // requested values; read every result back because the kernel may clamp a
    // request or an external tool may have changed it before boot.
    const int arm_rc = scePowerSetArmClockFrequency(444);
    const int bus_rc = scePowerSetBusClockFrequency(222);
    const int gpu_rc = scePowerSetGpuClockFrequency(222);
    const int xbar_rc = scePowerSetGpuXbarClockFrequency(166);
    LOG("[hw] clocks request arm=444 bus=222 gpu=222 xbar=166 rc=%d/%d/%d/%d effective arm=%d bus=%d gpu=%d xbar=%d\n",
        arm_rc, bus_rc, gpu_rc, xbar_rc,
        scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
        scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
#endif
    const std::string ini_path = base + "hwvita.ini";
    if (hw::io::boot_should_open()) {
        LOG("[hw] selector de arranque solicitado\n");
        hw::io::run_boot_selector(ini_path);
    }
    hw::io::HwVitaConfig io_cfg;
    hw::io::load(ini_path, io_cfg);
    for (const auto& warning : io_cfg.warnings) LOG("[hw] hwvita.ini: %s\n", warning.c_str());
    const bool image_trace = [] { const char* e = std::getenv("HW_IMAGE_TRACE"); return e && *e == '1'; }();
    if (image_trace) {
        const char* filter = io_cfg.filter == hw::io::HwVitaConfig::Filter::Linear ? "linear"
                           : io_cfg.filter == hw::io::HwVitaConfig::Filter::Point ? "point" : "game";
        LOG("[hw-image] ini=%s data_root=%s scaling=%s render_scale=%s filter=%s\n",
            ini_path.c_str(), io_cfg.data_root.c_str(),
            io_cfg.scaling == hw::io::HwVitaConfig::Scaling::Stretch ? "stretch" : "fit",
            io_cfg.render_scale == hw::io::HwVitaConfig::RenderScale::Native ? "native" : "original", filter);
        std::string image_root = io_cfg.data_root;
        while (!image_root.empty() && (image_root.back() == '/' || image_root.back() == '\\')) image_root.pop_back();
        for (const char* rel : {"save/data/menu/settings.dat", "save/DATA/MENU/settings.dat",
                                "data/menu/settings.dat", "DATA/MENU/settings.dat"}) {
            const std::string path = image_root + "/" + rel;
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f) { LOG("[hw-image] settings candidate=%s absent\n", path.c_str()); continue; }
            const auto end = f.tellg();
            const size_t size = end > 0 ? size_t(end) : 0;
            std::array<unsigned char, 64> prefix{};
            f.seekg(0);
            const size_t count = std::min(size, prefix.size());
            f.read(reinterpret_cast<char*>(prefix.data()), std::streamsize(count));
            char hex[129]{};
            for (size_t i = 0; i < count; ++i)
                std::snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", prefix[i]);
            LOG("[hw-image] settings candidate=%s bytes=%u prefix_hex=%s%s\n", path.c_str(),
                unsigned(size), hex, size > count ? "..." : "");
        }
        const bool stretched = io_cfg.render_scale == hw::io::HwVitaConfig::RenderScale::Native &&
                               io_cfg.scaling == hw::io::HwVitaConfig::Scaling::Stretch;
        LOG("[hw-image] framebuffer=960x544 output_viewport=%s; no intermediate FBO; XYZRHW mapped in shader\n",
            stretched ? "0,0+960x544" : "117,0+725x544");
        LOG("[hw-image] settings path tracing=CreateFileA shim opt-in\n");
    }
    hw::dinput::MapperConfig mapper;
    hw::io::apply_to(mapper, io_cfg);
    hw::dinput::VitaInputSource vita_input(mapper);
    const hw::io::Roots roots = hw::io::vita_roots(io_cfg.data_root);
    if (!hw::io::ensure_save(roots)) LOG("[hw] no se pudo crear save: %s\n", roots.save.c_str());
    if (io_cfg.force_d3d) {
        if (hw::io::apply_display_defaults(roots.game, roots.save, true))
            LOG("[hw] force_d3d=1 aplicado antes del guest\n");
        else
            LOG("[hw] force_d3d: settings.dat inválido o no escribible; se conserva\n");
    }

    base = io_cfg.data_root;
    if (base.empty() || (base.back() != '/' && base.back() != '\\')) base += '/';
    std::string exe = argc > 1 ? argv[1] : base + (getenv("HW_EXE") ? getenv("HW_EXE") : "HotWheels.exe");

    std::vector<uint8_t> pe; hw::PeFileInfo info; std::string err;
    if (!hw::read_pe_file(exe, pe, info, err)) {
        LOG("[hw] read_pe_file %s: %s\n", exe.c_str(), err.c_str()); return 1;
    }
    uint32_t pe_off = 0, img_base = 0, img_size = 0;
    std::memcpy(&pe_off, &pe[0x3c], 4);
    std::memcpy(&img_base, &pe[pe_off + 24 + 28], 4);
    std::memcpy(&img_size, &pe[pe_off + 24 + 56], 4);

    hw::Layout L = hw::make_layout(img_base, img_size);
    if (!L.ok) { LOG("[hw] layout: %s\n", L.why.c_str()); return 1; }
    for (const hw::Region& r : L.regions())
        LOG("[hw] layout %-13s 0x%08x..0x%08x\n", r.name, r.base, r.base + r.size);
    LOG("[hw] layout exe 0x%08x..0x%08x span=0x%08x arena=0x%08x\n",
                img_base, img_base + img_size, L.span, L.arena);
    // ANTES de make_cpu_box86 (lee WX86_ARENA) y del Bridge (lee bases).
    hw::apply_layout_env(L);
    // Defaults genéricos de D2Vita leídos DENTRO de winx86; env.txt los pisa.
    set_default("WX86_B5", "1");
    set_default("WX86_YIELD", "2");
    wx86_set_main_tib(L.main_tib);

    Cpu* cpu = make_cpu_box86();
#ifdef __vita__
    const bool kub = dyn86_vita_kubridge() != 0;
#else
    const bool kub = false;
#endif
    LOG("[hw] kubridge: %s\n", kub ? "presente" :
                "AUSENTE (piscina JIT 16 MiB, sin handler de faltas)");

    if (!hw::map_runtime(*cpu, L, err)) { LOG("[hw] map_runtime: %s\n", err.c_str()); return 1; }

    Bridge br(cpu);
    br.set_module_limit(L.misc.base);

    ThreadScheduler* sched = nullptr;
    int traps = 0;
    // El juego tiene que arrancar por default (como en el host, que sigue con
    // stubs): con la parada automática el juego moría en el primer import sin
    // shim (KERNEL32!GetVersion) y NO había audio. La parada controlada queda
    // como diagnóstico: HW_STOP_AT_FIRST_TRAP=1.
    const bool stop_first = hw::runtime::stop_at_first_trap(getenv("HW_STOP_AT_FIRST_TRAP"));
    hw::present::VitaPresenter presenter;
    // Comparte el contexto vitaGL y los pools Vita con el presenter P8. Esto
    // permite que el COM D3D6 reproduzca su ring en el mismo hilo GXM.
    std::unique_ptr<hw::d3d::Backend> d3d_backend;
    // BUG-035 bisección: HW_DIAG_NOD3D=1 no crea el backend D3D vitaGL.
    const bool diag_nod3d = std::getenv("HW_DIAG_NOD3D") && *std::getenv("HW_DIAG_NOD3D") == '1';
    if (diag_nod3d) LOG("[hw] D3D backend desactivado (HW_DIAG_NOD3D=1)\n");
    if (presenter.init(640, 480) && !diag_nod3d) {
        d3d_backend = hw::d3d::make_vitagl_backend(
            960, 544, io_cfg.render_scale == hw::io::HwVitaConfig::RenderScale::Native,
            io_cfg.scaling == hw::io::HwVitaConfig::Scaling::Stretch,
            io_cfg.filter == hw::io::HwVitaConfig::Filter::Linear ? hw::d3d::TextureFilter::Linear
          : io_cfg.filter == hw::io::HwVitaConfig::Filter::Point ? hw::d3d::TextureFilter::Point
                                                                : hw::d3d::TextureFilter::Game);
        LOG("[hw] D3D backend vitaGL listo\n");
    } else {
        LOG("[hw] D3D backend vitaGL no disponible: %s\n", presenter.error());
    }
    std::mutex vita_gl_mutex;
    std::unique_ptr<hw::fps::Interp60Presenter> interp_presenter;
    auto fps_mode = hw::fps::select_runtime_mode(io_cfg.fps_mode);
    std::atomic<bool> present_loop_running{false};
    std::thread present_thread;
    std::atomic<uint32_t> interp_captures{0}, interp_rejects{0}, interp_ticks{0};
    std::atomic<uint64_t> interp_draws{0}, interp_fallback{0};
    std::atomic<uint64_t> interp_start_us{0};
    std::atomic<uint32_t> guest_d3d_frames{0};
    std::atomic<uint32_t> d3d_scenes_closed{0};
    hw::fps::PresentationHistory original_present_history;
    std::mutex original_present_history_mutex;
    std::atomic<uint64_t> original_first_present_us{0};
    const auto record_original_present = [&] {
        if (fps_mode.effective != hw::fps::EffectiveRuntimeMode::Original) return;
        const uint64_t now = runtime_time_us();
        uint64_t none = 0;
        original_first_present_us.compare_exchange_strong(none, now);
        std::lock_guard<std::mutex> lock(original_present_history_mutex);
        original_present_history.record(now);
    };
    if (fps_mode.ignored_saved)
        LOG("[hw] fps_mode=%s ignorado: el runtime corre siempre en original (BUG-047, SPEC-056)\n",
            hw::io::fps_mode_name(io_cfg.fps_mode));
    LOG("[hw] fps_mode guardado=%s efectivo=%s\n",
        hw::io::fps_mode_name(io_cfg.fps_mode),
        fps_mode.effective == hw::fps::EffectiveRuntimeMode::Interp60 ? "interp60" : "original");
    if (fps_mode.effective == hw::fps::EffectiveRuntimeMode::Interp60) {
        interp_presenter = std::make_unique<hw::fps::Interp60Presenter>(*d3d_backend);
    }
    hw::integrate::Config cfg;
    cfg.vita = true;
    cfg.image_trace = image_trace;
    const char* capture_env = std::getenv("HW_IMAGE_CAPTURE");
    const bool image_capture_enabled = image_trace && capture_env && *capture_env == '1';
    std::vector<std::vector<uint32_t>> image_capture_frames;
    uint64_t image_capture_words = 0;
    uint32_t image_capture_target = UINT32_MAX;
    bool image_capture_aborted = false;
    pthread_t image_capture_writer{};
    bool image_capture_writer_started = false;
    std::atomic<bool> image_capture_written{false};
    std::string image_capture_path = io_cfg.data_root;
    if (image_capture_path.empty() || (image_capture_path.back() != '/' && image_capture_path.back() != '\\'))
        image_capture_path += '/';
    image_capture_path += "image_capture.hwr";
    ImageCaptureJob image_capture_job{
        &image_capture_path, &image_capture_frames, &image_capture_target, &image_capture_written};
    cfg.d3d_backend = d3d_backend.get();
    if (d3d_backend) {
        cfg.d3d_frame_closed = [&d3d_scenes_closed, &interp_presenter,
                                &interp_captures, &interp_rejects, &interp_ticks,
                                &interp_draws, &interp_fallback, &interp_start_us,
                                image_capture_enabled, &image_capture_frames,
                                &image_capture_words, &image_capture_target, &image_capture_aborted,
                                &image_capture_writer, &image_capture_writer_started,
                                &image_capture_job](
                std::shared_ptr<const hw::d3d::Ring> ring) mutable {
            const uint32_t scene = ++d3d_scenes_closed;
            uint32_t draws = 0, uploads = 0;
            ring->for_each([&](uint16_t op, const uint32_t*, uint32_t) {
                if (op == uint16_t(hw::d3d::Op::Draw)) ++draws;
                if (op == uint16_t(hw::d3d::Op::TexUpload)) ++uploads;
            });
            if (image_capture_enabled && image_capture_target == UINT32_MAX && !image_capture_aborted) {
                const uint64_t next_words = image_capture_words + ring->words().size();
                if (image_capture_frames.size() >= 1024 || next_words > 8ull * 1024 * 1024) {
                    image_capture_aborted = true;
                    LOG("[hw-image] ring-capture abortado: límite 1024 frames / 8M words\n");
                } else {
                    image_capture_frames.push_back(ring->words());
                    image_capture_words = next_words;
                    if (draws >= 500) {
                        image_capture_target = uint32_t(image_capture_frames.size() - 1);
                        LOG("[hw-image] ring-capture target scene=%u frames=%u words=%llu draws=%u\n",
                            scene, unsigned(image_capture_frames.size()),
                            (unsigned long long)image_capture_words, draws);
                        const int rc = pthread_create(&image_capture_writer, nullptr,
                                                      write_image_capture_worker, &image_capture_job);
                        if (rc != 0) {
                            image_capture_aborted = true;
                            LOG("[hw-image] pthread_create writer rc=%d; captura desactivada\n", rc);
                        } else {
                            image_capture_writer_started = true;
                        }
                    }
                }
            }
            if (scene < 3 || scene % 100 == 0)
                LOG("[hw] D3D escena=%u draws=%u uploads=%u words=%u\n",
                    scene, draws, uploads, unsigned(ring->words().size()));
            if (interp_presenter && (scene == 1 || scene % 100 == 0)) {
                const uint64_t start = interp_start_us.load();
                const uint64_t elapsed = start ? runtime_time_us() - start : 0;
                const uint64_t hz_milli = elapsed
                    ? uint64_t(interp_ticks.load()) * 1000000000ull / elapsed : 0;
                LOG("[hw] interp60 captured=%u rejected=%u ticks=%u avg_hz_milli=%llu interpolated=%llu fallback=%llu\n",
                    interp_captures.load(), interp_rejects.load(), interp_ticks.load(),
                    (unsigned long long)hz_milli,
                    (unsigned long long)interp_draws.load(),
                    (unsigned long long)interp_fallback.load());
            }
        };
        cfg.d3d_submitted = [&guest_d3d_frames] { ++guest_d3d_frames; };
        cfg.d3d_present = [&record_original_present](uint32_t frame) {
            record_original_present();
            if (frame < 3 || frame % 100 == 0)
                LOG("[hw] D3D frame=%u backend=vitaGL\n", frame);
        };
        cfg.d3d_consumer_start = [] {
            const int rc = sceKernelChangeThreadCpuAffinityMask(
                sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_1);
            hw::diag::thread_registry.set(unsigned(hw::diag::ThreadRole::Render), sceKernelGetThreadId());
            LOG("[hw] D3D consumer affinity USER_1 (core 1) rc=0x%x\n", rc);
        };
        cfg.d3d_lock_backend = [&vita_gl_mutex] { vita_gl_mutex.lock(); };
        cfg.d3d_unlock_backend = [&vita_gl_mutex] { vita_gl_mutex.unlock(); };
        if (interp_presenter) {
            cfg.d3d_present_frame = [&](std::shared_ptr<const hw::d3d::Ring> ring) {
                if (!present_loop_running.load() || !interp_presenter) return false;
                const bool captured = interp_presenter->capture_world_frame(
                    *ring, runtime_time_us());
                if (captured) ++interp_captures;
                else ++interp_rejects;
                // The consumer parses the immutable slot into owned snapshot words;
                // malformed frames retain the last valid snapshot. Before the first
                // valid snapshot, ComBridge replays the frame on this same consumer.
                return captured || interp_captures.load() != 0;
            };
        }
    }
    cfg.enable_audio = !getenv("HW_AUDIO") || std::strcmp(getenv("HW_AUDIO"), "0") != 0;
    LOG("[hw] audio=%s\n", cfg.enable_audio ? "Vita" : "DSERR_NODRIVER (diagnóstico)");
    cfg.game_root = roots.game; cfg.cd_root = roots.cd; cfg.save_root = roots.save; cfg.out_dir = base;
#ifdef __vita__
    // BUG-035: HW_TRACE_UDP=1 manda cada línea (incluida la traza de shims)
    // por el canal UDP del vigía, sin pasar por la memory card.
    static const bool trace_udp = [] { const char* e = std::getenv("HW_TRACE_UDP"); return e && *e == '1'; }();
    cfg.log = [](const std::string& s) {
        if (trace_udp) {
            char m[300];
            const int n = std::snprintf(m, sizeof m, "%u %s", hb_now_ms(), s.c_str());
            if (n > 0) hb_udp(m, std::min<size_t>(size_t(n), sizeof m - 1));
            if (s.rfind("trace", 0) == 0) return;
        }
        LOG("[hw] %s\n", s.c_str());
    };
#else
    cfg.log = [](const std::string& s) { LOG("[hw] %s\n", s.c_str()); };
#endif
    cfg.present = [&](const uint8_t* idx, uint32_t pitch, const uint32_t* pal,
                      uint32_t w, uint32_t h) {
#ifdef __vita__
        hw::hb::mark(hw::hb::Slot::DdrawPresent, 1, hb_now_ms());  // esperando vita_gl_mutex
#endif
        std::lock_guard<std::mutex> lock(vita_gl_mutex);
#ifdef __vita__
        hw::hb::mark(hw::hb::Slot::DdrawPresent, 3, hb_now_ms());  // dentro de vitaGL/swap
#endif
        if (!presenter.resize(w, h)) {
            LOG("[hw] present %ux%u: %s\n", w, h, presenter.error());
            if (sched) sched->request_shutdown();
            return;
        }
        // BUG-035 bisección: HW_DIAG_NOPRESENT=1 salta vitaGL/swap del present DDraw.
        static const bool diag_nopresent = [] {
            const char* e = std::getenv("HW_DIAG_NOPRESENT"); return e && *e == '1'; }();
        if (!diag_nopresent) presenter.present(idx, pitch, pal);
#ifdef __vita__
        hw::hb::mark(hw::hb::Slot::DdrawPresent, 4, hb_now_ms());
#endif
        record_original_present();
        if (presenter.frames() <= 3 || presenter.frames() % 100 == 0)
            LOG("[hw] present frame=%llu %ux%u status=%s\n",
                (unsigned long long)presenter.frames(), w, h, presenter.error());
    };
    hw::integrate::Stats st;
    cfg.input = &vita_input;
    cfg.mapper = &mapper;
    hw::integration::ScriptedInput scripted([&st] { return st.frames; });
    if (const char* path = std::getenv("HW_INPUT_SCRIPT")) {
        std::ifstream input(path);
        std::string input_error;
        if (!input || !scripted.load(input, input_error)) {
            LOG("[hw] input script %s: %s\n", path,
                input_error.empty() ? "no se pudo abrir" : input_error.c_str());
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
    // Parada controlada (patrón rt_boot): argc del import desconocido → no se
    // sigue sobre un marco corrido; se redirige al centinela y se apaga el scheduler.
    opt.on_unresolved = [&](Cpu& c, const std::string& tag) -> uint32_t {
        ++traps;
        LOG("[hw] trap #%d %s (ret=0x%08x)\n", traps, tag.c_str(), c.trap_retaddr());
        if (stop_first) {
            LOG("[hw] parada controlada: import sin shim\n");
            if (sched) sched->request_shutdown();
            br.redirect_next(br.sentinel());
        }
        return 0;
    };
    if (!hw::boot_to_entry(br, *cpu, pe, opt, r, err)) {
        LOG("[hw] boot_to_entry: %s\n", err.c_str()); return 1;
    }
    // HW-052: objetivos HLE registrados antes de la primera traducción.
    hw::intrin::install(hw::intrin::parse_mode(std::getenv("HW_INTRIN")), L.span,
                        [](const std::string& s) { LOG("[hw] %s\n", s.c_str()); });
    LOG("[hw] base=0x%08x entry=0x%08x imports=%u sin_shim=%u stack_top=0x%08x\n",
                r.load_base, r.entry_va, r.import_count, r.unresolved_count, br.stack_top());

    // Scheduler nativo (make_scheduler de D2Vita sin lo específico de D2).
    if (!cpu->thread_emu_create()) { LOG("[hw] FATAL: backend sin multi-emu\n"); return 1; }
    dyn86_set_native(1);
    NativeScheduler* ns = new NativeScheduler(cpu, &br, L.thread_stacks.base, L.thread_stack_each,
                                              L.thread_tibs.base);
    g_native_sched = ns;   // ANTES de cualquier create_thread
    sched = ns;
    wx86_set_scheduler(ns);
    LOG("[hw] sched: nativo, NOCANCEL=%s\n",
                getenv("WX86_NOCANCEL") ? getenv("WX86_NOCANCEL") : "0 (default D2Vita)");

    GuestThread* mainT = ns->set_main(r.entry_va, L.main_stack_top, L.main_tib);
    LOG("[hw] run desde entry 0x%08x\n", r.entry_va);
    if (interp_presenter) {
        try {
            interp_start_us.store(runtime_time_us());
            present_loop_running.store(true);
            present_thread = std::thread([&] {
#ifdef __vita__
                const int affinity_rc = sceKernelChangeThreadCpuAffinityMask(
                    sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
                if (affinity_rc != 0)
                    LOG("[hw] interp60 affinity USER_2 rc=0x%x\n", affinity_rc);
#endif
                hw::fps::PresentationHistory history;
                uint64_t report_start = runtime_time_us();
                uint32_t report_guest_start = guest_d3d_frames.load();
                uint64_t min_1s_hz_milli = UINT64_MAX;
                uint64_t match_count_delta = 0, match_texture_miss = 0;
                uint64_t match_topology_miss = 0, match_viewport_miss = 0;
                uint64_t match_rematched = 0, match_window_miss = 0;
                uint64_t match_spatial_reject = 0, match_displacement_reject = 0;
                const char* context = std::getenv("HW_FPS_CONTEXT");
                const bool context_known = context && *context;
                if (!context_known) context = "unknown";
                const char* scene_label = std::getenv("HW_FPS_SCENE");
                const bool scene_known = scene_label && *scene_label;
                if (!scene_known) scene_label = "unknown";
                while (present_loop_running.load()) {
#ifdef __vita__
                    hw::hb::mark(hw::hb::Slot::Interp, 1, hb_now_ms());  // vblank
                    sceDisplayWaitVblankStart();
                    hw::hb::mark(hw::hb::Slot::Interp, 3, hb_now_ms());  // esperando vita_gl_mutex
#else
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
#endif
                    if (!present_loop_running.load()) break;
                    std::lock_guard<std::mutex> lock(vita_gl_mutex);
                    hw::fps::InterpStats stats;
                    const uint64_t tick_time_us = runtime_time_us();
#ifdef __vita__
                    hw::hb::mark(hw::hb::Slot::Interp, 5, hb_now_ms());  // dentro de tick (vitaGL)
#endif
                    const bool presented = interp_presenter->tick(tick_time_us, &stats);
#ifdef __vita__
                    hw::hb::mark(hw::hb::Slot::Interp, 6, hb_now_ms());
#endif
                    const uint64_t now = runtime_time_us();
                    if (presented) {
                        ++interp_ticks;
                        // Record after Replayer::play/backend.present completes.
                        history.record(now);
                        interp_draws.fetch_add(stats.interpolated);
                        interp_fallback.fetch_add(stats.fallback);
                        match_count_delta += stats.draw_count_delta;
                        match_texture_miss += stats.ordinal_texture_miss;
                        match_topology_miss += stats.ordinal_topology_miss;
                        match_viewport_miss += stats.ordinal_viewport_miss;
                        match_rematched += stats.rematched_order;
                        match_window_miss += stats.no_candidate_window;
                        match_spatial_reject += stats.spatial_reject;
                        match_displacement_reject += stats.displacement_reject;
                    }
                    if (now - interp_start_us.load() >= 1000000) {
                        const uint64_t rate = uint64_t(history.count_in_last(now, 1000000)) * 1000;
                        min_1s_hz_milli = std::min(min_1s_hz_milli, rate);
                    }
                    if (now - report_start >= 10000000) {
                        constexpr uint64_t kReportWindowUs = 10000000;
                        const uint32_t presents = history.count_in_last(now, kReportWindowUs);
                        const uint64_t avg_rate = uint64_t(presents) * 100;
                        const uint32_t guest_frames = guest_d3d_frames.load() - report_guest_start;
                        const uint64_t report_elapsed = now - report_start;
                        const uint64_t guest_rate = uint64_t(guest_frames) * 1000000000ull /
                                                    report_elapsed;
                        const bool rate_pass = avg_rate >= 58000 && min_1s_hz_milli >= 55000;
                        const char* gate = !context_known || !scene_known
                            ? "FAIL_CONTEXT" : rate_pass ? "PASS" : "FAIL_RATE";
                        LOG("[hw] fps-window context=%s scene=%s d3d_scenes_closed=%u guest_d3d_frames=%u guest_hz_milli=%llu presents_10s=%u avg_hz_milli=%llu min_1s_hz_milli=%llu gate=%s\n",
                            context, scene_label, d3d_scenes_closed.load(), guest_frames,
                            (unsigned long long)guest_rate, presents,
                            (unsigned long long)avg_rate,
                            (unsigned long long)(min_1s_hz_milli == UINT64_MAX ? 0 : min_1s_hz_milli),
                            gate);
                        LOG("[hw] interp60-match-window draw_count_delta=%llu ordinal_texture=%llu topology=%llu viewport=%llu rematched=%llu no_candidate=%llu spatial=%llu displacement=%llu (tick-weighted)\n",
                            (unsigned long long)match_count_delta,
                            (unsigned long long)match_texture_miss,
                            (unsigned long long)match_topology_miss,
                            (unsigned long long)match_viewport_miss,
                            (unsigned long long)match_rematched,
                            (unsigned long long)match_window_miss,
                            (unsigned long long)match_spatial_reject,
                            (unsigned long long)match_displacement_reject);
                        log_diag_line(d3d_backend.get(), context);
                        match_count_delta = match_texture_miss = match_topology_miss = 0;
                        match_viewport_miss = match_rematched = match_window_miss = 0;
                        match_spatial_reject = match_displacement_reject = 0;
                        { const std::string ir = hw::intrin::report();
                          if (!ir.empty()) LOG("[hw] %s\n", ir.c_str()); }
                        report_start = now;
                        report_guest_start = guest_d3d_frames.load();
                        min_1s_hz_milli = UINT64_MAX;
                    }
                }
            });
            LOG("[hw] fps_mode=interp60: consumer VitaGL 60 Hz activo\n");
        } catch (...) {
            present_loop_running.store(false);
            fps_mode.effective = hw::fps::EffectiveRuntimeMode::Original;
            LOG("[hw] no se pudo iniciar consumer 60 Hz; replay original\n");
        }
    }
    hw::diag::ThreadRegistration guest_cpu_registration(hw::diag::ThreadRole::Guest);
    const char* thread_cpu_env = std::getenv("HW_THREAD_CPU");
    const bool thread_cpu_enabled = thread_cpu_env && std::strcmp(thread_cpu_env, "1") == 0;
    std::atomic<bool> original_metrics_running{false};
    std::thread original_metrics_thread;
    if (fps_mode.effective == hw::fps::EffectiveRuntimeMode::Original) {
        try {
            original_metrics_running.store(true);
            original_metrics_thread = std::thread([&] {
                const char* context = std::getenv("HW_FPS_CONTEXT");
                const bool context_known = context && *context;
                if (!context_known) context = "unknown";
                const char* scene = std::getenv("HW_FPS_SCENE");
                const bool scene_known = scene && *scene;
                if (!scene_known) scene = "unknown";
                auto cpu_begin = thread_cpu_enabled ? hw::diag::sample_threads()
                    : std::array<hw::diag::ThreadSample, hw::diag::kThreadRoles>{};
                uint64_t report_start = runtime_time_us();
                uint32_t guest_start = guest_d3d_frames.load();
                uint64_t sample_start = report_start;
                uint64_t min_1s_hz_milli = UINT64_MAX;
                while (original_metrics_running.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    const uint64_t now = runtime_time_us();
                    const uint64_t first = original_first_present_us.load();
                    if (first && now - first >= 1000000 && now - sample_start >= 100000) {
                        std::lock_guard<std::mutex> lock(original_present_history_mutex);
                        const uint64_t rate = uint64_t(
                            original_present_history.count_in_last(now, 1000000)) * 1000;
                        min_1s_hz_milli = std::min(min_1s_hz_milli, rate);
                        sample_start = now;
                    }
                    if (now - report_start < 10000000) continue;
                    uint32_t presents;
                    {
                        std::lock_guard<std::mutex> lock(original_present_history_mutex);
                        presents = original_present_history.count_in_last(now, 10000000);
                    }
                    const uint64_t avg_rate = uint64_t(presents) * 100;
                    const uint32_t guest_end = guest_d3d_frames.load();
                    const uint32_t guest_frames = guest_end - guest_start;
                    const uint64_t elapsed = now - report_start;
                    const uint64_t guest_rate = elapsed
                        ? uint64_t(guest_frames) * 1000000000ull / elapsed : 0;
                    const bool rate_pass = avg_rate >= 58000 && min_1s_hz_milli >= 55000;
                    const char* gate = !context_known || !scene_known
                        ? "FAIL_CONTEXT" : rate_pass ? "PASS" : "FAIL_RATE";
                    LOG("[hw] fps-window mode=original context=%s scene=%s guest_d3d_frames=%u guest_hz_milli=%llu presents_10s=%u avg_hz_milli=%llu min_1s_hz_milli=%llu gate=%s\n",
                        context, scene, guest_frames, (unsigned long long)guest_rate,
                        presents, (unsigned long long)avg_rate,
                        (unsigned long long)(min_1s_hz_milli == UINT64_MAX ? 0 : min_1s_hz_milli),
                        gate);
                    if (thread_cpu_enabled) {
                        const auto cpu_end = hw::diag::sample_threads();
                        LOG("[hw] thread-cpu-clocks t_us=%llu arm=%d bus=%d gpu=%d xbar=%d\n",
                            (unsigned long long)now, scePowerGetArmClockFrequency(),
                            scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(),
                            scePowerGetGpuXbarClockFrequency());
                        for (unsigned role = 0; role < hw::diag::kThreadRoles; ++role) {
                            char line[1200];
                            hw::diag::format_thread_window(line, sizeof line,
                                hw::diag::kThreadNames[role], scene, 0, 0,
                                guest_start, guest_end, cpu_begin[role], cpu_end[role]);
                            LOG("%s\n", line);
                        }
                        cpu_begin = cpu_end;
                    }
                    log_diag_line(d3d_backend.get(), context);
                    report_start = now;
                    guest_start = guest_end;
                    sample_start = now;
                    min_1s_hz_milli = UINT64_MAX;
                }
            });
        } catch (...) {
            original_metrics_running.store(false);
            LOG("[hw] no se pudo iniciar telemetría fps original\n");
        }
    }
    hw::runtime::GuestEipProfile guest_profile;
    if (guest_profile.start_from_env()) LOG("[hw] guest EIP sampler opt-in activo\n");
    ns->run();
    const auto guest_profile_result = guest_profile.stop();
    if (guest_profile_result.total || guest_profile_result.idle) {
        const uint64_t active = guest_profile_result.guest + guest_profile_result.shim;
        const double guest_pct = active ? 100.0 * guest_profile_result.guest / active : 0.0;
        const double shim_pct = active ? 100.0 * guest_profile_result.shim / active : 0.0;
        LOG("[hw] guest-profile scene=%s interval_us=1000 start_ms=%u duration_ms=%u guest=%llu shim=%llu idle=%llu guest_pct_active=%.2f shim_pct_active=%.2f map_functions=%zu sampler=%s affinity_rc=%d priority_rc=%d\n",
            guest_profile_result.scene.c_str(), guest_profile_result.window_start_ms,
            guest_profile_result.duration_ms, (unsigned long long)guest_profile_result.guest,
            (unsigned long long)guest_profile_result.shim, (unsigned long long)guest_profile_result.idle,
            guest_pct, shim_pct, guest_profile_result.mapped_functions,
            guest_profile_result.vita_affinity_attempted ? "vita-user1" : "host-default",
            guest_profile_result.vita_affinity_attempted ? guest_profile_result.vita_affinity_rc : 0,
            guest_profile_result.vita_affinity_attempted ? guest_profile_result.vita_priority_rc : 0);
        for (size_t i = 0; i < guest_profile_result.top.size(); ++i) {
            const auto& row = guest_profile_result.top[i];
            const double pct = guest_profile_result.guest
                ? 100.0 * row.samples / guest_profile_result.guest : 0.0;
            LOG("[hw] guest-profile-top scene=%s rank=%zu start=0x%08x end=0x%08x name=%s samples=%llu guest_pct=%.2f\n",
                guest_profile_result.scene.c_str(), i + 1, row.start, row.end, row.name.c_str(),
                (unsigned long long)row.samples, pct);
        }
    }
    if (pmu_profile) {
        char shims[1024]{};
        if (br.natprof_window(shims, sizeof shims, 12) > 0)
            LOG("[pmu] shim_time_us (incluye I/O identificado por nombre): %s\n", shims);
    }
    present_loop_running.store(false);
    if (present_thread.joinable()) present_thread.join();
    hw::integrate::shutdown_d3d();
    hw::diag::thread_registry.set(unsigned(hw::diag::ThreadRole::Render), 0);
    if (image_capture_writer_started) {
        const int rc = pthread_join(image_capture_writer, nullptr);
        if (rc != 0) {
            LOG("[hw-image] pthread_join writer rc=%d; salida inmediata\n", rc);
            sceKernelExitProcess(1);
        }
        LOG("[hw-image] ring-capture %s frames=%u words=%llu ok=%u\n", image_capture_path.c_str(),
            unsigned(image_capture_target + 1), (unsigned long long)image_capture_words,
            unsigned(image_capture_written.load()));
    } else if (image_capture_aborted) {
        // El historial lo libera main (consumer ya unido), no el callback de consumo.
        image_capture_frames.clear();
    } else if (image_capture_enabled && image_capture_target == UINT32_MAX) {
        LOG("[hw-image] ring-capture sin escena elegible (mínimo 500 draws); no se escribió archivo\n");
    }
    original_metrics_running.store(false);
    if (original_metrics_thread.joinable()) original_metrics_thread.join();
    const uint64_t interp_begin = interp_start_us.load();
    const uint64_t interp_elapsed_us = interp_begin
        ? runtime_time_us() - interp_begin : 0;
    if (interp_presenter) {
        const uint32_t ticks = interp_ticks.load();
        const uint64_t hz_milli = interp_elapsed_us
            ? uint64_t(ticks) * 1000000000ull / interp_elapsed_us : 0;
        LOG("[hw] interp60 captures=%u rejected=%u ticks=%u avg_hz_milli=%llu interpolated=%llu fallback=%llu\n",
            interp_captures.load(), interp_rejects.load(), ticks,
            (unsigned long long)hz_milli,
            (unsigned long long)interp_draws.load(),
            (unsigned long long)interp_fallback.load());
    }
    presenter.shutdown();
    // SPEC-052: la última línea de telemetría (el protocolo la lee al salir de la carrera).
    log_diag_line(d3d_backend.get(), "shutdown");
    LOG("[hw] fin: %s, traps=%d, exit=0x%08x\n", ns->stop_reason(), traps,
                mainT ? mainT->exit_code : 0);
#ifdef __vita__
    LOG_FLUSH();
    sceKernelExitProcess(0);
#endif
    return 0;
}
