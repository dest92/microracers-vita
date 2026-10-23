#include <winx86-dx/io/read_stream.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <limits>
#include <type_traits>
#include <vector>

using namespace winx86::dx::io;

static_assert(!std::is_copy_constructible_v<ReadStream>);
static_assert(!std::is_move_constructible_v<ReadStream>);

int main() {
    auto bytes = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{2, 4, 6, 8});
    ReadStream stream(bytes);
    uint8_t out[2]{};
    assert(stream.read(out, sizeof(out)) == sizeof(out));
    assert(out[0] == 2 && out[1] == 4);
    assert(stream.seek(-1, SEEK_END) == 3);
    assert(stream.read(out, 1) == 1 && out[0] == 8);
    assert(stream.seek(std::numeric_limits<int64_t>::max(), SEEK_SET) == std::numeric_limits<int64_t>::max());
    const auto before_overflow = stream.tell();
    assert(stream.seek(1, SEEK_CUR) == -1);
    assert(stream.tell() == before_overflow);
}
