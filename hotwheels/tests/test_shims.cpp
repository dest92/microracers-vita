// test_shims.cpp — criterios A1..A8 de specs/006-shims-base.md (host, sin x86).
// Los shims se capturan con build() y se invocan con una CPU falsa de memoria plana.
#include "shims/shims.h"
#include <cassert>
#include <strings.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace hw::shims;

struct FakeCpu final : d2rt::Cpu {
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x400000);
    uint32_t regs[16] = {};
    bool map(uint32_t, uint32_t, const void*, int) override { return true; }
    bool protect(uint32_t, uint32_t, int) override { return true; }
    bool read(uint32_t va, void* d, uint32_t n) override {
        if (uint64_t(va) + n > mem.size()) return false;
        std::memcpy(d, &mem[va], n); return true; }
    bool write(uint32_t va, const void* s, uint32_t n) override {
        if (uint64_t(va) + n > mem.size()) return false;
        std::memcpy(&mem[va], s, n); return true; }
    void set_trap(uint32_t, uint32_t, d2rt::TrapFn) override {}
    bool run(uint32_t, const char**) override { return false; }
    uint32_t fault_addr() const override { return 0; }
    void* hostptr(uint32_t va, uint32_t n) override {
        return uint64_t(va) + n <= mem.size() && va ? &mem[va] : nullptr; }
    uint32_t reg(int r) override { return regs[r]; }
    void set_reg(int r, uint32_t v) override { regs[r] = v; }
};

static FakeCpu cpu;
static std::map<std::string, d2rt::Shim> shims;   // "dll!name" en minúsculas la dll
static uint32_t last_error = 0, heap = 0x200000, waits = 0, idles = 0;
static std::vector<std::vector<uint32_t>> guest_calls;
static uint64_t fake_us = 5000000;

static uint32_t put(const std::string& s) {
    uint32_t a = heap; std::memcpy(&cpu.mem[a], s.c_str(), s.size() + 1); heap += (s.size() + 16) & ~7u; return a; }
static uint32_t buf(uint32_t n) { uint32_t a = heap; std::memset(&cpu.mem[a], 0, n); heap += (n + 15) & ~7u; return a; }
static std::string str(uint32_t a) { return std::string(reinterpret_cast<char*>(&cpu.mem[a])); }
static uint32_t u32(uint32_t a) { return cpu.read_u32(a); }
static uint16_t u16(uint32_t a) { uint16_t v; std::memcpy(&v, &cpu.mem[a], 2); return v; }

static uint32_t call(const std::string& key, std::vector<uint32_t> args) {
    auto it = shims.find(key);
    if (it == shims.end()) { std::fprintf(stderr, "falta shim %s\n", key.c_str()); assert(false); }
    uint32_t esp = 0x100000; cpu.set_reg(d2rt::R_ESP, esp); cpu.write_u32(esp, 0xDEAD0000u);
    for (size_t i = 0; i < args.size(); ++i) cpu.write_u32(esp + 4 + 4 * i, args[i]);
    assert(it->second.argc == args.size());
    return it->second.fn(cpu);
}
#define K(n) "kernel32.dll!" n
#define U(n) "user32.dll!" n

static void write_file(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << data; }

int main() {
#ifdef HW_SHIMS_LINK_INSTALL
    // Fuerza enlazar install.cpp contra winx86 real (build CMake).
    volatile auto keep = &hw_register_shims; (void)keep;
#endif
    // --- fixture de disco: base (sólo lectura), save, cd ---
    fs::path root = fs::temp_directory_path() / ("hw006_" + std::to_string(::getpid()));
    fs::remove_all(root);
    write_file(root / "base/DATA/MENU/SETTINGS.DAT", "0 0 0\n");
    write_file(root / "base/DATA/MENU/A.TGA", "AAAA");
    write_file(root / "base/DATA/MENU/b.tga", "BB");
    write_file(root / "base/DATA/Menu/x.TXT", "x");
    write_file(root / "base/LANG.TXT", "Hot Wheels");
    write_file(root / "cd/HWICON.BMP", "BM");
    fs::create_directories(root / "save");

    Context ctx;
    ctx.vfs.mount('C', (root / "base").string(), (root / "save").string());
    ctx.vfs.mount('D', (root / "cd").string());
    ctx.vfs.set_cwd("C:\\");
    ctx.cd.audio_track_frames = {75 * 180, 75 * 200, 75 * 150, 75 * 210};
    ctx.clock.set_source([] { return fake_us; });
    ctx.trap_for = [](const std::string& dll, const std::string& name) -> uint32_t {
        auto k = dll + "!" + name;
        for (auto& c : k) c = char(std::tolower((unsigned char)c));
        for (auto& [key, s] : shims) {
            auto lk = key; for (auto& c : lk) c = char(std::tolower((unsigned char)c));
            if (lk == k) return 0x7000000u + uint32_t(std::hash<std::string>{}(key) & 0xFFFF0);
        }
        return 0u; };
    ctx.call_guest = [](uint32_t va, const std::vector<uint32_t>& a) {
        std::vector<uint32_t> v{va}; v.insert(v.end(), a.begin(), a.end()); guest_calls.push_back(v); return 0x55u; };
    ctx.alloc_guest = [](uint32_t n) { return buf(n); };
    ctx.log = [](const std::string&) {};
    ctx.exit = [](uint32_t) { assert(!"exit inesperado"); };
    ctx.wait = [] { ++waits; };
    ctx.set_last_error = [](uint32_t e) { last_error = e; };
    ctx.wall_ms = [] { return int64_t(1759363200000); };  // 2025-10-02 00:00:00 UTC
    std::set<std::string> dup;
    build(ctx, [&](const std::string& dll, const std::string& name, d2rt::Shim s) {
        std::string d = dll; for (auto& c : d) c = char(std::tolower((unsigned char)c));
        auto key = d + "!" + name;
        assert(!dup.count(key) && "registro duplicado"); dup.insert(key);
        shims[key] = std::move(s); });

    // --- A8 cobertura ---
    for (const char* n : {"GetLogicalDriveStringsA","GetDriveTypeA","GetVolumeInformationA",
            "CreateToolhelp32Snapshot","Process32First","Process32Next","GetModuleHandleA",
            "LoadLibraryA","GetProcAddress","CreateFileA","ReadFile","WriteFile","SetFilePointer",
            "CloseHandle","FlushFileBuffers","GetFileType","FindFirstFileA","FindClose","DeleteFileA",
            "MoveFileA","FindNextFileA","DeviceIoControl","QueryPerformanceFrequency","QueryPerformanceCounter",
            "GetModuleFileNameA","GetCommandLineA","GetVersion","OutputDebugStringA",
            "SetUnhandledExceptionFilter","ExitProcess","TerminateProcess","GetCurrentProcess",
            "GetLocalTime","GetSystemTime","GetTimeZoneInformation","GetStdHandle"})
        assert(shims.count(std::string("kernel32.dll!") + n));
    for (const char* n : {"MessageBoxA","RegisterClassA","CreateWindowExA","ShowWindow",
            "DispatchMessageA","WaitMessage","PeekMessageA","PostQuitMessage"})
        assert(shims.count(std::string("user32.dll!") + n));
    assert(shims.count("winmm.dll!timeGetTime"));

    // --- A1 Toolhelp vía GetProcAddress ---
    uint32_t hk = call(K("GetModuleHandleA"), {put("KERNEL32.DLL")});
    assert(hk != 0 && hk != ctx.exe_base);
    assert(call(K("GetModuleHandleA"), {0}) == ctx.exe_base);
    for (const char* n : {"CreateToolhelp32Snapshot","Process32First","Process32Next"})
        assert(call(K("GetProcAddress"), {hk, put(n)}) != 0);
    assert(call(K("GetProcAddress"), {hk, put("NoExiste")}) == 0 && last_error == 127);
    assert(call(K("GetModuleHandleA"), {put("nada.dll")}) == 0 && last_error == 126);
    uint32_t snap = call(K("CreateToolhelp32Snapshot"), {2, 0});
    assert(snap != 0 && snap != 0xFFFFFFFFu);
    uint32_t pe = buf(0x128); cpu.write_u32(pe, 0x128);
    int count = 0;
    for (uint32_t ok = call(K("Process32First"), {snap, pe}); ok; ok = call(K("Process32Next"), {snap, pe}))
        if (strcasecmp(str(pe + 36).c_str(), "hotwheels.exe") == 0) ++count;
    assert(count == 1 && last_error == 18);
    uint32_t pe_small = buf(0x128); cpu.write_u32(pe_small, 8);
    assert(call(K("Process32First"), {snap, pe_small}) == 0);
    assert(call(K("CloseHandle"), {snap}) == 1);

    // --- A2 CD ---
    uint32_t drives = buf(32);
    assert(call(K("GetLogicalDriveStringsA"), {32, drives}) == 8);
    assert(std::memcmp(&cpu.mem[drives], "C:\\\0D:\\\0\0", 9) == 0);
    assert(call(K("GetLogicalDriveStringsA"), {4, buf(4)}) == 9);
    assert(call(K("GetDriveTypeA"), {put("D:\\")}) == 5);
    assert(call(K("GetDriveTypeA"), {put("d:")}) == 5);
    assert(call(K("GetDriveTypeA"), {put("C:\\")}) == 3);
    assert(call(K("GetDriveTypeA"), {0}) == 3);
    assert(call(K("GetDriveTypeA"), {put("Q:\\")}) == 1);
    uint32_t fd = buf(320);
    uint32_t fh = call(K("FindFirstFileA"), {put("D:\\hwicon.bmp"), fd});
    assert(fh != 0xFFFFFFFFu && strcasecmp(str(fd + 44).c_str(), "hwicon.bmp") == 0 && u32(fd + 32) == 2);
    assert(call(K("FindClose"), {fh}) == 1);
    uint32_t label = buf(64), fsname = buf(64), serial = buf(4), maxc = buf(4), flags = buf(4);
    assert(call(K("GetVolumeInformationA"), {put("D:\\"), label, 64, serial, maxc, flags, fsname, 64}) == 1);
    assert(str(label) == "HWMR" && str(fsname) == "CDFS" && u32(serial) == 0x1F2E3D4Cu);

    // --- A3 IOCTL ---
    uint32_t dev = call(K("CreateFileA"), {put("\\\\.\\D:"), 0x80000000u, 1, 0, 3, 0, 0});
    assert(dev != 0xFFFFFFFFu);
    uint32_t toc = buf(804), got = buf(4);
    assert(call(K("DeviceIoControl"), {dev, 0x24000, 0, 0, toc, 804, got, 0}) == 1);
    uint32_t ntracks = 1 + 4;
    assert(u32(got) == 4 + 8 * (ntracks + 1));
    assert(((cpu.mem[toc] << 8) | cpu.mem[toc + 1]) == int(2 + 8 * (ntracks + 1)));
    assert(cpu.mem[toc + 2] == 1 && cpu.mem[toc + 3] == ntracks);
    uint32_t t1 = toc + 4;
    assert((cpu.mem[t1 + 1] >> 4) == 4 || (cpu.mem[t1 + 1] & 0x0F) == 4);  // Control datos
    assert(cpu.mem[t1 + 2] == 1 && cpu.mem[t1 + 5] == 0 && cpu.mem[t1 + 6] == 2 && cpu.mem[t1 + 7] == 0);
    assert(cpu.mem[toc + 4 + 8 * ntracks + 2] == 0xAA);
    assert(call(K("DeviceIoControl"), {dev, 0x24008, 0, 0, 0, 0, got, 0}) == 1);
    assert(call(K("DeviceIoControl"), {dev, 0x2404C, 0, 0, toc, 804, got, 0}) == 0 && last_error == 1);
    assert(call(K("CloseHandle"), {dev}) == 1);

    // --- A4 Vfs / A5 archivos ---
    auto open = [&](const std::string& p, uint32_t acc, uint32_t disp) {
        return call(K("CreateFileA"), {put(p), acc, 1, 0, disp, 0x80, 0}); };
    uint32_t h = open("data\\menu\\settings.dat", 0x80000000u, 3);
    assert(h != 0xFFFFFFFFu);
    assert(call(K("GetFileType"), {h}) == 1);
    assert(call(K("SetFilePointer"), {h, 0, 0, 2}) == 6);
    assert(call(K("SetFilePointer"), {h, 0, 0, 0}) == 0);
    uint32_t rb = buf(16), nread = buf(4);
    assert(call(K("ReadFile"), {h, rb, 16, nread, 0}) == 1 && u32(nread) == 6);
    assert(std::memcmp(&cpu.mem[rb], "0 0 0\n", 6) == 0);
    assert(call(K("CloseHandle"), {h}) == 1);
    assert(open("C:\\data\\..\\DATA\\Menu\\X.txt", 0x80000000u, 3) != 0xFFFFFFFFu);
    assert(open("data\\nada.dat", 0x80000000u, 3) == 0xFFFFFFFFu && last_error == 2);
    assert(open("data\\menu\\settings.dat", 0x40000000u, 1) == 0xFFFFFFFFu && last_error == 80);
    h = open("data\\menu\\settings.dat", 0x40000000u, 2);
    assert(h != 0xFFFFFFFFu);
    uint32_t wbuf = put("1 1 1\n"), nw = buf(4);
    assert(call(K("WriteFile"), {h, wbuf, 6, nw, 0}) == 1 && u32(nw) == 6);
    assert(call(K("FlushFileBuffers"), {h}) == 1);
    assert(call(K("CloseHandle"), {h}) == 1);
    assert(fs::exists(root / "save/data/menu/settings.dat") || fs::exists(root / "save/DATA/MENU/SETTINGS.DAT"));
    { std::ifstream f(root / "base/DATA/MENU/SETTINGS.DAT"); std::string s; std::getline(f, s); assert(s == "0 0 0"); }
    h = open("DATA\\MENU\\SETTINGS.DAT", 0x80000000u, 3);
    assert(call(K("ReadFile"), {h, rb, 16, nread, 0}) == 1 && std::memcmp(&cpu.mem[rb], "1 1 1", 5) == 0);
    call(K("CloseHandle"), {h});
    std::set<std::string> found;
    fh = call(K("FindFirstFileA"), {put("data\\menu\\*.TGA"), fd});
    assert(fh != 0xFFFFFFFFu);
    found.insert(str(fd + 44));
    while (call(K("FindNextFileA"), {fh, fd})) found.insert(str(fd + 44));
    assert(last_error == 18);
    call(K("FindClose"), {fh});
    assert(found == (std::set<std::string>{"A.TGA", "b.tga"}));
    assert(call(K("GetFileType"), {call(K("GetStdHandle"), {0xFFFFFFF5u})}) == 2);
    assert(call(K("DeleteFileA"), {put("data\\menu\\settings.dat")}) == 1);
    h = open("data\\menu\\settings.dat", 0x80000000u, 3);
    call(K("ReadFile"), {h, rb, 16, nread, 0}); call(K("CloseHandle"), {h});
    assert(std::memcmp(&cpu.mem[rb], "0 0 0", 5) == 0);  // vuelve a ver la base

    // --- A6 reloj ---
    uint32_t li = buf(8);
    assert(call(K("QueryPerformanceFrequency"), {li}) == 1 && u32(li) == 1000000 && u32(li + 4) == 0);
    assert(call(K("QueryPerformanceCounter"), {li}) == 1 && (u32(li) | u32(li + 4)) != 0);
    uint64_t q0 = u32(li) | (uint64_t(u32(li + 4)) << 32);
    uint32_t t0 = call("winmm.dll!timeGetTime", {});
    fake_us += 123456;
    call(K("QueryPerformanceCounter"), {li});
    uint64_t q1 = u32(li) | (uint64_t(u32(li + 4)) << 32);
    uint32_t t1v = call("winmm.dll!timeGetTime", {});
    assert(q1 > q0);
    long dq = long((q1 - q0) / 1000), dt = long(t1v - t0);
    assert(dq - dt <= 1 && dt - dq <= 1);
    // timerWaitFrame40Hz: dt = Δqpc * 163840 / QPF, spin hasta >= 0x1000.
    call(K("QueryPerformanceCounter"), {li});
    uint64_t start = u32(li) | (uint64_t(u32(li + 4)) << 32), now = start;
    uint64_t start_us = fake_us;
    while ((now - start) * 163840 / 1000000 < 0x1000) {
        fake_us += 100;
        call(K("QueryPerformanceCounter"), {li});
        now = u32(li) | (uint64_t(u32(li + 4)) << 32);
    }
    assert(fake_us - start_us >= 25000 && fake_us - start_us <= 25100);
    ctx.clock.spin_idle_us = 500; ctx.clock.idle = [] { ++idles; fake_us += 1000; };
    call(K("QueryPerformanceCounter"), {li}); call(K("QueryPerformanceCounter"), {li});
    assert(idles >= 1);
    // HW-056: contadores para medir el spin (llamadas QPC y siestas por ventana).
    assert(ctx.clock.qpc_calls() >= 2 && ctx.clock.idles() >= 1);
    { std::string line; fake_us += 11000000;  // la ventana la abrió el shim QPC
      assert(ctx.clock.take_stats_line(line) && line.find("qpc_calls=") != std::string::npos
             && line.find("idles=") != std::string::npos);
      assert(!ctx.clock.take_stats_line(line));  // como mucho una vez cada 10 s
    }
    ctx.clock.spin_idle_us = 0;

    // --- A6.b (SPEC-059 / HW-056): la siesta del giro de QPC no debe mover el dt ---
    {
        // El invitado gira sobre QPC hasta que dt = trunc(delta·163,84) >= 0x1000
        // (25 ms) tomando el delta desde su `prev` (la lectura final del giro
        // anterior). El giro real avanza la fuente step_us por lectura; cada
        // siesta del idle avanza sleep_us + jitter (1 ms nominal + scheduler).
        // Entre esperas el juego trabaja work_us sin leer QPC: la lectura de
        // entrada al giro no es "apretada" y cierra el tramo anterior.
        const uint32_t step_us = 2, sleep_us = 1027, work_us = 5000, spin_us = 300;
        struct Arm { uint32_t idles; uint64_t calls; };
        uint64_t jitter = 0;
        auto run = [&](uint32_t spin_idle, bool deadline, uint32_t margin_us,
                       std::vector<uint32_t>& dts) {
            uint64_t src = 1000000;
            GuestClock clk;
            clk.set_source([&] { return src; });
            clk.spin_idle_us = spin_idle;
            clk.idle_margin_us = margin_us;
            clk.idle = [&] { src += sleep_us + (++jitter * 13) % 31; };
            uint64_t prev = clk.qpc();                 // el `prev` que el juego publica
            if (deadline) clk.spin_deadline = [&] { return prev + 25000; };
            for (int i = 0; i < 6; ++i) {
                src += work_us;
                uint64_t now = clk.qpc();
                while ((now - prev) * 163840 / 1000000 < 0x1000) { src += step_us; now = clk.qpc(); }
                dts.push_back(uint32_t((now - prev) * 163840 / 1000000));
                prev = now;                            // al salir: prev = actual
            }
            return Arm{uint32_t(clk.idles()), clk.qpc_calls()};
        };
        auto all_frame = [](const std::vector<uint32_t>& v) {
            if (v.empty()) return false;
            for (uint32_t dt : v) if (dt != 0x1000) return false;
            return true; };
        std::vector<uint32_t> libre, siesta, exacto;
        const Arm a_libre = run(0, false, 0, libre);           // referencia: giro puro
        const Arm a_siesta = run(spin_us, false, 0, siesta);   // HW-056: siesta incondicional
        const Arm a_exacto = run(spin_us, true, 2000, exacto); // SPEC-059: deadline del timer
        assert(libre.size() == 6 && all_frame(libre));
        assert(!all_frame(siesta) && a_siesta.idles > 0);      // el defecto medido
        assert(exacto.size() == 6 && all_frame(exacto));       // el fix: dt exacto
        assert(a_exacto.idles >= 100);                         // y se cede casi todo el giro
        assert(a_exacto.calls < a_libre.calls / 3);            // mucho menos que el giro puro
    }

    // --- A7 ventana / mensajes ---
    uint32_t wc = buf(40), cls = put("HotWheelsClass");
    cpu.write_u32(wc + 4, 0x4507a0); cpu.write_u32(wc + 36, cls);
    assert(call(U("RegisterClassA"), {wc}) != 0);
    assert(call(U("RegisterClassA"), {wc}) == 0 && last_error == 1410);
    assert(call(U("CreateWindowExA"), {0, put("nada"), 0, 0, 0, 0, 640, 480, 0, 0, 0, 0}) == 0);
    uint32_t hwnd = call(U("CreateWindowExA"), {0, put("hotwheelsclass"), put("t"), 0x80000000u, 0, 0, 640, 480, 0, 0, 0x400000, 0});
    assert(hwnd != 0);
    uint32_t msg = buf(28);
    assert(call(U("PeekMessageA"), {msg, 0, 0, 0, 1}) == 0);
    assert(call(U("ShowWindow"), {hwnd, 1}) == 0);
    assert(call(U("PeekMessageA"), {msg, 0, 0, 0, 0}) == 1);           // sin sacar
    assert(call(U("PeekMessageA"), {msg, 0, 0, 0, 1}) == 1);
    assert(u32(msg) == hwnd && u32(msg + 4) == 6 && u32(msg + 8) == 1);
    assert(call(U("DispatchMessageA"), {msg}) == 0x55);
    assert(guest_calls.size() == 1 && guest_calls[0] == (std::vector<uint32_t>{0x4507a0, hwnd, 6, 1, 0}));
    assert(call(U("PeekMessageA"), {msg, 0, 0, 0, 1}) == 0);
    assert(call(U("WaitMessage"), {}) == 1 && waits == 1);
    call(U("PostQuitMessage"), {7});
    assert(call(U("PeekMessageA"), {msg, 0, 0, 0, 1}) == 1 && u32(msg + 4) == 0x12 && u32(msg + 8) == 7);
    assert(call(U("ShowWindow"), {hwnd, 1}) == 1);

    // --- varios KERNEL32 ---
    assert(call(K("GetVersion"), {}) == 0x0A280105u);
    assert(call(K("GetCurrentProcess"), {}) == 0xFFFFFFFFu);
    uint32_t cl = call(K("GetCommandLineA"), {});
    assert(cl && str(cl) == "\"C:\\HotWheels.exe\"" && call(K("GetCommandLineA"), {}) == cl);
    uint32_t mf = buf(64);
    assert(call(K("GetModuleFileNameA"), {0, mf, 64}) == 16 && str(mf) == "C:\\HotWheels.exe");
    assert(call(K("SetUnhandledExceptionFilter"), {0x1234}) == 0);
    assert(call(K("SetUnhandledExceptionFilter"), {0}) == 0x1234);
    uint32_t st = buf(16);
    call(K("GetSystemTime"), {st});
    assert(u16(st) == 2025 && u16(st + 2) == 10 && u16(st + 6) == 2 && u16(st + 4) == 4);
    assert(call(K("GetTimeZoneInformation"), {buf(172)}) == 0);
    assert(call(U("MessageBoxA"), {0, put("hola"), put("t"), 0}) == 1);

    fs::remove_all(root);
    std::puts("test_shims OK");
    return 0;
}
