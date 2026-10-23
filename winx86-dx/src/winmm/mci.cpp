// Portable MCI cdaudio implementation.
#include "../../include/winx86-dx/winmm/mci.h"

#include <algorithm>
#include <cctype>

namespace winx86::dx::winmm {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

uint32_t seconds_to_tmsf(int track, double seconds) {
    if (seconds < 0) seconds = 0;
    const uint32_t frames_total = static_cast<uint32_t>(seconds * 75.0 + 0.5);
    const int f = static_cast<int>(frames_total % 75);
    const int s = static_cast<int>((frames_total / 75) % 60);
    const int m = static_cast<int>(frames_total / 75 / 60);
    return MCI_MAKE_TMSF(track, m, s, f);
}

// MSF absoluto (MCI_FORMAT_MSF): m | s<<8 | f<<16, 75 frames/s, con el pregap
// de 150 frames de un CD real antes de la pista 1 (BUG-006).
constexpr uint32_t kPregap = 150;
uint32_t sec_frames(double s) { return s < 0 ? 0u : static_cast<uint32_t>(s * 75.0 + 0.5); }
uint32_t frames_to_msf(uint32_t fr) { return (fr / 4500) | (((fr / 75) % 60) << 8) | ((fr % 75) << 16); }
uint32_t msf_to_frames(uint32_t v) { return (v & 0xff) * 4500 + ((v >> 8) & 0xff) * 75 + ((v >> 16) & 0xff); }
uint32_t frames_to_tmsf(int t, uint32_t fr) {
    return MCI_MAKE_TMSF(t, static_cast<int>(fr / 4500), static_cast<int>((fr / 75) % 60), static_cast<int>(fr % 75));
}

}  // namespace

std::string Cdaudio::track_path(int track) const {
    return track < 1 || track > track_count() ? std::string{} : tracks_[size_t(track - 1)].path;
}

Cdaudio::Device* Cdaudio::get(MCIDEVICEID id) {
    auto it = devices_.find(id);
    return it == devices_.end() ? nullptr : &it->second;
}

const Cdaudio::Device* Cdaudio::get(MCIDEVICEID id) const {
    auto it = devices_.find(id);
    return it == devices_.end() ? nullptr : &it->second;
}

MCIERROR Cdaudio::send_command(MCIDEVICEID id, uint32_t msg, uint32_t flags, void* parms) {
    switch (msg) {
    case MCI_OPEN: {
        auto* p = static_cast<MciOpenParms*>(parms);
        if (!p || !p->lpstrDeviceType) return MCIERR_DEVICE_TYPE_REQUIRED;
        if (lower(p->lpstrDeviceType) != "cdaudio") return MCIERR_UNSUPPORTED_FUNCTION;
        const MCIDEVICEID dev = next_++;
        Device d;
        d.element = p->lpstrElementName ? p->lpstrElementName : "";
        devices_[dev] = d;
        p->wDeviceID = dev;
        return 0;
    }
    case MCI_CLOSE: {
        if (!get(id)) return MCIERR_INVALID_DEVICE_ID;
        sink_.stop();
        devices_.erase(id);
        return 0;
    }
    case MCI_SET: {
        Device* d = get(id);
        if (!d) return MCIERR_INVALID_DEVICE_ID;
        if (flags & MCI_SET_TIME_FORMAT) {
            auto* p = static_cast<MciSetParms*>(parms);
            if (p) { d->time_format = p->dwTimeFormat; d->format_set = true; }
        }
        return 0;
    }
    case MCI_STATUS: {
        Device* d = get(id);
        if (!d) return MCIERR_INVALID_DEVICE_ID;
        auto* p = static_cast<MciStatusParms*>(parms);
        if (!p || !(flags & MCI_STATUS_ITEM)) return MCIERR_UNSUPPORTED_FUNCTION;
        switch (p->dwItem) {
        case MCI_STATUS_NUMBER_OF_TRACKS:
            p->dwReturn = static_cast<uint32_t>(track_count());
            return 0;
        case MCI_STATUS_CURRENT_TRACK:
            p->dwReturn = static_cast<uint32_t>(d->current);
            return 0;
        case MCI_STATUS_LENGTH: {
            const int t = p->dwTrack ? static_cast<int>(p->dwTrack) : d->current;
            if (t < 1 || t > track_count()) return MCIERR_UNSUPPORTED_FUNCTION;
            p->dwReturn = d->msf_abs() ? frames_to_msf(sec_frames(track_seconds(t)))
                                                           : track_length_tmsf(t);
            return 0;
        }
        case MCI_STATUS_POSITION: {
            const int t = p->dwTrack ? static_cast<int>(p->dwTrack) : d->current;
            if (t < 1 || t > track_count()) return MCIERR_UNSUPPORTED_FUNCTION;
            if (d->msf_abs()) {
                uint32_t fr = kPregap;
                for (int i = 1; i < t; ++i) fr += sec_frames(track_seconds(i));
                p->dwReturn = frames_to_msf(fr);
            } else {
                p->dwReturn = track_start_tmsf(t);
            }
            return 0;
        }
        case MCI_STATUS_MODE:
            p->dwReturn = d->playing ? MCI_MODE_PLAY : (d->paused ? MCI_MODE_PAUSE : MCI_MODE_STOP);
            return 0;
        case MCI_STATUS_TIME_FORMAT:
            p->dwReturn = d->time_format;
            return 0;
        case MCI_STATUS_MEDIA_PRESENT:
        case MCI_STATUS_READY:
            p->dwReturn = track_count() != 0 ? 1u : 0u;
            return 0;
        default:
            return MCIERR_UNSUPPORTED_FUNCTION;
        }
    }
    case MCI_PLAY: {
        Device* d = get(id);
        if (!d) return MCIERR_INVALID_DEVICE_ID;
        auto* p = static_cast<MciPlayParms*>(parms);
        if (track_count() == 0) return MCIERR_UNSUPPORTED_FUNCTION;
        int track = (flags & MCI_FROM && p) ? MCI_TMSF_TRACK(p->dwFrom) : (d->last ? d->last : 1);
        uint32_t from = (flags & MCI_FROM && p) ? p->dwFrom : 0;
        uint32_t to = (flags & MCI_TO && p) ? p->dwTo : 0;
        if (d->msf_abs() && (flags & MCI_FROM) && p) {
            // MSF absoluto -> pista que lo contiene + offset relativo (TMSF).
            const uint32_t abs = msf_to_frames(p->dwFrom);
            uint32_t start = kPregap;
            track = 1;
            while (track < track_count() && abs >= start + sec_frames(track_seconds(track))) start += sec_frames(track_seconds(track++));
            from = frames_to_tmsf(track, abs > start ? abs - start : 0);
            const uint32_t tabs = (flags & MCI_TO) ? msf_to_frames(p->dwTo) : 0;
            const uint32_t end = start + sec_frames(track_seconds(track));
            to = (tabs > start && tabs <= end) ? frames_to_tmsf(track, tabs - start) : 0;  // otra pista: hasta el final
        }
        track = std::clamp(track, 1, track_count());
        d->current = d->last = track;
        d->playing = true;
        d->paused = false;
        d->last_path = track_path(track);
        sink_.play(track, d->last_path, from, to);
        return 0;
    }
    case MCI_PAUSE: {
        Device* d = get(id);
        if (!d) return MCIERR_INVALID_DEVICE_ID;
        d->playing = false;
        d->paused = true;
        sink_.pause();
        return 0;
    }
    case MCI_STOP: {
        Device* d = get(id);
        if (!d) return MCIERR_INVALID_DEVICE_ID;
        d->playing = false;
        d->paused = false;
        sink_.stop();
        return 0;
    }
    default:
        return MCIERR_UNRECOGNIZED_COMMAND;
    }
}

int Cdaudio::current_track(MCIDEVICEID id) const {
    const Device* d = get(id);
    return d ? d->current : 0;
}

uint32_t Cdaudio::time_format(MCIDEVICEID id) const {
    const Device* d = get(id);
    return d ? d->time_format : 0;
}

void Cdaudio::set_track_seconds(int track, double seconds) {
    if (track >= 1 && track <= track_count()) tracks_[size_t(track - 1)].seconds = seconds;
}

double Cdaudio::track_seconds(int track) const {
    if (track < 1 || track > track_count()) return 0.0;
    return tracks_[size_t(track - 1)].seconds;
}

uint32_t Cdaudio::track_length_tmsf(int track) const {
    return seconds_to_tmsf(track, track_seconds(track));
}

uint32_t Cdaudio::track_start_tmsf(int track) const {
    double acc = 0.0;
    for (int i = 1; i < track && i <= track_count(); ++i) acc += track_seconds(i);
    return seconds_to_tmsf(track, acc);
}

Cdaudio::~Cdaudio() = default;

}  // namespace winx86::dx::winmm
