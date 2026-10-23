// Checked ranges for the PE32 MMIO structures passed by a guest process.
#pragma once

#include <cstdint>

namespace winx86::dx::winmm::detail {

inline bool checked_parent_range(uint32_t offset, uint32_t size, uint32_t file_size,
    bool is_form, uint32_t& from, uint32_t& to) {
    if (offset > file_size || size > file_size - offset) return false;
    if (is_form && size < 4) return false;
    const uint32_t end = offset + size;  // bounded by file_size before addition
    const uint32_t begin = is_form ? offset + 4 : offset;
    from = begin;
    to = end;
    return true;
}

}  // namespace winx86::dx::winmm::detail
