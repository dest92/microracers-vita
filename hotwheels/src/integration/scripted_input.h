#pragma once
#include "dinput/dinput.h"
#include <functional>
#include <istream>
#include <string>
#include <vector>
namespace hw::integration {
class ScriptedInput final : public hw::dinput::InputSource {
public:
    explicit ScriptedInput(std::function<uint32_t()> frame) : frame_(std::move(frame)) {}
    bool load(std::istream& input, std::string& error);
    hw::dinput::InputSnapshot poll() override;
private:
    struct Event { uint32_t start, end; uint8_t key; };
    std::function<uint32_t()> frame_;
    std::vector<Event> events_;
};

struct RacePilotState {
    bool race_active = false;
    int16_t laps = 0;
};

struct RacePosition {
    bool race_active = false;
    int32_t x_q12 = 0, z_q12 = 0;
};

// Captura de teclas y trazo Q12; playback selecciona por proximidad espacial
// y corrige el error transversal, sin depender de un contador de vuelta.
class WaypointInput final : public hw::dinput::InputSource {
public:
    using Reader = std::function<bool(RacePosition&)>;
    WaypointInput(hw::dinput::InputSource& base, Reader read_position,
                  bool record = false, const char* route_path = nullptr);
    bool load(std::istream& input, std::string& error);
    hw::dinput::InputSnapshot poll() override;
    size_t waypoint_count() const { return route_.size(); }
private:
    struct Waypoint { uint32_t tick; int32_t x_q12, z_q12; uint8_t keys; };
    hw::dinput::InputSource& base_;
    Reader read_position_;
    bool record_;
    bool active_ = false;
    std::vector<Waypoint> route_;
    uint32_t tick_ = 0, record_last_tick_ = UINT32_MAX, route_index_ = 0;
    uint8_t record_last_keys_ = 0xff;
};

// Opt-in overlay for the real guest input. It stays on the GetDeviceState
// caller thread and never writes guest memory.
class RacePilotInput final : public hw::dinput::InputSource {
public:
    using Reader = std::function<bool(RacePilotState&)>;
    RacePilotInput(hw::dinput::InputSource& base, Reader read_state,
                   bool enabled, uint16_t laps_to_finish = 3);
    hw::dinput::InputSnapshot poll() override;
private:
    hw::dinput::InputSource& base_;
    Reader read_state_;
    bool enabled_;
    uint16_t laps_to_finish_;
    bool baseline_valid_ = false;
    int16_t baseline_laps_ = 0;
    int16_t last_laps_ = 0;
    uint32_t pilot_ticks_ = 0;
};
}
