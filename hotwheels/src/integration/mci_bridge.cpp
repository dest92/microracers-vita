// src/integration/mci_bridge.cpp — ver mci_bridge.h.
#include "integration/mci_bridge.h"

#include "winmm/mci.h"

#include "runtime/bridge.h"
#include "runtime/cpu.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace hw::integration {
namespace {
using namespace hw::winmm;

std::string guest_cstr(d2rt::Cpu& c, uint32_t va) {
    std::string s;
    if (va < 0x10000) return s;   // MCI_OPEN_TYPE_ID: entero, no puntero
    for (uint8_t b; s.size() < 260 && (c.read(va + (uint32_t)s.size(), &b, 1), b) != 0;) s += char(b);
    return s;
}

std::unique_ptr<Cdaudio> g_cd;

TrackCatalog hotwheels_track_catalog() {
    return {{{"MUSIC\\Track1.wav", 312.37}, {"MUSIC\\Track2.wav", 313.37},
             {"MUSIC\\Track3.wav", 264.64}, {"MUSIC\\Track4.wav", 282.61}}};
}
}  // namespace

void install_mci(d2rt::Bridge& br, d2rt::Cpu& /*cpu*/, MusicSink& sink) {
    g_cd = std::make_unique<Cdaudio>(sink, hotwheels_track_catalog());
    d2rt::Shim s;
    s.tag = "WINMM.dll!mciSendCommandA";
    s.argc = 4;
    s.fn = [](d2rt::Cpu& c) -> uint32_t {
        const uint32_t id = c.arg(0), msg = c.arg(1), flags = c.arg(2), p = c.arg(3);
        auto rd = [&](uint32_t off) { return p ? c.read_u32(p + off) : 0u; };
        static const bool dbg = std::getenv("HW_MCI_LOG") != nullptr;
        if (dbg) std::fprintf(stderr, "[mci] id=%u msg=0x%x flags=0x%x p=[%08x %08x %08x %08x]\n",
                              id, msg, flags, rd(0), rd(4), rd(8), rd(12));
        switch (msg) {
        case MCI_OPEN: {
            MciOpenParms o;
            o.dwCallback = rd(0);
            std::string type = (flags & MCI_OPEN_TYPE_ID) ? std::string("cdaudio") : guest_cstr(c, rd(8));
            std::string elem = (flags & MCI_OPEN_ELEMENT) ? guest_cstr(c, rd(12)) : std::string();
            std::string alias = (flags & MCI_OPEN_ALIAS) ? guest_cstr(c, rd(16)) : std::string();
            if (flags & (MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID)) o.lpstrDeviceType = type.c_str();
            if (flags & MCI_OPEN_ELEMENT) o.lpstrElementName = elem.c_str();
            if (flags & MCI_OPEN_ALIAS) o.lpstrAlias = alias.c_str();
            const MCIERROR e = g_cd->send_command(id, msg, (flags & MCI_OPEN_TYPE_ID) ? ((flags & ~MCI_OPEN_TYPE_ID) | MCI_OPEN_TYPE) : flags, &o);
            if (!e && p) c.write_u32(p + 4, o.wDeviceID);
            return e;
        }
        case MCI_SET: {
            MciSetParms o{rd(0), rd(4), rd(8)};
            return g_cd->send_command(id, msg, flags, &o);
        }
        case MCI_PLAY: {
            MciPlayParms o{rd(0), rd(4), rd(8)};
            return g_cd->send_command(id, msg, flags, &o);
        }
        case MCI_STATUS: {
            MciStatusParms o{rd(0), rd(4), rd(8), rd(12)};
            const MCIERROR e = g_cd->send_command(id, msg, flags, &o);
            if (!e && p) c.write_u32(p + 4, o.dwReturn);
            return e;
        }
        default: {
            MciGenericParms o{rd(0)};
            return g_cd->send_command(id, msg, flags, &o);
        }
        }
    };
    br.register_shim("WINMM.dll", "mciSendCommandA", s);
}

}  // namespace hw::integration
