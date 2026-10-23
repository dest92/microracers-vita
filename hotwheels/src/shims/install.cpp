// install.cpp — HW-006: registra los shims en el Bridge con servicios de winx86.
#include "runtime/win32_shims_wait.h"
#include <cstdlib>
#include "shims/guest_call.h"
#include "shims/shims.h"
#include "runtime/guest_scratch.h"
#include "runtime/guest_thread_ctx.h"
#include "runtime/host_clock.h"
#include "runtime/guest_dt_profile.h" // kGuestPrevRva (SPEC-059)
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <filesystem>

namespace hw::shims {

void install(d2rt::Bridge& br, d2rt::Cpu& cpu, Context& ctx) {
    d2rt::Bridge* b = &br; d2rt::Cpu* c = &cpu;
    if (!ctx.trap_for) ctx.trap_for = [b](const std::string& dll, const std::string& name) { return b->shim_trap(dll, name); };
    if (!ctx.call_guest) ctx.call_guest = [b, c](uint32_t va, const std::vector<uint32_t>& args) {
        std::array<uint32_t, 10> regs{};
        for (int i = 0; i < 10; ++i) regs[i] = c->reg(i);
        uint32_t result = 0; const char* fault = nullptr;
        bool ok = hw::guest_call(*b, va, args, result, &fault);
        for (int i = 0; i < 10; ++i) c->set_reg(i, regs[i]);
        if (!ok) std::fprintf(stderr, "[hw-shims] call_va 0x%08x falló: %s\n", va, fault ? fault : "?");
        return ok ? result : 0u;
    };
    if (!ctx.alloc_guest) ctx.alloc_guest = [](uint32_t n) { return wx86_scratch_alloc(n); };
    if (!ctx.log) ctx.log = [](const std::string& s) { std::fprintf(stderr, "[hw-shims] %s\n", s.c_str()); };
    if (!ctx.exit) ctx.exit = [](uint32_t code) { std::fflush(nullptr); std::exit(int(code)); };
    if (!ctx.set_last_error) ctx.set_last_error = [c](uint32_t e) { wx86_set_lasterr(*c, e); };
    if (!ctx.wall_ms) ctx.wall_ms = [] {
        return int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()); };
    ctx.clock.set_source([] { return wx86_now_us(); });
    // HW-056: el juego espera 25 ms girando sobre QPC (timerWaitFrame40Hz
    // 0x412df0): miles de traps por frame con el GIL tomado. HW_QPC_IDLE_US=N:
    // dos QPC a <N us entre sí → dormir 1 ms en el scheduler (libera el GIL).
    if (const char* e = std::getenv("HW_QPC_IDLE_US")) {
        const unsigned long v = std::strtoul(e, nullptr, 10);
        if (v > 0 && v < 100000) {
            ctx.clock.spin_idle_us = uint32_t(v);
            ctx.clock.idle = [] { wx86_wait_sleep(1); };
            // SPEC-059: el giro del timer de 40 Hz corta cuando el dt llega a
            // 0x1000, o sea a los 25 ms del `prev` del juego. Con ese deadline la
            // siesta se corta antes del tramo final y el dt queda igual que sin
            // dormir. El margen cubre la siesta (~1 ms) + su jitter del scheduler.
            if (const char* d = std::getenv("HW_QPC_IDLE_DEADLINE_US")) {
                const unsigned long deadline = std::strtoul(d, nullptr, 10);
                if (deadline > 0 && deadline < 1000000) {
                    ctx.clock.idle_margin_us = 2000;
                    if (const char* m = std::getenv("HW_QPC_IDLE_MARGIN_US")) {
                        const unsigned long margin = std::strtoul(m, nullptr, 10);
                        if (margin > 0 && margin < 100000) ctx.clock.idle_margin_us = uint32_t(margin);
                    }
                    const uint32_t prev_va = ctx.exe_base + hw::runtime::kGuestPrevRva;
                    ctx.clock.spin_deadline = [c, prev_va, deadline] {
                        return uint64_t(c->read_u32(prev_va)) + deadline; };
                }
            }
        }
    }
    // HW-064: detect a tight empty USER32 poll loop without changing the
    // portable shim's policy. Once polls arrive within N us, sleep 1 ms on
    // subsequent empty polls; refreshing after the scheduler wait keeps the
    // detector active while the guest remains in that loop.
    if (const char* e = std::getenv("HW_PEEK_IDLE_US")) {
        char* end = nullptr;
        const unsigned long v = std::strtoul(e, &end, 10);
        if (end != e && *end == '\0' && v > 0 && v < 100000) {
            auto previous = std::make_shared<std::atomic<uint64_t>>(wx86_now_us());
            const uint64_t threshold = v;
            ctx.empty_peek = [previous, threshold] {
                const uint64_t now = wx86_now_us();
                const uint64_t prior = previous->load(std::memory_order_relaxed);
                if (now >= prior && now - prior < threshold)
                    wx86_wait_sleep(1);
                const uint64_t sample = wx86_now_us();
                uint64_t observed = previous->load(std::memory_order_relaxed);
                while (observed < sample && !previous->compare_exchange_weak(
                           observed, sample, std::memory_order_relaxed, std::memory_order_relaxed)) {}
            };
        } else {
            ctx.empty_peek = {};
        }
    } else {
        ctx.empty_peek = {};
    }
    build(ctx, [b](const std::string& dll, const std::string& name, d2rt::Shim s) {
        b->register_shim(dll, name, std::move(s)); });
}

Context& hw_register_shims(d2rt::Bridge& br, d2rt::Cpu& cpu, const std::string& game_root,
                           const std::string& cd_root, const std::string& save_root) {
    static Context ctx;
    ctx.vfs.mount('C', game_root, save_root);
    ctx.vfs.mount('D', cd_root);
    ctx.vfs.set_cwd("C:\\");
    ctx.cd.audio_track_frames.clear();
    for (auto& e : ctx.vfs.find("C:\\MUSIC\\Track *.wav")) {
        // Duración aprox. de un WAV PCM 44100/16/estéreo (176400 B/s) en frames de CD.
        ctx.cd.audio_track_frames.push_back(uint32_t(e.size * 75 / 176400) + 1);
    }
    install(br, cpu, ctx);
    return ctx;
}

} // namespace hw::shims
