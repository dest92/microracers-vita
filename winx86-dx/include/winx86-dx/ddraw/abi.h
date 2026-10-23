#pragma once
// i386 guest ABI metadata: explicit argument counts per vtable slot. These
// constants describe the wire contract; they are not host struct layouts.
#include <array>
#include <cstdint>
namespace winx86::dx::ddraw::abi {
// Cantidad de argumentos explícitos, sin this, según ddraw.h/d3d.h i386.
inline constexpr std::array<uint8_t,23> ddraw1={
    2,0,0,0,3,4,3,2,4,4,0,2,1,2,1,1,1,1,1,0,2,3,2};
inline constexpr std::array<uint8_t,28> ddraw4={
    2,0,0,0,3,4,3,2,4,4,0,2,1,2,1,1,1,1,1,0,2,5,2,3,2,0,0,2};
inline constexpr std::array<uint8_t,45> surface4={
    2,0,0,1,1,5,3,5,2,2,3,2,2,1,1,1,2,1,1,2,1,1,1,2,0,4,1,0,1,2,2,1,1,5,1,2,1,1,1,2,4,3,1,1,0};
inline constexpr std::array<uint8_t,7> palette={2,0,0,1,4,3,4};
inline constexpr std::array<uint8_t,12> d3d3={2,0,0,2,2,2,2,2,4,4,3,0};
}
