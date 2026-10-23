#include "winx86-dx/shims/base.h"

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>

using namespace winx86::dx::shims;

struct FlatGuest final : GuestCpu {
    std::vector<uint8_t> memory = std::vector<uint8_t>(256);
    std::vector<uint32_t> arguments{0x20};
    bool probe_pe32_wrap = false;
    mutable uint32_t low_reads = 0;
    uint32_t arg(size_t index) const override { return index < arguments.size() ? arguments[index] : 0; }
    bool read(uint32_t address, void* out, uint32_t size) const override {
        if (probe_pe32_wrap && address == UINT32_MAX && size == 1) {
            *static_cast<uint8_t*>(out) = 'A';
            return true;
        }
        if (address < memory.size()) ++low_reads;
        if (uint64_t(address) + size > memory.size()) return false;
        std::memcpy(out, memory.data() + address, size);
        return true;
    }
    bool write(uint32_t address, const void* in, uint32_t size) override {
        if (uint64_t(address) + size > memory.size()) return false;
        std::memcpy(memory.data() + address, in, size);
        return true;
    }
};

static_assert(!std::is_copy_constructible_v<GuestClock> && !std::is_move_constructible_v<GuestClock>);
static_assert(!std::is_copy_constructible_v<MsgQueue> && !std::is_move_constructible_v<MsgQueue>);

int main() {
    FlatGuest guest;
    uint32_t input = 0x12345678, output = 0;
    assert(guest.write(guest.arg(0), &input, sizeof input));
    assert(guest.read(guest.arg(0), &output, sizeof output) && output == input);
    assert(!guest.read(253, &output, sizeof output)); // guest range failure is explicit
    assert(!guest.read(UINT32_MAX, &output, sizeof output));
    assert(!guest.write(UINT32_MAX - 1, &input, sizeof input)); // reject PE32 wrap before host pointer arithmetic

    ShimSpec query{"KERNEL32.dll", "QueryValue", 1, true, [](GuestCpu& c) {
        uint32_t value = 0xAABBCCDD;
        return c.write(c.arg(0), &value, sizeof value) ? 1u : 0u;
    }};
    assert(query.dll == "KERNEL32.dll" && query.name == "QueryValue" && query.argc == 1);
    assert(query.stdcall_cleanup && query.invoke(guest) == 1);
    assert(guest.read(guest.arg(0), &output, sizeof output) && output == 0xAABBCCDD);

    BaseConfig profile{"sample.exe", "C:\\GAMES\\sample.exe", "C:\\WINDOWS\\SYSTEM32", 0x400000, 123,
                       {"kernel32.dll", "user32.dll"}, {"DATA", "FAT", 0xCAFEu, 0x6u},
                       0x0A280105u, 0x00F10004u, 0x00010010u, 0xC001u};
    assert(profile.exe_name == "sample.exe" && profile.exe_path == "C:\\GAMES\\sample.exe");
    assert(profile.pid == 123 && profile.known_dlls.size() == 2);
    assert(profile.fallback_volume.label == "DATA" && profile.version == 0x0A280105u);

    FlatGuest import_guest;
    import_guest.memory.resize(4096);
    winx86::dx::io::Vfs vfs;
    vfs.mount('C', "/tmp/sample-game");
    CdDrive sample_cd(CdDriveConfig{'E', "SAMPLECD", "CDFS", 0x1234u, 750, {}});
    GuestClock import_clock;
    uint64_t import_now = 1000000;
    import_clock.set_source([&] { return import_now; });
    MsgQueue import_queue;
    BaseServices services;
    uint32_t empty_peeks = 0;
    services.empty_peek = [&] { ++empty_peeks; };
    services.trap_for = [](const std::string&, const std::string&) { return 0xABCDEFu; };
    services.set_last_error = [](uint32_t) {};
    services.wall_ms = [] { return int64_t(0); };
    auto imports = std::make_shared<BaseContext>(vfs, sample_cd, import_clock, import_queue, profile, services);
    std::map<std::string, ShimSpec> built;
    build_base(imports, [&](ShimSpec spec) {
        const std::string key = spec.dll + "!" + spec.name;
        assert(built.emplace(key, std::move(spec)).second);
    });
    imports.reset(); // registered thunks retain their per-game config/services
    assert(built.at("KERNEL32.dll!GetVersion").invoke(import_guest) == profile.version);
    import_guest.arguments = {0, 512, 128};
    assert(built.at("KERNEL32.dll!GetModuleFileNameA").invoke(import_guest) == profile.exe_path.size());
    assert(std::string(reinterpret_cast<char*>(import_guest.memory.data() + 512)) == profile.exe_path);
    import_guest.probe_pe32_wrap = true;
    import_guest.low_reads = 0;
    import_guest.arguments = {UINT32_MAX};
    assert(built.at("KERNEL32.dll!GetModuleHandleA").invoke(import_guest) == 0);
    assert(import_guest.low_reads == 0); // a wrapped string VA must not read low guest memory
    import_guest.probe_pe32_wrap = false;
    import_guest.low_reads = 0;
    import_guest.arguments = {UINT32_MAX - 8};
    assert(built.at("USER32.dll!RegisterClassA").invoke(import_guest) == 0);
    assert(import_guest.low_reads == 0); // struct field offsets must be range-checked before access

    import_guest.arguments = {0x100, 0, 0, 0, 0};
    assert(built.at("USER32.dll!PeekMessageA").invoke(import_guest) == 0);
    assert(empty_peeks == 1); // only an empty queue enters the adapter's idle seam
    import_queue.post({0x11, 0x12, 0x13, 0x14, 0x15});
    import_guest.arguments[4] = 1; // PM_REMOVE
    assert(built.at("USER32.dll!PeekMessageA").invoke(import_guest) == 1);
    assert(empty_peeks == 1);
    uint32_t raw_msg[7]{};
    assert(import_guest.read(0x100, raw_msg, sizeof raw_msg));
    assert(raw_msg[0] == 0x11 && raw_msg[1] == 0x12 && raw_msg[2] == 0x13 &&
           raw_msg[3] == 0x14 && raw_msg[4] == 0x15);

    MsgQueue no_idle_queue;
    GuestClock no_idle_clock;
    BaseServices no_idle_services;
    auto no_idle_context = std::make_shared<BaseContext>(vfs, sample_cd, no_idle_clock,
                                                         no_idle_queue, profile, no_idle_services);
    std::map<std::string, ShimSpec> no_idle_imports;
    build_base(no_idle_context, [&](ShimSpec spec) {
        no_idle_imports.emplace(spec.dll + "!" + spec.name, std::move(spec));
    });
    assert(no_idle_imports.at("USER32.dll!PeekMessageA").invoke(import_guest) == 0);
    assert(empty_peeks == 1); // an absent service preserves the old no-op path

    CdDrive cd(CdDriveConfig{'E', "GAMECD", "CDFS", 0x12345678u, 150, {75, 150}});
    assert(cd.letter == 'E' && cd.label == "GAMECD" && cd.filesystem == "CDFS" && cd.serial == 0x12345678u);
    assert(cd.data_track_frames == 150 && cd.audio_track_frames == std::vector<uint32_t>({75, 150}));
    const auto toc = cd.read_toc();
    assert(toc.size() == 4 + 8 * 4);
    assert(toc[2] == 1 && toc[3] == 3); // data track + two configured audio tracks
    assert(toc[4 + 2] == 1 && toc[4 + 8 + 2] == 2 && toc[4 + 16 + 2] == 3);
    assert(toc[4 + 24 + 2] == 0xAA); // lead-out follows configured tracks
    assert(toc[4 + 8 + 5] == 0 && toc[4 + 8 + 6] == 4 && toc[4 + 8 + 7] == 0);
    assert(toc[4 + 16 + 5] == 0 && toc[4 + 16 + 6] == 5 && toc[4 + 16 + 7] == 0);
    assert(toc[4 + 24 + 5] == 0 && toc[4 + 24 + 6] == 7 && toc[4 + 24 + 7] == 0);
    std::vector<uint8_t> ioctl;
    assert(cd.ioctl(0x24000, ioctl) == 0 && ioctl == toc);
    assert(cd.ioctl(0xDEADBEEF, ioctl) == 1 && ioctl.empty());

    uint64_t now = 500000;
    GuestClock clock;
    clock.set_source([&] { return now; });
    assert(clock.qpc() == 1);
    ++now;
    assert(clock.qpc() >= 2);
    now += 999999;
    assert(clock.ms() == 1000);
    clock.spin_idle_us = 500;
    clock.idle = [&] { now += 1000; };
    (void)clock.qpc();
    (void)clock.qpc(); // repeated source sample invokes the configured idle callback
    assert(clock.qpc_calls() >= 4 && clock.idles() >= 1);

    now += 10000000;
    GuestClock::Stats stats{};
    (void)clock.take_stats(stats); // establish metrics window
    now += 10000000;
    (void)clock.qpc();
    assert(clock.take_stats(stats));
    assert(stats.window_us >= 10000000 && stats.qpc_calls >= 1);
    clock.spin_idle_us = 0;
    const uint64_t last_qpc = clock.qpc();
    uint64_t next_source = 7;
    clock.set_source([&] { return next_source; });
    assert(clock.qpc() >= last_qpc); // source reconfiguration preserves QPC monotonicity

    MsgQueue queue;
    Msg message{1, 2, 3, 4, 5}, out{};
    queue.post(message);
    message = {10, 20, 30, 40, 50}; // the queue owns a copy of the posted value
    assert(!queue.empty() && queue.peek(out, false));
    assert(out.hwnd == 1 && out.message == 2 && out.wparam == 3 && out.lparam == 4 && out.time == 5);
    out = {};
    assert(queue.peek(out, true) && queue.empty()); // peek returns a value copy too
    assert(out.hwnd == 1 && out.message == 2 && out.wparam == 3 && out.lparam == 4 && out.time == 5);
    assert(!queue.peek(out, true));

    // HW-070: opt-in trace must report the path selected by the real VFS
    // resolver, including the effective install/save overlay.
    namespace fs = std::filesystem;
    const fs::path trace_root = fs::temp_directory_path() / "hw-image-trace-test";
    fs::remove_all(trace_root);
    fs::create_directories(trace_root / "DATA" / "MENU");
    { std::ofstream settings(trace_root / "DATA" / "MENU" / "settings.dat", std::ios::binary); settings << "fixture"; }
    winx86::dx::io::Vfs trace_vfs;
    trace_vfs.mount('C', trace_root.string());
    CdDrive trace_cd(CdDriveConfig{'E', "TRACECD", "CDFS", 1, 75, {}});
    GuestClock trace_clock;
    MsgQueue trace_queue;
    BaseServices trace_services;
    trace_services.trace_settings = true;
    std::vector<std::string> image_logs;
    trace_services.log = [&](const std::string& line) { image_logs.push_back(line); };
    auto trace_context = std::make_shared<BaseContext>(trace_vfs, trace_cd, trace_clock,
                                                        trace_queue, profile, trace_services);
    std::map<std::string, ShimSpec> trace_imports;
    build_base(trace_context, [&](ShimSpec spec) {
        trace_imports.emplace(spec.dll + "!" + spec.name, std::move(spec));
    });
    FlatGuest trace_guest;
    trace_guest.memory.resize(4096);
    const char settings_guest[] = "C:\\DATA\\MENU\\settings.dat";
    assert(trace_guest.write(0x200, settings_guest, sizeof settings_guest));
    trace_guest.arguments = {0x200, 0x80000000u, 0, 0, 3, 0, 0};
    assert(trace_imports.at("KERNEL32.dll!CreateFileA").invoke(trace_guest) != 0xffffffffu);
    const std::string expected_path = (trace_root / "DATA" / "MENU" / "settings.dat").string();
    assert(std::any_of(image_logs.begin(), image_logs.end(), [&](const std::string& line) {
        return line.find("settings read guest=C:\\DATA\\MENU\\settings.dat resolved=" + expected_path +
                         " source=file ok=1") != std::string::npos;
    }));
    trace_imports.clear();
    trace_context.reset();
    fs::remove_all(trace_root);
}
