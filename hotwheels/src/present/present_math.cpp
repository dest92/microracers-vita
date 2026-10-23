#include "present_math.h"
#include <cstring>
#include <limits>
namespace hw::present {
Rect fit_rect(int32_t sw,int32_t sh,int32_t dw,int32_t dh) {
    if(sw<=0||sh<=0||dw<=0||dh<=0) return {0,0,0,0};
    int64_t w=int64_t(sw)*dh/sh,h=dh;
    if(w>dw) { w=dw;h=int64_t(sh)*dw/sw; }
    if(w<=0||h<=0) return {0,0,0,0};
    return {int32_t((dw-w)/2),int32_t((dh-h)/2),int32_t(w),int32_t(h)};
}
uint32_t argb_to_abgr(uint32_t c) {
    return (c&0xFF00FF00u)|((c&0x00FF0000u)>>16)|((c&0x000000FFu)<<16);
}
bool copy_index_rows(uint8_t* dst,uint32_t ds,const uint8_t* src,uint32_t sp,uint32_t w,uint32_t h) {
    if(!dst||!src||!w||!h||ds<w||sp<w||
       uint64_t(ds)*h>std::numeric_limits<uint32_t>::max()||
       uint64_t(sp)*h>std::numeric_limits<uint32_t>::max()) return false;
    for(uint32_t y=0;y<h;++y) {
        std::memcpy(dst+size_t(y)*ds,src+size_t(y)*sp,w);
        if(ds>w) std::memset(dst+size_t(y)*ds+w,0,ds-w);
    }
    return true;
}
}
