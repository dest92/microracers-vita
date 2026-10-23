#include "measurement.h"
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <atomic>
#include <cstdio>

unsigned int sceUserMainThreadStackSize = 256 * 1024;
unsigned int _newlib_heap_size_user = 2 * 1024 * 1024;
using namespace hw::clock_probe;
static_assert(kWaiting == SCE_THREAD_WAITING);
static constexpr uint32_t kDurationUs = 200000;
enum { Stop, Busy, Sleep };
struct WorkerArgs { unsigned core; int command, done; };
static std::atomic<int> phase{Stop}, pin_rc{-1}, phase_rc{0};

static int wait_sema(int sema) {
    SceUInt timeout = 2000000;
    return sceKernelWaitSema(sema, 1, &timeout);
}
static int worker(SceSize, void* raw) {
    const auto args = *static_cast<WorkerArgs*>(raw);
    pin_rc.store(sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), int(0x10000u << args.core)),
                 std::memory_order_release);
    int rc = sceKernelSignalSema(args.done, 1);
    if (rc != 0) return rc;
    for (;;) {
        // Observer escribe logs sólo con este worker bloqueado.
        SceUInt timeout = 5000000;
        rc = sceKernelWaitSema(args.command, 1, &timeout);
        if (rc != 0) return rc;
        const int command = phase.load(std::memory_order_acquire);
        if (command == Stop) return 0;
        rc = 0;
        if (command == Busy) {
            volatile uint32_t value = 1;
            const uint64_t start = sceKernelGetSystemTimeWide();
            do {
                for (unsigned i = 0; i < 512; ++i) value = value * 1664525u + 1013904223u;
            } while (sceKernelGetSystemTimeWide() - start < kDurationUs);
        } else if (command == Sleep) rc = sceKernelDelayThread(kDurationUs);
        else rc = -1;
        phase_rc.store(rc, std::memory_order_release);
        rc = sceKernelSignalSema(args.done, 1);
        if (rc != 0) return rc;
    }
}
static Sample blocked_sample(int tid, unsigned generation, int barrier) {
    Sample sample;
    const uint64_t start = sceKernelGetSystemTimeWide();
    do {
        SceKernelThreadInfo info{}; info.size = sizeof info;
        sample.thread.tid = tid; sample.thread.generation = generation;
        sample.thread.rc = sceKernelGetThreadInfo(tid, &info);
        sample.thread.wall_us = sceKernelGetSystemTimeWide();
        sample.cpu_mhz = scePowerGetArmClockFrequency();
        if (sample.thread.rc != 0) return sample;
        sample.thread.clocks = info.runClocks;
        sample.thread.mask = info.currentCpuAffinityMask;
        sample.thread.cpu = info.currentCpuId;
        sample.thread.last_cpu = info.lastExecutedCpuId;
        sample.thread.priority = info.currentPriority;
        sample.thread.state = info.status;
        sample.wait_type = info.waitType; sample.wait_id = info.waitId;
        if ((info.status & kWaiting) && info.waitType && info.waitId == barrier) return sample;
        if (sample.thread.wall_us - start >= 2000000) { sample.thread.rc = -1; return sample; }
        const int rc = sceKernelDelayThread(1000);
        if (rc != 0) { sample.thread.rc = rc; return sample; }
    } while (true);
}
static bool run_core(FILE* log, unsigned core) {
    WorkerArgs args{core, sceKernelCreateSema("clock-command", 0, 0, 1, nullptr),
                          sceKernelCreateSema("clock-done", 0, 0, 1, nullptr)};
    if (args.command < 0 || args.done < 0) {
        std::fprintf(log, "error semaphores command=%d done=%d\n", args.command, args.done);
        return false;
    }
    const int tid = sceKernelCreateThread("clock-worker", worker, 0x10000100, 64 * 1024,
                                          0, int(0x10000u << core), nullptr);
    if (tid < 0) { std::fprintf(log, "error create_thread=%d\n", tid); return false; }
    int rc = sceKernelStartThread(tid, sizeof args, &args);
    if (rc == 0) rc = wait_sema(args.done);
    if (rc != 0) { std::fprintf(log, "error start/ready=%d\n", rc); return false; }
    const int pin = pin_rc.load(std::memory_order_acquire);
    for (unsigned pair = 0; pair < 3; ++pair) {
        for (unsigned step = 0; step < 2; ++step) {
            const int command = ((pair + step) % 2 == 0) ? Busy : Sleep;
            const auto a = blocked_sample(tid, core + 1, args.command);
            if (a.thread.rc != 0) { std::fprintf(log, "error snapshot_begin=%d\n", a.thread.rc); return false; }
            phase.store(command, std::memory_order_release);
            rc = sceKernelSignalSema(args.command, 1);
            if (rc == 0) rc = wait_sema(args.done);
            if (rc != 0) { std::fprintf(log, "error phase_wait=%d\n", rc); return false; }
            const auto b = blocked_sample(tid, core + 1, args.command);
            const int work_rc = phase_rc.load(std::memory_order_acquire);
            char raw[1200];
            hw::diag::format_thread_window(raw, sizeof raw, "clock-worker", "calibration", 0, 0, 0, 0,
                                           a.thread, b.thread);
            std::fprintf(log, "core=%u pair=%u step=%u phase=%s target_us=%u pin_rc=%d work_rc=%d "
                "integrity=%d core_target=%s config_stable=%d wait_type=%u:%u wait_id=%d:%d "
                "barrier=%d arm_mhz=%d:%d unit=unverified %s\n", core, pair, step,
                command == Busy ? "busy" : "sleep", kDurationUs, pin, work_rc,
                work_rc == 0 && integrity(a, b, args.command),
                target_verified(core, pin, a, b) ? "verified" : "unverified", config_stable(a, b),
                a.wait_type, b.wait_type, a.wait_id, b.wait_id, args.command, a.cpu_mhz, b.cpu_mhz, raw);
            std::fflush(log);
            if (b.thread.rc != 0 || work_rc != 0) return false;
        }
    }
    phase.store(Stop, std::memory_order_release);
    rc = sceKernelSignalSema(args.command, 1);
    int exit_status = -1;
    SceUInt timeout = 2000000;
    if (rc == 0) rc = sceKernelWaitThreadEnd(tid, &exit_status, &timeout);
    if (rc != 0 || exit_status != 0) {
        std::fprintf(log, "error join=%d worker_status=%d\n", rc, exit_status); return false;
    }
    const int del_thread = sceKernelDeleteThread(tid);
    const int del_command = sceKernelDeleteSema(args.command);
    const int del_done = sceKernelDeleteSema(args.done);
    std::fprintf(log, "cleanup core=%u rc=%d/%d/%d\n", core, del_thread, del_command, del_done);
    return del_thread == 0 && del_command == 0 && del_done == 0;
}
int main() {
    sceIoMkdir("ux0:data/hotwheels", 0777);
    FILE* log = std::fopen("ux0:data/hotwheels/clock-calibration.log", "w");
    if (!log) { sceKernelExitProcess(1); return 1; }
    std::fputs("HW075 raw clock contrast v1; standalone launch is opt-in; no unit conversion; "
               "one worker; observer intervals include dispatch/wakeup; no PMU\n", log);
    bool ok = true;
    for (unsigned core = 0; core < 3 && ok; ++core) ok = run_core(log, core);
    std::fprintf(log, "finished=%d unit=unverified\n", ok);
    std::fclose(log);
    sceKernelExitProcess(ok ? 0 : 1);
    return ok ? 0 : 1;
}
