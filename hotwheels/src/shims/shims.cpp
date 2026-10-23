// Adaptador Hot Wheels entre d2rt y el registro portable de shims base.
#include "shims/shims.h"
#include "io/read_stream.h"
#include <utility>

namespace hw::shims {
namespace {
class D2rtGuestCpu final : public winx86::dx::shims::GuestCpu {
public:
    explicit D2rtGuestCpu(d2rt::Cpu& cpu) : cpu_(cpu) {}
    uint32_t arg(size_t index) const override { return cpu_.arg(int(index)); }
    bool read(uint32_t va, void* dst, uint32_t bytes) const override { return cpu_.read(va, dst, bytes); }
    bool write(uint32_t va, const void* src, uint32_t bytes) override { return cpu_.write(va, src, bytes); }
private:
    d2rt::Cpu& cpu_;
};
}

void build(Context& ctx, const Registrar& reg) {
    using namespace winx86::dx::shims;
    BaseConfig config{
        "HotWheels.exe", ctx.exe_path, "C:\\WINDOWS\\SYSTEM32", ctx.exe_base, ctx.pid,
        {"kernel32.dll", "user32.dll", "winmm.dll", "ddraw.dll", "dsound.dll",
         "dinput.dll", "gdi32.dll", "advapi32.dll"},
        {"HOTWHEELS", "NTFS", 0x0C0FFEE0u, 0x6u}, 0x0A280105u,
        0x00F10004u, 0x00010010u, 0xC001u};
    BaseServices services{
        ctx.trap_for, ctx.call_guest, ctx.alloc_guest, ctx.log, ctx.exit, ctx.wait,
        ctx.set_last_error, ctx.wall_ms,
        [](const std::string& path) { return hw::io::BlobCache::instance().get(path); },
        [](const std::string& path) { hw::io::BlobCache::instance().invalidate(path); },
        ctx.empty_peek, ctx.trace_settings};
    ctx.portable_context = std::make_shared<BaseContext>(ctx.vfs, ctx.cd, ctx.clock, ctx.queue,
                                                         std::move(config), std::move(services));
    build_base(ctx.portable_context, [&reg](ShimSpec spec) {
        d2rt::Shim shim;
        shim.argc = spec.argc;
        shim.stdcall_cleanup = spec.stdcall_cleanup;
        shim.tag = spec.dll + "!" + spec.name;
        shim.fn = [handler = std::move(spec.handler)](d2rt::Cpu& cpu) {
            D2rtGuestCpu guest(cpu);
            return handler(guest);
        };
        reg(spec.dll, spec.name, std::move(shim));
    });
}

} // namespace hw::shims
