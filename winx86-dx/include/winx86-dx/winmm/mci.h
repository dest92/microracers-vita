// Portable MCI cdaudio API for PE32/i386 Win32 guests.
#pragma once

#include "winmm.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace winx86::dx::winmm {

using MCIDEVICEID = uint32_t;
using MCIERROR = uint32_t;

// Parámetros host de alto nivel. Los punteros son punteros host; nunca apuntan
// a structs guest ni se copian con memcpy desde una dirección guest PE32.
struct MciOpenParms {
    uint32_t dwCallback = 0;
    uint32_t wDeviceID = 0;
    const char* lpstrDeviceType = nullptr;
    const char* lpstrElementName = nullptr;
    const char* lpstrAlias = nullptr;
};
struct MciSetParms {
    uint32_t dwCallback = 0;
    uint32_t dwTimeFormat = 0;
    uint32_t dwAudio = 0;
};
struct MciPlayParms {
    uint32_t dwCallback = 0;
    uint32_t dwFrom = 0;
    uint32_t dwTo = 0;
};
struct MciStatusParms {
    uint32_t dwCallback = 0;
    uint32_t dwReturn = 0;
    uint32_t dwItem = 0;
    uint32_t dwTrack = 0;
};
struct MciGenericParms {
    uint32_t dwCallback = 0;
};

struct TrackInfo {
    std::string path;
    double seconds = 0.0;
};
struct TrackCatalog {
    std::vector<TrackInfo> tracks;
};

class Cdaudio {
public:
    Cdaudio(MusicSink& sink, TrackCatalog catalog) : sink_(sink), tracks_(std::move(catalog.tracks)) {}
    ~Cdaudio();
    Cdaudio(const Cdaudio&) = delete;
    Cdaudio& operator=(const Cdaudio&) = delete;
    Cdaudio(Cdaudio&&) = delete;
    Cdaudio& operator=(Cdaudio&&) = delete;

    int track_count() const { return static_cast<int>(tracks_.size()); }
    // Configuración inyectada por el port; no hay nombres/rutas de juego por default.
    std::string track_path(int track) const;

    // Equivalente portable de mciSendCommandA(id, msg, flags, parms).
    MCIERROR send_command(MCIDEVICEID id, uint32_t msg, uint32_t flags, void* parms);

    // Introspección para tests.
    bool has_device(MCIDEVICEID id) const { return devices_.count(id) != 0; }
    int current_track(MCIDEVICEID id) const;
    uint32_t time_format(MCIDEVICEID id) const;

    void set_track_seconds(int track, double seconds);
    double track_seconds(int track) const;
    uint32_t track_length_tmsf(int track) const;
    uint32_t track_start_tmsf(int track) const;

private:
    struct Device {
        std::string element;
        uint32_t time_format = MCI_FORMAT_MSF;
        bool format_set = false;  // MSF absoluto sólo si el llamador lo pidió (BUG-006)
        bool msf_abs() const { return format_set && time_format == MCI_FORMAT_MSF; }
        int current = 0;  // 1-based, 0 = ninguna
        int last = 0;
        bool playing = false;
        bool paused = false;
        std::string last_path;
    };

    Device* get(MCIDEVICEID id);
    const Device* get(MCIDEVICEID id) const;

    MusicSink& sink_;
    std::map<MCIDEVICEID, Device> devices_;
    MCIDEVICEID next_ = 1;
    std::vector<TrackInfo> tracks_;
};

}  // namespace winx86::dx::winmm
