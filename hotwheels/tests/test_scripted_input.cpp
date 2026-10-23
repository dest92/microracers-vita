#include "integration/scripted_input.h"
#include <cassert>
#include <sstream>
#include <string>
using namespace hw::integration;
namespace {
struct FakeInput final : hw::dinput::InputSource {
    hw::dinput::InputSnapshot state;
    hw::dinput::InputSnapshot poll() override { return state; }
};
}
int main() {
    uint32_t frame=0;
    ScriptedInput input([&]{return frame;}); std::string error;
    std::istringstream valid("# menú\n10 0x1c 3\n12 200 2 # combo\n");
    assert(input.load(valid,error));
    assert(input.poll().keys[0x1c]==0);
    frame=10;
    for(int i=0;i<20;++i) assert(input.poll().keys[0x1c]==0x80);
    frame=12;assert(input.poll().keys[0x1c]==0x80);assert(input.poll().keys[200]==0x80);
    frame=13;assert(input.poll().keys[0x1c]==0);assert(input.poll().keys[200]==0x80);
    frame=14;assert(input.poll().keys[200]==0);
    for(const char* line:{"1 256 1", "1 28 0", "-1 28 1", "4294967295 28 2", "1 0xGG 2", "1 28 2 extra", "# vacío"}) {
        std::istringstream invalid(line);assert(!input.load(invalid,error));assert(!error.empty());
    }
    frame=12;assert(input.poll().keys[200]==0); // no conservar eventos tras error

    FakeInput base; base.state.keys[0x1c]=0x80;
    RacePilotState observed{false,0};
    RacePilotInput pilot(base,[&](RacePilotState& s){s=observed;return true;},true,3);
    auto sample=pilot.poll();
    assert(sample.keys[0x1c]==0x80 && sample.keys[0xc8]==0 && sample.keys[0xcb]==0);
    observed.race_active=true; observed.laps=1;
    sample=pilot.poll(); // activa sin perder el input de menú/base
    assert(sample.keys[0xc8]==0x80 && sample.keys[0xcd]==0x80 && sample.keys[0xcb]==0);
    for(int i=0;i<47;++i) sample=pilot.poll();
    assert(sample.keys[0xcd]==0x80);
    sample=pilot.poll(); // fase neutral
    assert(sample.keys[0xc8]==0x80 && sample.keys[0xcb]==0 && sample.keys[0xcd]==0);
    for(int i=0;i<131;++i) sample=pilot.poll();
    sample=pilot.poll(); // giro opuesto
    assert(sample.keys[0xcb]==0x80 && sample.keys[0xcd]==0);
    observed.laps=4; // tres incrementos desde la observación inicial (=1)
    sample=pilot.poll();
    assert(sample.keys[0xc8]==0x80 && sample.keys[0xcb]==0 && sample.keys[0xcd]==0);
    observed.race_active=false;
    sample=pilot.poll();
    assert(sample.keys[0x1c]==0x80 && sample.keys[0xc8]==0);
    observed.race_active=true; observed.laps=2;
    sample=pilot.poll(); // otra carrera toma su propio baseline
    assert(sample.keys[0xc8]==0x80);
    observed.laps=3;
    sample=pilot.poll();
    assert(sample.keys[0xc8]==0x80);
    FakeInput untouched; untouched.state.keys[0x1c]=0x80;
    RacePilotInput disabled(untouched,[](RacePilotState&){return false;},false);
    assert(disabled.poll().keys[0x1c]==0x80);

    // Waypoint replay chooses control by guest position, not by a fixed poll phase.
    FakeInput route_base;
    RacePosition pose{true, 10 * 4096, 0}; // 10 world units right of a +Z route.
    WaypointInput waypoints(route_base, [&](RacePosition& p) { p = pose; return true; });
    std::istringstream route("0 0 0 1\n8 0 4096 1\n16 0 8192 1\n24 0 12288 1\n32 0 16384 1\n");
    assert(waypoints.load(route,error));
    sample=waypoints.poll();
    assert(sample.keys[0xc8]==0x80); // recorded accelerate
    assert(sample.keys[0xcb]==0x80 && sample.keys[0xcd]==0); // steer back toward route
    pose={true, int32_t(1u << 28), 0}; // guest corruption must not overflow int64 distance.
    sample=waypoints.poll();
    assert(sample.keys[0xcb]==0 && sample.keys[0xcd]==0);
    pose={true,0,4096};
    sample=waypoints.poll();
    assert(sample.keys[0xc8]==0x80 && sample.keys[0xcb]==0 && sample.keys[0xcd]==0);
    std::istringstream malformed_route("0 0 99\n");
    assert(!waypoints.load(malformed_route,error) && !error.empty());
    std::istringstream nonmonotonic_route("0 0 0 1\n0 0 1 1\n8 0 2 1\n");
    assert(!waypoints.load(nonmonotonic_route,error) && !error.empty());
}
