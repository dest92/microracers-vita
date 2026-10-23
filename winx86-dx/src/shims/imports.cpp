// Generic KERNEL32/USER32/WINMM import semantics, independent of a PE bridge.
#include "../../include/winx86-dx/shims/base.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include "../../include/winx86-dx/io/read_stream.h"

namespace fs = std::filesystem;
namespace winx86::dx::shims {

constexpr uint32_t kInvalid = 0xFFFFFFFFu;
enum Err : uint32_t { kFileNotFound = 2, kPathNotFound = 3, kAccessDenied = 5, kBadHandle = 6,
    kInvalidFunction = 1, kNoMoreFiles = 18, kBadLength = 24, kFileExists = 80,
    kInsufficient = 122, kModNotFound = 126, kProcNotFound = 127, kNegativeSeek = 131,
    kNoAccess = 998,
    kClassExists = 1410, kNoClass = 1407 };

struct State {
    enum class Kind { File, Find, Snap, CdDevice };
    struct Handle {
        Kind kind; FILE* f = nullptr; std::unique_ptr<io::ReadStream> rs; std::string path; std::vector<io::Vfs::Entry> found; size_t cur = 0;
        Handle(Kind k, FILE* fp = nullptr) : kind(k), f(fp) {}
    };
    std::map<uint32_t, Handle> handles;
    uint32_t next_handle;
    struct Window { uint32_t wndproc = 0; bool visible = false; };
    std::map<std::string, uint32_t> classes;  // nombre en minúsculas -> wndproc
    std::map<uint32_t, Window> windows;
    uint32_t next_hwnd, next_atom;
    uint32_t uef = 0, cmdline = 0;
    explicit State(const BaseConfig& config)
        : next_handle(config.first_handle), next_hwnd(config.first_hwnd), next_atom(config.first_atom) {}
    uint32_t add(Handle h) { uint32_t v = next_handle; next_handle += 4; handles.emplace(v, std::move(h)); return v; }
    ~State() {
        for (auto& [v, h] : handles) {
            h.rs.reset(); // ReadStream borrows the FILE*; destroy the borrower first.
            if (h.f) std::fclose(h.f);
        }
    }
};

namespace {
constexpr uint32_t kDllBase = 0x7FF00000u, kDllStep = 0x10000u;
constexpr uint32_t kStdIn = 0x00F00001u, kStdOut = 0x00F00002u, kStdErr = 0x00F00003u;

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

bool guest_address(uint32_t base, uint32_t offset, uint32_t bytes, uint32_t& out) {
    // Both operands are bounded to 32 bits before promotion, so this sum cannot
    // overflow uint64_t. Check the narrowed PE32 domain and full range next.
    const uint64_t address = uint64_t(base) + uint64_t(offset);
    if (address > UINT32_MAX || uint64_t(bytes) > (uint64_t(UINT32_MAX) + 1 - address)) return false;
    out = uint32_t(address);
    return true;
}
bool read_at(const GuestCpu& c, uint32_t base, uint32_t offset, void* dst, uint32_t bytes) {
    uint32_t address;
    return guest_address(base, offset, bytes, address) && c.read(address, dst, bytes);
}
bool write_at(GuestCpu& c, uint32_t base, uint32_t offset, const void* src, uint32_t bytes) {
    uint32_t address;
    return guest_address(base, offset, bytes, address) && c.write(address, src, bytes);
}
bool read_u32_at(const GuestCpu& c, uint32_t base, uint32_t offset, uint32_t& out) {
    return read_at(c, base, offset, &out, sizeof out);
}
bool write_u32_at(GuestCpu& c, uint32_t base, uint32_t offset, uint32_t value) {
    return write_at(c, base, offset, &value, sizeof value);
}
bool gstr(GuestCpu& c, uint32_t a, std::string& out, size_t max = 1024) {
    out.clear();
    if (!a) return true;
    for (size_t i = 0; i < max; ++i) {
        uint32_t address;
        char ch;
        if (i > UINT32_MAX || !guest_address(a, uint32_t(i), 1, address) || !c.read(address, &ch, 1)) return false;
        if (!ch) return true;
        out += ch;
    }
    return true;
}
std::string gstr(GuestCpu& c, uint32_t a, size_t max = 1024) {
    std::string s;
    (void)gstr(c, a, s, max);
    return s;
}
bool gput(GuestCpu& c, uint32_t a, const std::string& s) {
    if (s.size() >= UINT32_MAX) return false;
    return write_at(c, a, 0, s.c_str(), uint32_t(s.size() + 1));
}
bool gput_at(GuestCpu& c, uint32_t base, uint32_t offset, const std::string& s) {
    if (s.size() >= UINT32_MAX) return false;
    return write_at(c, base, offset, s.c_str(), uint32_t(s.size() + 1));
}
bool gzero(GuestCpu& c, uint32_t a, uint32_t n) {
    std::vector<uint8_t> z(n, 0);
    return write_at(c, a, 0, z.data(), n);
}
bool gzero_at(GuestCpu& c, uint32_t base, uint32_t offset, uint32_t n) {
    std::vector<uint8_t> z(n, 0);
    return write_at(c, base, offset, z.data(), n);
}
bool put64(GuestCpu& c, uint32_t a, uint64_t v) { return write_at(c, a, 0, &v, 8); }

int dll_index(std::string name, const std::vector<std::string>& dlls) {
    name = lower(name);
    auto slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.find('.') == std::string::npos) name += ".dll";
    for (int i = 0; i < int(dlls.size()); ++i) if (name == lower(dlls[i])) return i;
    return -1;
}
int dll_of_handle(uint32_t h, size_t dll_count) {
    if (h < kDllBase || (h - kDllBase) % kDllStep) return -1;
    uint32_t i = (h - kDllBase) / kDllStep;
    return i < dll_count ? int(i) : -1;
}

// SYSTEMTIME desde ms UNIX (UTC), algoritmo civil de H. Hinnant.
bool systemtime(GuestCpu& c, uint32_t a, int64_t ms) {
    int64_t days = ms / 86400000, rem = ms % 86400000;
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = unsigned(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    int64_t y = int64_t(yoe) + era * 400 + (m <= 2);
    uint16_t st[8] = {uint16_t(y), uint16_t(m), uint16_t((days + 4) % 7), uint16_t(d),
                      uint16_t(rem / 3600000), uint16_t(rem / 60000 % 60), uint16_t(rem / 1000 % 60),
                      uint16_t(rem % 1000)};
    return write_at(c, a, 0, st, 16);
}
} // namespace

void build_base(const std::shared_ptr<BaseContext>& context, const Registrar& reg) {
    if (!context) return;
    auto state = std::make_shared<State>(context->config);
    State& st = *state;
    BaseContext* cx = context.get();
    auto err = [cx](uint32_t e) { if (cx->services.set_last_error) cx->services.set_last_error(e); };
    auto log = [cx](const std::string& s) { if (cx->services.log) cx->services.log(s); };
    auto R = [&](const char* dll, const char* name, uint32_t argc, std::function<uint32_t(GuestCpu&)> fn) {
        ShimSpec s; s.dll = dll; s.name = name; s.argc = argc; s.stdcall_cleanup = true;
        s.handler = [context, state, fn = std::move(fn)](GuestCpu& guest) mutable { return fn(guest); };
        reg(std::move(s));
    };
    auto K = [&](const char* n, uint32_t argc, std::function<uint32_t(GuestCpu&)> fn) { R("KERNEL32.dll", n, argc, std::move(fn)); };
    auto U = [&](const char* n, uint32_t argc, std::function<uint32_t(GuestCpu&)> fn) { R("USER32.dll", n, argc, std::move(fn)); };
    auto handle = [&st](uint32_t h, State::Kind k) -> State::Handle* {
        auto it = st.handles.find(h);
        return it != st.handles.end() && it->second.kind == k ? &it->second : nullptr;
    };
    auto fill_find = [](GuestCpu& c, uint32_t fd, const io::Vfs::Entry& e) {
        return gzero(c, fd, 320) && write_u32_at(c, fd, 0, e.dir ? 0x10u : 0x20u) &&
               write_u32_at(c, fd, 32, uint32_t(e.size)) && gput_at(c, fd, 44, e.name.substr(0, 259));
    };

    // ---------------- unidades / CD ----------------
    auto letters = [cx] {
        auto v = cx->vfs.drive_letters();
        char cd = char(std::toupper((unsigned char)cx->cd.letter));
        if (std::find(v.begin(), v.end(), cd) == v.end()) v.push_back(cd);
        std::sort(v.begin(), v.end());
        return v;
    };
    K("GetLogicalDriveStringsA", 2, [letters](GuestCpu& c) {
        std::string s;
        for (char l : letters()) { s += l; s += ":\\"; s += '\0'; }
        s += '\0';
        uint32_t need = uint32_t(s.size());
        if (c.arg(0) < need || !c.arg(1)) return need;
        c.write(c.arg(1), s.data(), need);
        return need - 1;
    });
    K("GetDriveTypeA", 1, [cx](GuestCpu& c) -> uint32_t {
        std::string r = gstr(c, c.arg(0), 8);
        char l = r.empty() ? cx->vfs.cwd_drive() : char(std::toupper((unsigned char)r[0]));
        if (!r.empty() && (r.size() < 2 || r[1] != ':')) return 1;
        if (l == std::toupper((unsigned char)cx->cd.letter)) return 5;  // DRIVE_CDROM
        return cx->vfs.has_drive(l) ? 3 : 1;                            // FIXED / NO_ROOT_DIR
    });
    K("GetVolumeInformationA", 8, [cx, err](GuestCpu& c) -> uint32_t {
        std::string r = gstr(c, c.arg(0), 8);
        char l = r.empty() ? cx->vfs.cwd_drive() : char(std::toupper((unsigned char)r[0]));
        std::string label, fsn; uint32_t serial, flags;
        if (l == std::toupper((unsigned char)cx->cd.letter)) {
            label = cx->cd.label; fsn = cx->cd.filesystem; serial = cx->cd.serial; flags = 0x80006u;
        } else if (cx->vfs.has_drive(l)) {
            label = cx->config.fallback_volume.label; fsn = cx->config.fallback_volume.filesystem; serial = cx->config.fallback_volume.serial; flags = cx->config.fallback_volume.flags;
        } else { err(kPathNotFound); return 0; }
        if (c.arg(1) && c.arg(2) > label.size()) gput(c, c.arg(1), label);
        if (c.arg(3)) (void)write_u32_at(c, c.arg(3), 0, serial);
        if (c.arg(4)) (void)write_u32_at(c, c.arg(4), 0, 110);
        if (c.arg(5)) (void)write_u32_at(c, c.arg(5), 0, flags);
        if (c.arg(6) && c.arg(7) > fsn.size()) gput(c, c.arg(6), fsn);
        return 1;
    });
    K("DeviceIoControl", 8, [cx, handle, err](GuestCpu& c) -> uint32_t {
        if (!handle(c.arg(0), State::Kind::CdDevice)) { err(kBadHandle); return 0; }
        std::vector<uint8_t> out;
        if (uint32_t e = cx->cd.ioctl(c.arg(1), out)) { err(e); return 0; }
        if (out.size() > c.arg(5)) { err(kInsufficient); return 0; }
        if (!out.empty()) c.write(c.arg(4), out.data(), uint32_t(out.size()));
        if (c.arg(6)) (void)write_u32_at(c, c.arg(6), 0, uint32_t(out.size()));
        return 1;
    });

    // ---------------- Toolhelp32 (1 proceso) ----------------
    K("CreateToolhelp32Snapshot", 2, [&st](GuestCpu&) { return st.add(State::Handle(State::Kind::Snap)); });
    K("Process32First", 2, [cx, handle, err](GuestCpu& c) -> uint32_t {
        auto* h = handle(c.arg(0), State::Kind::Snap);
        if (!h) { err(kBadHandle); return 0; }
        uint32_t pe = c.arg(1);
        uint32_t struct_size = 0;
        if (!read_u32_at(c, pe, 0, struct_size)) { err(kNoAccess); return 0; }
        if (struct_size < 0x128) { err(kBadLength); return 0; }
        if (!gzero_at(c, pe, 4, 0x124) || !write_u32_at(c, pe, 8, cx->config.pid) ||
            !write_u32_at(c, pe, 20, 1) || !write_u32_at(c, pe, 28, 8) ||
            !gput_at(c, pe, 36, cx->config.exe_name)) { err(kNoAccess); return 0; }
        h->cur = 1;
        return 1;
    });
    K("Process32Next", 2, [handle, err](GuestCpu& c) -> uint32_t {
        if (!handle(c.arg(0), State::Kind::Snap)) err(kBadHandle); else err(kNoMoreFiles);
        return 0;
    });

    // ---------------- módulos ----------------
    auto module = [cx, err](GuestCpu& c) -> uint32_t {
        if (!c.arg(0)) return cx->config.exe_base;
        int i = dll_index(gstr(c, c.arg(0), 260), cx->config.known_dlls);
        if (i < 0) { err(kModNotFound); return 0; }
        return kDllBase + uint32_t(i) * kDllStep;
    };
    K("GetModuleHandleA", 1, module);
    K("LoadLibraryA", 1, module);
    K("GetProcAddress", 2, [cx, err](GuestCpu& c) -> uint32_t {
        int i = dll_of_handle(c.arg(0), cx->config.known_dlls.size());
        uint32_t name = c.arg(1);
        uint32_t trap = 0;
        if (i >= 0 && name >= 0x10000u && cx->services.trap_for) trap = cx->services.trap_for(cx->config.known_dlls[size_t(i)], gstr(c, name, 256));
        if (!trap) err(kProcNotFound);
        return trap;
    });
    K("GetModuleFileNameA", 3, [cx, err](GuestCpu& c) -> uint32_t {
        std::string p = cx->config.exe_path;
        int i = dll_of_handle(c.arg(0), cx->config.known_dlls.size());
        if (i >= 0) p = cx->config.system_directory + "\\" + cx->config.known_dlls[size_t(i)];
        uint32_t n = c.arg(2);
        if (!n) { err(kInsufficient); return 0; }
        if (p.size() < n) { gput(c, c.arg(1), p); return uint32_t(p.size()); }
        gput(c, c.arg(1), p.substr(0, n - 1));
        err(kInsufficient);
        return n;
    });
    K("GetCommandLineA", 0, [cx, &st](GuestCpu& c) -> uint32_t {
        if (!st.cmdline && cx->services.alloc_guest) {
            std::string s = "\"" + cx->config.exe_path + "\"";
            st.cmdline = cx->services.alloc_guest(uint32_t(s.size() + 1));
            if (st.cmdline) gput(c, st.cmdline, s);
        }
        return st.cmdline;
    });
    K("GetVersion", 0, [cx](GuestCpu&) { return cx->config.version; });
    K("GetCurrentProcess", 0, [](GuestCpu&) { return 0xFFFFFFFFu; });
    K("OutputDebugStringA", 1, [log](GuestCpu& c) { log("OutputDebugString: " + gstr(c, c.arg(0))); return 0u; });
    K("SetUnhandledExceptionFilter", 1, [&st](GuestCpu& c) { uint32_t o = st.uef; st.uef = c.arg(0); return o; });
    K("ExitProcess", 1, [cx](GuestCpu& c) { if (cx->services.exit) cx->services.exit(c.arg(0)); return 0u; });
    K("TerminateProcess", 2, [cx](GuestCpu& c) {
        if (c.arg(0) == 0xFFFFFFFFu && cx->services.exit) cx->services.exit(c.arg(1));
        return 1u;
    });

    // ---------------- reloj ----------------
    K("QueryPerformanceFrequency", 1, [](GuestCpu& c) { put64(c, c.arg(0), GuestClock::kFreq); return 1u; });
    K("QueryPerformanceCounter", 1, [cx](GuestCpu& c) {
        put64(c, c.arg(0), cx->clock.qpc());
        std::string line;  // HW-056: spin de QPC, una línea cada 10 s
        GuestClock::Stats stats;
        if (cx->clock.take_stats(stats) && cx->services.log) {
            const uint64_t dt_ms = stats.window_us / 1000;
            line = "qpc: window_ms=" + std::to_string(dt_ms) + " qpc_calls=" + std::to_string(stats.qpc_calls) +
                   " idles=" + std::to_string(stats.idles) + " calls_per_s=" + std::to_string(stats.calls_per_second);
            cx->services.log(line);
        }
        return 1u; });
    R("winmm.dll", "timeGetTime", 0, [cx](GuestCpu&) { return cx->clock.ms(); });
    K("GetSystemTime", 1, [cx](GuestCpu& c) { systemtime(c, c.arg(0), cx->services.wall_ms ? cx->services.wall_ms() : 0); return 0u; });
    K("GetLocalTime", 1, [cx](GuestCpu& c) { systemtime(c, c.arg(0), cx->services.wall_ms ? cx->services.wall_ms() : 0); return 0u; });
    K("GetTimeZoneInformation", 1, [](GuestCpu& c) { gzero(c, c.arg(0), 172); return 0u; });

    // ---------------- archivos ----------------
    K("GetStdHandle", 1, [](GuestCpu& c) -> uint32_t {
        switch (c.arg(0)) { case 0xFFFFFFF6u: return kStdIn; case 0xFFFFFFF5u: return kStdOut;
                            case 0xFFFFFFF4u: return kStdErr; default: return kInvalid; }
    });
    K("GetFileType", 1, [&st](GuestCpu& c) -> uint32_t {
        uint32_t h = c.arg(0);
        if (h >= kStdIn && h <= kStdErr) return 2;          // FILE_TYPE_CHAR
        auto it = st.handles.find(h);
        if (it != st.handles.end() && (it->second.kind == State::Kind::File ||
                                       it->second.kind == State::Kind::CdDevice)) return 1;  // DISK
        return 0;
    });
    K("CreateFileA", 7, [cx, &st, err](GuestCpu& c) -> uint32_t {
        std::string name = gstr(c, c.arg(0), 260);
        uint32_t access = c.arg(1), disp = c.arg(4);
        if (name.size() == 6 && name.compare(0, 4, "\\\\.\\") == 0 && name[5] == ':') {
            if (std::toupper((unsigned char)name[4]) != std::toupper((unsigned char)cx->cd.letter)) {
                err(kFileNotFound); return kInvalid;
            }
            return st.add(State::Handle(State::Kind::CdDevice));
        }
        std::string existing = cx->vfs.resolve_read(name);
        bool write = (access & 0x40000000u) != 0;
        const char* mode = nullptr;
        std::string path;
        switch (disp) {
        case 1: if (!existing.empty()) { err(kFileExists); return kInvalid; } mode = "wb+"; break;
        case 2: mode = "wb+"; break;
        case 3: if (existing.empty()) { err(kFileNotFound); return kInvalid; } mode = write ? "rb+" : "rb"; break;
        case 4: mode = existing.empty() ? "wb+" : (write ? "rb+" : "rb"); break;
        case 5: if (existing.empty()) { err(kFileNotFound); return kInvalid; } mode = "wb+"; break;
        default: err(87); return kInvalid;
        }
        if (mode[0] == 'r' && !write) path = existing;
        else {
            path = cx->vfs.resolve_write(name);
            if (path.empty()) { err(kPathNotFound); return kInvalid; }
            std::error_code ec;
            if (mode[0] == 'r' && existing != path)  // copy-on-write desde la base
                fs::copy_file(existing, path, fs::copy_options::overwrite_existing, ec);
        }
        auto log_settings_read = [&](const char* source) {
            if (!cx->services.trace_settings || !cx->services.log || mode[0] != 'r' || write) return;
            const std::string lower_name = lower(name);
            constexpr char suffix[] = "settings.dat";
            if (lower_name.size() >= sizeof(suffix) - 1 &&
                lower_name.compare(lower_name.size() - (sizeof(suffix) - 1), sizeof(suffix) - 1, suffix) == 0)
                cx->services.log("[hw-image] settings read guest=" + name + " resolved=" + path +
                                 " source=" + source + " ok=1");
        };
        if (mode[0] == 'r' && mode[1] == 'b' && !mode[2]) {  // lectura cacheada por el consumidor
            if (cx->services.read_blob) if (auto blob = cx->services.read_blob(path)) {
                log_settings_read("blob");
                State::Handle mh(State::Kind::File, nullptr);
                mh.rs = std::make_unique<io::ReadStream>(std::move(blob));
                return st.add(std::move(mh));
            }
        } else {
            if (cx->services.invalidate_blob) cx->services.invalidate_blob(path);  // copia RAM del consumidor
        }
        FILE* f = std::fopen(path.c_str(), mode);
        if (!f) { err(mode[0] == 'r' ? kFileNotFound : kAccessDenied); return kInvalid; }
        log_settings_read("file");
        State::Handle fh(State::Kind::File, f);
        fh.path = path;
        if (mode[0] == 'r' && mode[1] == 'b' && !mode[2])  // sólo lectura: readahead + seek aritmético
            fh.rs = std::make_unique<io::ReadStream>(f);
        return st.add(std::move(fh));
    });
    K("ReadFile", 5, [handle, err](GuestCpu& c) -> uint32_t {
        auto* h = handle(c.arg(0), State::Kind::File);
        if (!h) { err(kBadHandle); return 0; }
        std::vector<uint8_t> b(c.arg(2));
        size_t n = b.empty() ? 0 : h->rs ? h->rs->read(b.data(), b.size())
                                         : std::fread(b.data(), 1, b.size(), h->f);
        if (n) c.write(c.arg(1), b.data(), uint32_t(n));
        if (c.arg(3)) (void)write_u32_at(c, c.arg(3), 0, uint32_t(n));
        return 1;
    });
    K("WriteFile", 5, [cx, handle, err](GuestCpu& c) -> uint32_t {
        auto* h = handle(c.arg(0), State::Kind::File);
        if (!h || !h->f) { err(h ? kAccessDenied : kBadHandle); return 0; }
        if (cx->services.invalidate_blob) cx->services.invalidate_blob(h->path);  // copia RAM del consumidor
        std::vector<uint8_t> b(c.arg(2));
        if (!b.empty()) c.read(c.arg(1), b.data(), uint32_t(b.size()));
        size_t n = b.empty() ? 0 : std::fwrite(b.data(), 1, b.size(), h->f);
        if (c.arg(3)) (void)write_u32_at(c, c.arg(3), 0, uint32_t(n));
        return n == b.size() ? 1u : 0u;
    });
    K("SetFilePointer", 4, [handle, err](GuestCpu& c) -> uint32_t {
        auto* h = handle(c.arg(0), State::Kind::File);
        if (!h) { err(kBadHandle); return kInvalid; }
        int64_t dist = int32_t(c.arg(1));
        if (c.arg(2)) {
            uint32_t high = 0;
            (void)read_u32_at(c, c.arg(2), 0, high);
            dist = int64_t((uint64_t(high) << 32) | c.arg(1));
        }
        int whence = c.arg(3) == 0 ? SEEK_SET : c.arg(3) == 1 ? SEEK_CUR : SEEK_END;
        long pos;
        if (h->rs) {
            int64_t np = h->rs->seek(dist, whence);
            if (np < 0) { err(kNegativeSeek); return kInvalid; }
            pos = long(np);
        } else {
            std::fflush(h->f);
            if (std::fseek(h->f, long(dist), whence) != 0) { err(kNegativeSeek); return kInvalid; }
            pos = std::ftell(h->f);
        }
        if (c.arg(2)) (void)write_u32_at(c, c.arg(2), 0, 0);
        return uint32_t(pos);
    });
    K("FlushFileBuffers", 1, [handle, err](GuestCpu& c) -> uint32_t {
        auto* h = handle(c.arg(0), State::Kind::File);
        if (!h) { err(kBadHandle); return 0; }
        if (h->f) std::fflush(h->f);
        return 1;
    });
    K("CloseHandle", 1, [&st, log](GuestCpu& c) -> uint32_t {
        auto it = st.handles.find(c.arg(0));
        if (it == st.handles.end()) { log("CloseHandle: handle ajeno, se ignora"); return 1; }
        it->second.rs.reset(); // ReadStream borrows f; destroy it before closing the file.
        if (it->second.f) std::fclose(it->second.f);
        st.handles.erase(it);
        return 1;
    });
    K("FindFirstFileA", 2, [cx, &st, err, fill_find](GuestCpu& c) -> uint32_t {
        auto found = cx->vfs.find(gstr(c, c.arg(0), 260));
        if (found.empty()) { err(kFileNotFound); return kInvalid; }
        if (!fill_find(c, c.arg(1), found[0])) { err(kNoAccess); return kInvalid; }
        State::Handle h(State::Kind::Find); h.found = std::move(found); h.cur = 1;
        return st.add(std::move(h));
    });
    K("FindNextFileA", 2, [handle, err, fill_find](GuestCpu& c) -> uint32_t {
        auto* h = handle(c.arg(0), State::Kind::Find);
        if (!h) { err(kBadHandle); return 0; }
        if (h->cur >= h->found.size()) { err(kNoMoreFiles); return 0; }
        if (!fill_find(c, c.arg(1), h->found[h->cur])) { err(kNoAccess); return 0; }
        ++h->cur;
        return 1;
    });
    K("FindClose", 1, [&st, handle, err](GuestCpu& c) -> uint32_t {
        if (!handle(c.arg(0), State::Kind::Find)) { err(kBadHandle); return 0; }
        st.handles.erase(c.arg(0));
        return 1;
    });
    K("DeleteFileA", 1, [cx, err](GuestCpu& c) -> uint32_t {
        auto n = gstr(c, c.arg(0), 260);
        if (cx->vfs.remove(n)) return 1;
        err(cx->vfs.resolve_read(n).empty() ? kFileNotFound : kAccessDenied);
        return 0;
    });
    K("MoveFileA", 2, [cx, err](GuestCpu& c) -> uint32_t {
        if (cx->vfs.rename(gstr(c, c.arg(0), 260), gstr(c, c.arg(1), 260))) return 1;
        err(kFileNotFound);
        return 0;
    });

    // ---------------- USER32 ----------------
    U("MessageBoxA", 4, [log](GuestCpu& c) {
        log("MessageBox [" + gstr(c, c.arg(2)) + "]: " + gstr(c, c.arg(1)));
        return 1u;  // IDOK
    });
    U("RegisterClassA", 1, [&st, err](GuestCpu& c) -> uint32_t {
        uint32_t wc = c.arg(0), cls = 0, wndproc = 0;
        if (!read_u32_at(c, wc, 36, cls) || !read_u32_at(c, wc, 4, wndproc)) { err(kNoAccess); return 0; }
        std::string name = cls < 0x10000u ? "#" + std::to_string(cls) : lower(gstr(c, cls, 256));
        if (st.classes.count(name)) { err(kClassExists); return 0; }
        st.classes[name] = wndproc;
        return st.next_atom++;
    });
    U("CreateWindowExA", 12, [&st, err](GuestCpu& c) -> uint32_t {
        uint32_t cls = c.arg(1);
        std::string name = cls < 0x10000u ? "#" + std::to_string(cls) : lower(gstr(c, cls, 256));
        auto it = st.classes.find(name);
        if (it == st.classes.end()) { err(kNoClass); return 0; }
        uint32_t h = st.next_hwnd; st.next_hwnd += 4;
        st.windows[h] = {it->second, false};
        return h;
    });
    U("ShowWindow", 2, [cx, &st](GuestCpu& c) -> uint32_t {
        auto it = st.windows.find(c.arg(0));
        if (it == st.windows.end()) return 0;
        bool was = it->second.visible;
        if (c.arg(1) != 0 && !was)
            cx->queue.post({c.arg(0), 0x0006u, 1, 0, cx->clock.ms()});  // WM_ACTIVATE(WA_ACTIVE)
        it->second.visible = c.arg(1) != 0;
        return was ? 1u : 0u;
    });
    U("PeekMessageA", 5, [cx](GuestCpu& c) -> uint32_t {
        Msg m;
        if (!cx->queue.peek(m, (c.arg(4) & 1) != 0)) {
            if (cx->services.empty_peek) cx->services.empty_peek();
            return 0;
        }
        uint32_t p = c.arg(0);
        uint32_t raw[7] = {m.hwnd, m.message, m.wparam, m.lparam, m.time, 0, 0};
        c.write(p, raw, sizeof raw);
        return 1;
    });
    U("DispatchMessageA", 1, [cx, &st, err](GuestCpu& c) -> uint32_t {
        uint32_t p = c.arg(0);
        uint32_t hwnd = 0, msg = 0, wp = 0, lp = 0;
        if (!read_u32_at(c, p, 0, hwnd) || !read_u32_at(c, p, 4, msg) ||
            !read_u32_at(c, p, 8, wp) || !read_u32_at(c, p, 12, lp)) { err(kNoAccess); return 0; }
        auto it = st.windows.find(hwnd);
        if (it == st.windows.end() || !it->second.wndproc || !cx->services.call_guest) return 0;
        return cx->services.call_guest(it->second.wndproc, {hwnd, msg, wp, lp});
    });
    U("WaitMessage", 0, [cx](GuestCpu&) {
        if (cx->queue.empty() && cx->services.wait) cx->services.wait();
        return 1u;
    });
    U("PostQuitMessage", 1, [cx](GuestCpu& c) {
        cx->queue.post({0, 0x0012u, c.arg(0), 0, cx->clock.ms()});  // WM_QUIT
        return 0u;
    });
}

} // namespace winx86::dx::shims
