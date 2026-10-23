#include "integration/mmio_transfer.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

int main() {
    using hw::integration::detail::kMmioTransferChunk;
    using hw::integration::detail::mmio_read_bounded;

    const std::vector<uint8_t> source{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint8_t> output;
    size_t cursor = 0, max_read = 0, max_write = 0;
    const int32_t got = mmio_read_bounded(std::numeric_limits<int32_t>::max(),
        [&](uint8_t* dst, int32_t requested) {
            max_read = std::max(max_read, size_t(requested));
            const size_t n = std::min(source.size() - cursor, size_t(requested));
            std::copy_n(source.data() + cursor, n, dst);
            cursor += n;
            return int32_t(n);
        },
        [&](const uint8_t* bytes, uint32_t count, uint32_t) {
            max_write = std::max(max_write, size_t(count));
            output.insert(output.end(), bytes, bytes + count);
            return true;
        });

    assert(got == int32_t(source.size()));
    assert(max_read <= kMmioTransferChunk && max_write <= kMmioTransferChunk);
    assert(output == source);
    assert(cursor == source.size());

    int calls = 0;
    const int32_t partial = mmio_read_bounded(9000,
        [&](uint8_t* dst, int32_t requested) {
            ++calls;
            std::fill_n(dst, requested, uint8_t(0x5a));
            return calls == 1 ? requested : 0;
        },
        [](const uint8_t*, uint32_t, uint32_t) { return true; });
    assert(partial == int32_t(kMmioTransferChunk));
    assert(calls == 2);
    return 0;
}
