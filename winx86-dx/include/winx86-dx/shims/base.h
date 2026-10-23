#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../io/read_stream.h"
#include "../io/vfs.h"

namespace winx86::dx::shims {

struct CdDriveConfig {
    char letter;
    std::string label;
    std::string filesystem;
    uint32_t serial;
    uint32_t data_track_frames;
    std::vector<uint32_t> audio_track_frames;
};

// Portable CD TOC/IOCTL model. Identity, track lengths and drive letter are
// supplied by the owning game's configuration.
class CdDrive {
public:
    explicit CdDrive(CdDriveConfig config);
    std::vector<uint8_t> read_toc() const;
    uint32_t ioctl(uint32_t code, std::vector<uint8_t>& out) const;

    char letter;
    std::string label, filesystem;
    uint32_t serial, data_track_frames;
    std::vector<uint32_t> audio_track_frames;
};

using ClockSource = std::function<uint64_t()>; // monotonic microseconds
using IdleCallback = std::function<void()>;

// QPC and timeGetTime share one microsecond source and per-instance epoch.
// ClockSource and idle callable objects are stored by value; any referenced
// host state captured by them must outlive this clock. Calls are serialized.
// idle is synchronous and non-reentrant; if it yields/releases the GIL it must
// reacquire it before returning to this same guest thread. No concurrent clock
// access is allowed during that wait.
class GuestClock {
public:
    static constexpr uint64_t kFreq = 1000000;
    static constexpr uint64_t kStatsWindowUs = 10000000;
    struct Stats {
        uint64_t window_us = 0;
        uint64_t qpc_calls = 0;
        uint64_t idles = 0;
        uint64_t calls_per_second = 0;
    };

    GuestClock() = default;
    GuestClock(const GuestClock&) = delete;
    GuestClock& operator=(const GuestClock&) = delete;
    GuestClock(GuestClock&&) = delete;
    GuestClock& operator=(GuestClock&&) = delete;

    void set_source(ClockSource source);
    uint64_t qpc(); // nonzero and nondecreasing for a monotonic source
    uint32_t ms();
    bool take_stats(Stats& out); // one sample per 10 seconds of guest time

    uint32_t spin_idle_us = 0;
    // SPEC-059: con spin_deadline la siesta deja de ser incondicional dentro del
    // giro. Se duerme sólo mientras falte más que idle_margin_us para el deadline
    // del giro, y el tramo final lo hace el invitado con lecturas finas: el QPC
    // que lee al salir —y su dt— es el mismo que sin dormir, y se sigue cediendo
    // casi todo el giro. El dueño del juego inyecta el deadline en µs del reloj
    // (p. ej. su propio `prev` + 25 ms); vacío = siesta en cada lectura apretada
    // (comportamiento previo). Ver specs/059-idle-qpc-dt-exacto.md.
    std::function<uint64_t()> spin_deadline;
    uint32_t idle_margin_us = 0; // tramo final que queda en giro real
    IdleCallback idle; // synchronous, same-thread, non-reentrant callback
    uint64_t qpc_calls() const { return qpc_calls_; }
    uint64_t idles() const { return idles_; }

private:
    uint64_t now();
    bool spin_has_room(uint64_t raw) const;
    ClockSource source_;
    uint64_t origin_ = 0, last_qpc_raw_ = 0, last_ = 0;
    uint64_t qpc_calls_ = 0, idles_ = 0, stats_start_ = 0, stats_calls_ = 0, stats_idles_ = 0;
    bool origin_set_ = false, have_last_qpc_ = false, stats_set_ = false;
};

// Host-side message values, not a guest MSG wire struct. Queue operations are
// guest-thread/GIL serialized without locks; host events must cross a consumer
// mailbox/callback and be posted while the guest owns the GIL.
struct Msg { uint32_t hwnd = 0, message = 0, wparam = 0, lparam = 0, time = 0; };
class MsgQueue {
public:
    MsgQueue() = default;
    MsgQueue(const MsgQueue&) = delete;
    MsgQueue& operator=(const MsgQueue&) = delete;
    MsgQueue(MsgQueue&&) = delete;
    MsgQueue& operator=(MsgQueue&&) = delete;

    void post(const Msg& message) { queue_.push_back(message); }
    bool peek(Msg& out, bool remove);
    bool empty() const { return queue_.empty(); }
private:
    std::vector<Msg> queue_;
};

// Narrow PE32 guest-memory seam. Implementations own neither the guest memory
// nor its scheduler; every address crossing this interface is a 32-bit guest VA.
// Implementations validate the complete range (including overflow past 4 GiB)
// before touching host memory; failed reads/writes return false without access.
class GuestCpu {
public:
    virtual ~GuestCpu() = default;
    virtual uint32_t arg(size_t index) const = 0;
    virtual bool read(uint32_t guest_va, void* host_dst, uint32_t bytes) const = 0;
    virtual bool write(uint32_t guest_va, const void* host_src, uint32_t bytes) = 0;
};

struct ShimSpec {
    using Handler = std::function<uint32_t(GuestCpu&)>;
    std::string dll;
    std::string name;
    uint32_t argc = 0;
    bool stdcall_cleanup = true;
    Handler handler;

    uint32_t invoke(GuestCpu& guest) const { return handler ? handler(guest) : 0; }
};

struct VolumeInfo {
    std::string label;
    std::string filesystem;
    uint32_t serial = 0;
    uint32_t flags = 0;
};

// Per-game facts that used to be hidden in the PE bridge implementation.
// No defaults intentionally: each consumer states its own process/disk policy.
struct BaseConfig {
    std::string exe_name;
    std::string exe_path;
    std::string system_directory;
    uint32_t exe_base = 0;
    uint32_t pid = 0;
    std::vector<std::string> known_dlls;
    VolumeInfo fallback_volume;
    uint32_t version = 0;
    uint32_t first_handle = 0;
    uint32_t first_hwnd = 0;
    uint32_t first_atom = 0;
};

// Host operations are synchronous callbacks invoked by guest-thread thunks.
// Callable targets and captured state must outlive all registered handlers.
// If a callback yields/releases the guest GIL, another guest thread may invoke
// services concurrently; callbacks must synchronize their own shared state.
struct BaseServices {
    std::function<uint32_t(const std::string& dll, const std::string& name)> trap_for;
    std::function<uint32_t(uint32_t guest_va, const std::vector<uint32_t>& args)> call_guest;
    std::function<uint32_t(uint32_t bytes)> alloc_guest;
    std::function<void(const std::string&)> log;
    std::function<void(uint32_t code)> exit;
    std::function<void()> wait;
    std::function<void(uint32_t error)> set_last_error;
    std::function<int64_t()> wall_ms;
    std::function<std::shared_ptr<const std::vector<uint8_t>>(const std::string& path)> read_blob;
    std::function<void(const std::string& path)> invalidate_blob;
    // Optional per-game callback for an empty PeekMessageA poll. Invoked
    // synchronously on the guest thread; any captured state must outlive the
    // registered thunks. Append-only to preserve positional aggregate users.
    std::function<void()> empty_peek;
    // Opt-in audit log for the guest's resolved settings.dat open path.
    bool trace_settings = false;
};

using Registrar = std::function<void(ShimSpec)>;

// A single game's mutable file/clock/message services and value configuration.
// The adapter/owner keeps referenced services alive and serializes calls.
class BaseContext {
public:
    BaseContext(io::Vfs& vfs_, CdDrive& cd_, GuestClock& clock_, MsgQueue& queue_,
                BaseConfig config_, BaseServices services_)
        : vfs(vfs_), cd(cd_), clock(clock_), queue(queue_), config(std::move(config_)),
          services(std::move(services_)) {}
    BaseContext(const BaseContext&) = delete;
    BaseContext& operator=(const BaseContext&) = delete;
    io::Vfs& vfs;
    CdDrive& cd;
    GuestClock& clock;
    MsgQueue& queue;
    BaseConfig config;
    BaseServices services;
};

// Each registered thunk retains this context and its private handles/windows
// state until the consumer releases its registration callback.
void build_base(const std::shared_ptr<BaseContext>& context, const Registrar& registrar);

} // namespace winx86::dx::shims
