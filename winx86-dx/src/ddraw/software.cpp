#include "../../include/winx86-dx/ddraw/software.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace winx86::dx::ddraw {
Surface* Device::make_surface(uint32_t caps, uint32_t width, uint32_t height, uint32_t bpp) {
    if (!width || !height || width > 4096 || height > 4096 || (bpp!=8 && bpp!=16 && bpp!=32)) return nullptr;
    auto s=std::make_unique<Surface>();
    s->caps=caps; s->width=width; s->height=height; s->bpp=bpp; s->pitch=width*(bpp/8);
    s->va=mem_.alloc(size_t(s->pitch)*height);
    if (!s->va || !mem_.ptr(s->va,size_t(s->pitch)*height)) return nullptr;
    std::memset(mem_.ptr(s->va,size_t(s->pitch)*height),0,size_t(s->pitch)*height);
    auto* ptr=s.get(); surfaces_.push_back(std::move(s)); return ptr;
}
Surface* Device::create_surface(uint32_t caps,uint32_t width,uint32_t height,uint32_t back_count,uint32_t bpp) {
    if ((caps&kPrimary) && primary_) return nullptr;
    if (back_count>1 || (back_count && !(caps&kPrimary))) return nullptr;
    auto* s=make_surface(caps,width,height,bpp); if(!s) return nullptr;
    if(back_count) {
        auto* back=make_surface(kBack|kFlip,width,height,bpp);
        if(!back) return nullptr;
        s->attached=back;
    }
    if(caps&kPrimary) primary_=s;
    return s;
}
uint32_t Device::lock(Surface& s,LockInfo& out) {
    if(s.locked || !mem_.ptr(s.va,size_t(s.pitch)*s.height)) return kInvalidParam;
    s.locked=true; out={s.va,s.pitch,s.width,s.height}; return kOk;
}
uint32_t Device::unlock(Surface& s) {
    if(!s.locked) return kInvalidParam;
    s.locked=false;++s.generation; return kOk;
}
uint32_t Device::blt_fast(Surface& dst,uint32_t x,uint32_t y,Surface& src,Rect r,uint32_t flags) {
    if(dst.bpp!=8||src.bpp!=8) return kUnsupported;
    if(r.left<0||r.top<0||r.right<=r.left||r.bottom<=r.top||
       uint32_t(r.right)>src.width||uint32_t(r.bottom)>src.height||
       x>dst.width||y>dst.height) return kInvalidParam;
    const uint32_t w=std::min<uint32_t>(r.right-r.left,dst.width-x);
    const uint32_t h=std::min<uint32_t>(r.bottom-r.top,dst.height-y);
    auto* dp=mem_.ptr(dst.va,size_t(dst.pitch)*dst.height);
    auto* sp=mem_.ptr(src.va,size_t(src.pitch)*src.height);
    if(!dp||!sp) return kInvalidParam;
    for(uint32_t row=0;row<h;++row) {
        auto* d=dp+(y+row)*dst.pitch+x;
        auto* s=sp+(r.top+row)*src.pitch+r.left;
        for(uint32_t col=0;col<w;++col) {
            uint8_t v=s[col];
            if((flags&kSrcColorKey) && src.has_key && v>=src.key_low && v<=src.key_high) continue;
            d[col]=v;
        }
    }
    return kOk;
}
uint32_t Device::blt(Surface& dst,Rect dr,Surface& src,Rect sr,uint32_t flags) {
    const int64_t dx0 = dr.left, dy0 = dr.top, dx1 = dr.right, dy1 = dr.bottom;
    const int64_t sx0 = sr.left, sy0 = sr.top, sx1 = sr.right, sy1 = sr.bottom;
    const int64_t dw = dx1 - dx0, dh = dy1 - dy0;
    const int64_t sw = sx1 - sx0, sh = sy1 - sy0;
    if(dw <= 0 || dh <= 0 || dw != sw || dh != sh)
        return kUnsupported; // HW-013 no escala.
    if(dx1 <= 0 || dy1 <= 0 || dx0 >= int64_t(dst.width) || dy0 >= int64_t(dst.height))
        return kOk; // totalmente fuera del destino.
    int64_t clipped_dx0 = dx0, clipped_dy0 = dy0;
    int64_t clipped_sx0 = sx0, clipped_sy0 = sy0, clipped_sx1 = sx1, clipped_sy1 = sy1;
    if(clipped_dx0 < 0) { clipped_sx0 -= clipped_dx0; clipped_dx0 = 0; }
    if(clipped_dy0 < 0) { clipped_sy0 -= clipped_dy0; clipped_dy0 = 0; }
    if(dx1 > int64_t(dst.width)) {
        clipped_sx1 -= dx1 - int64_t(dst.width);
    }
    if(dy1 > int64_t(dst.height)) {
        clipped_sy1 -= dy1 - int64_t(dst.height);
    }
    constexpr int64_t i32_min = std::numeric_limits<int32_t>::min();
    constexpr int64_t i32_max = std::numeric_limits<int32_t>::max();
    if(clipped_sx0 < i32_min || clipped_sx0 > i32_max || clipped_sy0 < i32_min || clipped_sy0 > i32_max ||
       clipped_sx1 < i32_min || clipped_sx1 > i32_max || clipped_sy1 < i32_min || clipped_sy1 > i32_max)
        return kInvalidParam;
    const Rect clipped_src{int32_t(clipped_sx0), int32_t(clipped_sy0),
                           int32_t(clipped_sx1), int32_t(clipped_sy1)};
    return blt_fast(dst, uint32_t(clipped_dx0), uint32_t(clipped_dy0), src,
                    clipped_src, flags & kSrcColorKey);
}
uint32_t Device::flip(Surface& primary) {
    if(primary.bpp!=8||!(primary.caps&kPrimary)||!primary.attached||primary.locked||primary.attached->locked)
        return kInvalidParam;
    std::swap(primary.va,primary.attached->va);
    auto* ptr=mem_.ptr(primary.va,size_t(primary.pitch)*primary.height);
    if(!ptr) return kInvalidParam;
    presenter_.present_sized(ptr,primary.pitch,palette_.data(),primary.width,primary.height); return kOk;
}
void Device::set_entries(uint32_t start,const PaletteEntry* e,uint32_t count) {
    if(!e||start>=256||count>256-start) return;
    for(uint32_t i=0;i<count;++i)
        palette_[start+i]=0xFF000000u|(uint32_t(e[i].red)<<16)|(uint32_t(e[i].green)<<8)|e[i].blue;
    if(count) for(auto& s:surfaces_) if(s->bpp==8 && (s->caps&0x1000)) ++s->generation;
}
}
