#include <winx86-dx/audio/dsound_bus.h>
#include <winx86-dx/audio/mixer.h>

#include <cassert>
#include <cstdint>
#include <type_traits>

using namespace winx86::dx::audio;

static_assert(!std::is_copy_constructible_v<DsoundBus>);
static_assert(!std::is_move_constructible_v<DsoundBus>);
static_assert(!std::is_copy_constructible_v<Mixer>);
static_assert(!std::is_move_constructible_v<Mixer>);

class Source final : public PcmSource {
public:
    int read(int16_t* out, int frames) override {
        const int count = frames < 2 ? frames : 2;
        for (int i = 0; i < count; ++i) out[2 * i] = out[2 * i + 1] = static_cast<int16_t>((i + 1) * 1000);
        return count;
    }
    int rate() const override { return 24000; }
};

int main() {
    Source source;
    Mixer mixer(24000);
    mixer.add(&source);
    int16_t mixed[4]{};
    assert(mixer.mix(mixed, 2) == 2);
    assert(mixed[0] == 1000 && mixed[1] == 1000 && mixed[2] == 2000 && mixed[3] == 2000);

    DsoundBusConfig config;
    config.capacity_frames = 2;
    config.prime_ms = 0;
    config.prime_max_ms = 120;
    DsoundBus bus(config);
    const int16_t burst[] = {10, 11, 20, 21, 30, 31};
    bus.push(burst, 3, 24000);
    int16_t drained[4]{};
    assert(bus.read(drained, 2) == 2);
    assert(drained[0] == 20 && drained[1] == 21 && drained[2] == 30 && drained[3] == 31);
    assert(bus.overruns() == 1);
}
