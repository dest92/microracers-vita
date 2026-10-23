#include "scripted_input.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef __vita__
#include <psp2/kernel/clib.h>
#endif
namespace hw::integration {
namespace {
void pilot_log(const char* fmt, ...) {
    char line[128];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
#ifdef __vita__
    sceClibPrintf("%s", line);
#else
    std::fputs(line, stderr);
#endif
}
uint64_t number(const std::string& s, int base) {
    if(s.empty() || s[0]=='-') throw std::invalid_argument("número inválido");
    size_t used=0;uint64_t n=std::stoull(s,&used,base);
    if(used!=s.size()) throw std::invalid_argument("número inválido");
    return n;
}
}
bool ScriptedInput::load(std::istream& input, std::string& error) {
    events_.clear();error.clear();
    std::vector<Event> parsed;
    std::string line;uint32_t lineno=0;
    while(std::getline(input,line)) {
        ++lineno;
        auto comment=line.find('#');if(comment!=std::string::npos)line.erase(comment);
        std::istringstream row(line);std::string frame,key,duration,extra;
        if(!(row>>frame))continue;
        try {
            if(!(row>>key>>duration)||(row>>extra))throw std::invalid_argument("se esperan frame DIK duración");
            const auto f=number(frame,10),k=number(key, key.size()>2 && key[0]=='0' && (key[1]=='x'||key[1]=='X') ? 16 : 10),d=number(duration,10);
            const auto max=std::numeric_limits<uint32_t>::max();
            if(f>max||k>255||!d||d>max||f+d>max)throw std::invalid_argument("fuera de rango");
            parsed.push_back({uint32_t(f),uint32_t(f+d),uint8_t(k)});
        } catch(const std::exception& e) {
            error="línea "+std::to_string(lineno)+": "+e.what();return false;
        }
    }
    if(input.bad()) { error="error leyendo guion";return false; }
    if(parsed.empty()) { error="guion sin eventos";return false; }
    events_=std::move(parsed);return true;
}
hw::dinput::InputSnapshot ScriptedInput::poll() {
    hw::dinput::InputSnapshot state;
    const uint32_t frame=frame_();
    for(const auto& e:events_)if(frame>=e.start&&frame<e.end)state.keys[e.key]=0x80;
    return state;
}

RacePilotInput::RacePilotInput(hw::dinput::InputSource& base, Reader read_state,
                               bool enabled, uint16_t laps_to_finish)
    : base_(base), read_state_(std::move(read_state)), enabled_(enabled),
      laps_to_finish_(laps_to_finish ? laps_to_finish : 3) {}

hw::dinput::InputSnapshot RacePilotInput::poll() {
    auto state = base_.poll();
    if (!enabled_ || !read_state_) return state;

    RacePilotState observed{};
    if (!read_state_(observed)) return state;
    if (!observed.race_active) {
        baseline_valid_ = false;
        return state;
    }
    if (observed.laps < 0 || observed.laps > 100) {
        baseline_valid_ = false;
        return state;
    }

    if (!baseline_valid_) {
        baseline_valid_ = true;
        baseline_laps_ = observed.laps;
        last_laps_ = observed.laps;
        pilot_log("[race-pilot] activo, vueltas iniciales=%d objetivo=+%u\n",
                  int(baseline_laps_), unsigned(laps_to_finish_));
    } else if (observed.laps != last_laps_) {
        pilot_log("[race-pilot] vueltas guest=%d delta=%d\n",
                  int(observed.laps), int(observed.laps) - int(last_laps_));
        last_laps_ = observed.laps;
    }

    state.keys[0xc8] = 0x80; // DIK_UP: mantener acelerador
    const int32_t completed = int32_t(observed.laps) - int32_t(baseline_laps_);
    state.keys[0xcb] = 0;
    state.keys[0xcd] = 0;
    if (completed < laps_to_finish_) {
        const uint32_t phase = pilot_ticks_++ % 360;
        if (phase < 48) state.keys[0xcd] = 0x80;          // DIK_RIGHT
        else if (phase >= 180 && phase < 228) state.keys[0xcb] = 0x80; // DIK_LEFT
    }
    return state;
}

WaypointInput::WaypointInput(hw::dinput::InputSource& base, Reader read_position,
                             bool record, const char* route_path)
    : base_(base), read_position_(std::move(read_position)), record_(record) {
    active_ = record_;
    if (route_path && *route_path) {
        std::ifstream route(route_path);
        std::string error;
        if (!route || !load(route, error)) {
            pilot_log("[waypoint] no se cargó %s: %s\n", route_path,
                      error.empty() ? "no se pudo abrir" : error.c_str());
        } else {
            active_ = true;
            pilot_log("[waypoint] cargados=%u path=%s\n",
                      unsigned(route_.size()), route_path);
        }
    }
}

bool WaypointInput::load(std::istream& input, std::string& error) {
    route_.clear(); route_index_ = 0; active_ = record_; error.clear();
    std::vector<Waypoint> parsed;
    std::string line; uint32_t lineno = 0;
    while (std::getline(input, line)) {
        ++lineno;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        std::istringstream row(line);
        std::string tick, x, z, keys, extra;
        if (!(row >> tick)) continue;
        try {
            if (!(row >> x >> z >> keys) || (row >> extra))
                throw std::invalid_argument("se esperan tick x_q12 z_q12 keymask");
            const uint64_t t = number(tick, 10);
            size_t used_x = 0, used_z = 0;
            const int64_t px = std::stoll(x, &used_x, 10);
            const int64_t pz = std::stoll(z, &used_z, 10);
            const uint64_t mask = number(keys, 10);
            constexpr int64_t kWorldLimit = int64_t(1) << 27; // ±32768 unidades Q12.
            if (used_x != x.size() || used_z != z.size() || t > UINT32_MAX ||
                px < -kWorldLimit || px > kWorldLimit || pz < -kWorldLimit ||
                pz > kWorldLimit || mask > 15 ||
                (!parsed.empty() && t <= parsed.back().tick))
                throw std::invalid_argument("rango o tick no monótono");
            parsed.push_back({uint32_t(t), int32_t(px), int32_t(pz), uint8_t(mask)});
        } catch (const std::exception& e) {
            error = "línea " + std::to_string(lineno) + ": " + e.what();
            return false;
        }
    }
    if (input.bad()) { error = "error leyendo waypoints"; return false; }
    if (parsed.size() < 3) { error = "se requieren al menos 3 waypoints"; return false; }
    route_ = std::move(parsed);
    active_ = true;
    return true;
}

hw::dinput::InputSnapshot WaypointInput::poll() {
    auto state = base_.poll();
    if (!active_ || !read_position_) return state;
    RacePosition pos{};
    if (!read_position_(pos)) return state;
    if (!pos.race_active) {
        route_index_ = 0; tick_ = 0; record_last_tick_ = UINT32_MAX;
        record_last_keys_ = 0xff;
        return state;
    }
    constexpr int64_t kWorldLimitQ12 = int64_t(1) << 27;
    if (int64_t(pos.x_q12) < -kWorldLimitQ12 || int64_t(pos.x_q12) > kWorldLimitQ12 ||
        int64_t(pos.z_q12) < -kWorldLimitQ12 || int64_t(pos.z_q12) > kWorldLimitQ12)
        return state;

    uint8_t keys = 0;
    if (state.keys[0xc8] & 0x80) keys |= 1;
    if (state.keys[0xd0] & 0x80) keys |= 2;
    if (state.keys[0xcb] & 0x80) keys |= 4;
    if (state.keys[0xcd] & 0x80) keys |= 8;
    // tick_ advances once per active GetDeviceState poll. A key transition
    // emits at its own tick and supersedes the next periodic sample; thus
    // samples are strictly increasing even when a transition is off cadence.
    if (record_ && (record_last_tick_ == UINT32_MAX || tick_ - record_last_tick_ >= 8 ||
                    keys != record_last_keys_)) {
        pilot_log("[waypoint] %u %d %d %u\n", tick_, pos.x_q12, pos.z_q12,
                  unsigned(keys));
        record_last_tick_ = tick_; record_last_keys_ = keys;
    }
    ++tick_;
    if (route_.empty()) return state;

    const size_t begin = route_index_;
    const size_t end = std::min(route_.size() - 1, begin + size_t(64));
    size_t nearest = begin;
    int64_t nearest_dist = INT64_MAX;
    for (size_t i = begin; i <= end; ++i) {
        const int64_t dx = int64_t(pos.x_q12) - route_[i].x_q12;
        const int64_t dz = int64_t(pos.z_q12) - route_[i].z_q12;
        const int64_t d2 = dx * dx + dz * dz;
        if (d2 < nearest_dist) { nearest = i; nearest_dist = d2; }
    }
    route_index_ = nearest; // Monótono para no saltar a otra curva en cruces.
    const auto& sample = route_[nearest];
    state.keys[0xc8] = sample.keys & 1 ? 0x80 : 0;
    state.keys[0xd0] = sample.keys & 2 ? 0x80 : 0;
    state.keys[0xcb] = sample.keys & 4 ? 0x80 : 0;
    state.keys[0xcd] = sample.keys & 8 ? 0x80 : 0;

    const size_t look = std::min(route_.size() - 1, nearest + size_t(3));
    if (look != nearest) {
        const int64_t dx = int64_t(route_[look].x_q12) - route_[nearest].x_q12;
        const int64_t dz = int64_t(route_[look].z_q12) - route_[nearest].z_q12;
        const int64_t ex = int64_t(pos.x_q12) - route_[nearest].x_q12;
        const int64_t ez = int64_t(pos.z_q12) - route_[nearest].z_q12;
        const int64_t cross = dx * ez - dz * ex;
        const int64_t len2 = dx * dx + dz * dz;
        const double t = len2 ? std::max(0.0, std::min(1.0,
            double(ex * dx + ez * dz) / double(len2))) : 0.0;
        const double off_x = double(ex) - t * double(dx);
        const double off_z = double(ez) - t * double(dz);
        const double error = cross ? std::copysign(std::hypot(off_x, off_z), double(cross)) : 0.0;
        constexpr double kCorrectionThresholdQ12 = 3.0 * 4096.0;
        if (std::abs(error) > kCorrectionThresholdQ12) {
            // Assumed X/Z convention: for +Z, negative cross is right of trace.
            // Keep this calibration gate open until a live LEFT/RIGHT pulse
            // confirms the guest's coordinate handedness.
            state.keys[0xcb] = error < 0.0 ? 0x80 : 0;
            state.keys[0xcd] = error > 0.0 ? 0x80 : 0;
        }
    }
    return state;
}
}
