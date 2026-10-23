#include <winx86-dx/winmm/mmio.h>
#include <winx86-dx/winmm/detail/mmio_bounds.h>
#include <winx86-dx/winmm/mci.h>

#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

using namespace winx86::dx::winmm;

static_assert(!std::is_copy_constructible_v<Mmio>);
static_assert(!std::is_move_constructible_v<Mmio>);
static_assert(!std::is_copy_constructible_v<Cdaudio>);
static_assert(!std::is_move_constructible_v<Cdaudio>);

class NoFiles final : public FileSource {
public:
    bool read(const std::string&, std::vector<uint8_t>&) override { return false; }
};

int main() {
    uint32_t from = 0, to = 0;
    assert(!detail::checked_parent_range(UINT32_MAX - 3, 12, 12, true, from, to));
    assert(detail::checked_parent_range(0, 12, 12, true, from, to));
    assert(from == 4 && to == 12);

    NoFiles source;
    Mmio mmio(source);
    assert(mmio.open("DATA.WAV", nullptr, MMIO_READ) == 0);
}
