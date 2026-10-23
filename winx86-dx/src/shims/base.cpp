#include "../../include/winx86-dx/shims/base.h"

#include <algorithm>
#include <utility>

namespace winx86::dx::shims {

CdDrive::CdDrive(CdDriveConfig config)
    : letter(config.letter), label(std::move(config.label)), filesystem(std::move(config.filesystem)),
      serial(config.serial), data_track_frames(config.data_track_frames),
      audio_track_frames(std::move(config.audio_track_frames)) {}

std::vector<uint8_t> CdDrive::read_toc() const {
    const size_t track_count = 1 + audio_track_frames.size();
    std::vector<uint8_t> toc(4 + 8 * (track_count + 1), 0);
    const uint16_t length = uint16_t(2 + 8 * (track_count + 1));
    toc[0] = uint8_t(length >> 8);
    toc[1] = uint8_t(length);
    toc[2] = 1;
    toc[3] = uint8_t(track_count);
    uint32_t lba = 150; // standard two-second pregap
    for (size_t i = 0; i <= track_count; ++i) {
        uint8_t* entry = &toc[4 + 8 * i];
        const bool leadout = i == track_count;
        entry[1] = uint8_t((1 << 4) | (i == 0 ? 4 : 0)); // Adr=1 | Control (4=data)
        entry[2] = leadout ? 0xAA : uint8_t(i + 1);
        entry[5] = uint8_t(lba / 75 / 60);
        entry[6] = uint8_t(lba / 75 % 60);
        entry[7] = uint8_t(lba % 75);
        if (!leadout) lba += i == 0 ? data_track_frames : audio_track_frames[i - 1];
    }
    return toc;
}

uint32_t CdDrive::ioctl(uint32_t code, std::vector<uint8_t>& out) const {
    out.clear();
    if (code == 0x24000) { out = read_toc(); return 0; } // IOCTL_CDROM_READ_TOC
    if (code == 0x24008) return 0;                       // IOCTL_CDROM_STOP_AUDIO
    return 1;                                            // ERROR_INVALID_FUNCTION
}

void GuestClock::set_source(ClockSource source) {
    source_ = std::move(source);
    origin_set_ = false;
    stats_set_ = false;
}

uint64_t GuestClock::now() {
    const uint64_t value = source_ ? source_() : 0;
    if (!origin_set_) { origin_ = value; origin_set_ = true; }
    return value < origin_ ? 0 : value - origin_;
}

uint64_t GuestClock::qpc() {
    uint64_t raw = now();
    ++qpc_calls_;
    if (spin_idle_us && have_last_qpc_ && raw - last_qpc_raw_ < spin_idle_us && idle &&
        spin_has_room(raw)) {
        idle();
        ++idles_;
        raw = now();
    }
    last_qpc_raw_ = raw;
    have_last_qpc_ = true;
    if (raw + 1 > last_) last_ = raw + 1;
    return last_;
}

// SPEC-059: dormir sólo si todavía falta más que el tramo final para el deadline
// del giro; el tramo final lo hace el invitado con lecturas finas, de modo que el
// QPC de salida —y el dt derivado— no cambian.
bool GuestClock::spin_has_room(uint64_t raw) const {
    if (!spin_deadline) return true;      // legado: dormir dentro de todo el giro
    const uint64_t deadline = spin_deadline();
    // Diff con signo sobre 32 bits: el giro dura ≤ 25 ms, así que no hay
    // ambigüedad de módulo 2^32 y el cruce del wrap del contador de µs es seguro.
    return int32_t(uint32_t(deadline) - uint32_t(raw)) > int32_t(idle_margin_us);
}

uint32_t GuestClock::ms() { return uint32_t(now() / 1000); }

bool GuestClock::take_stats(Stats& out) {
    const uint64_t time = now();
    if (!stats_set_) {
        stats_set_ = true;
        stats_start_ = time;
        stats_calls_ = qpc_calls_;
        stats_idles_ = idles_;
        return false;
    }
    if (time - stats_start_ < kStatsWindowUs) return false;
    const uint64_t elapsed_ms = (time - stats_start_) / 1000;
    out.window_us = time - stats_start_;
    out.qpc_calls = qpc_calls_ - stats_calls_;
    out.idles = idles_ - stats_idles_;
    out.calls_per_second = out.qpc_calls * 1000 / (elapsed_ms ? elapsed_ms : 1);
    stats_start_ = time;
    stats_calls_ = qpc_calls_;
    stats_idles_ = idles_;
    return true;
}

bool MsgQueue::peek(Msg& out, bool remove) {
    if (queue_.empty()) return false;
    out = queue_.front();
    if (remove) queue_.erase(queue_.begin());
    return true;
}

} // namespace winx86::dx::shims
