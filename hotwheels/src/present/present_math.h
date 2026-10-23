#pragma once
#include <cstdint>
namespace hw::present {
struct Rect { int32_t x,y,w,h; };
Rect fit_rect(int32_t src_w,int32_t src_h,int32_t dst_w,int32_t dst_h);
// Entrada 0xAARRGGBB de HW-013, salida 0xAABBGGRR de GXM P8_ABGR.
uint32_t argb_to_abgr(uint32_t color);
// Copia índices lineales; rellena con cero el padding de cada fila destino.
bool copy_index_rows(uint8_t* dst,uint32_t dst_stride,const uint8_t* src,
                     uint32_t src_pitch,uint32_t width,uint32_t height);
}
