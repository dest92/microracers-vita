#include "ddraw/software.h"
#include "ddraw/png.h"
#include "ddraw/abi.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while (0)
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
using namespace hw::ddraw;
struct TestMemory final : Memory {
    uint32_t next = 0x10000000;
    std::map<uint32_t,std::vector<uint8_t>> blocks;
    uint32_t alloc(size_t n) override { auto va=next; next+=(n+4095)&~4095u; blocks[va].resize(n); return va; }
    uint8_t* ptr(uint32_t va,size_t n) override {
        auto it=blocks.upper_bound(va); if(it==blocks.begin()) return nullptr; --it;
        size_t off=va-it->first; return off+n<=it->second.size()?it->second.data()+off:nullptr;
    }
};
struct Capture final : Presenter {
    unsigned calls=0; uint8_t first=0; uint32_t color=0;
    void present(const uint8_t* idx,uint32_t,const uint32_t pal[256]) override {
        ++calls; first=idx[0]; color=pal[first];
    }
};
int main() {
    static_assert(slot::dd_create_surface==6 && slot::dd_set_mode==21);
    static_assert(slot::surf_lock==25 && slot::surf_unlock==32);
    static_assert(slot::surf_blt_fast==7 && slot::surf_flip==11);
    static_assert(slot::palette_set_entries==6 && slot::d3d_create_device==8);
    static_assert(abi::ddraw1.size()==23 && abi::ddraw4.size()==28);
    static_assert(abi::surface4.size()==45 && abi::palette.size()==7 && abi::d3d3.size()==12);
    static_assert(abi::ddraw4[slot::dd_create_surface]==3);
    static_assert(abi::ddraw4[slot::dd_set_mode]==5);
    static_assert(abi::surface4[slot::surf_blt_fast]==5);
    static_assert(abi::surface4[slot::surf_lock]==4 && abi::surface4[slot::surf_unlock]==1);
    static_assert(abi::surface4[slot::surf_flip]==2 && abi::palette[slot::palette_set_entries]==4);
    static_assert(abi::d3d3[slot::d3d_create_device]==4);
    TestMemory mem; Capture display; Device dd(mem,display);
    auto* primary=dd.create_surface(kPrimary|kComplex|kFlip,640,480,1);
    CHECK(primary && primary->attached && primary->attached->caps&kBack);
    auto* back=primary->attached;
    auto* src=dd.create_surface(kOffscreen|kSysmem,640,480,0);
    CHECK(src); LockInfo lock{};
    CHECK(dd.lock(*src,lock)==kOk && lock.pitch==640 && lock.va==src->va);
    auto* px=mem.ptr(lock.va,640*480); CHECK(px);
    px[0]=3; px[1]=7; CHECK(dd.unlock(*src)==kOk);
    CHECK(mem.ptr(src->va,2)[0]==3);
    src->has_key=true; src->key_low=src->key_high=7;
    CHECK(dd.blt_fast(*back,0,0,*src,{0,0,2,1},kSrcColorKey)==kOk);
    CHECK(mem.ptr(back->va,2)[0]==3 && mem.ptr(back->va,2)[1]==0);
    CHECK(dd.blt(*back,{2,0,4,1},*src,{0,0,2,1},kSrcColorKey)==kOk);
    CHECK(mem.ptr(back->va,4)[2]==3 && mem.ptr(back->va,4)[3]==0);
    CHECK(dd.blt(*back,{-1,1,1,2},*src,{0,0,2,1},0)==kOk);
    CHECK(mem.ptr(back->va+640,1)[0]==7);
    PaletteEntry entry{0x12,0x34,0x56,0}; dd.set_entries(3,&entry,1);
    auto old_front=primary->va, old_back=back->va;
    CHECK(dd.flip(*primary)==kOk);
    CHECK(primary->va==old_back && back->va==old_front);
    CHECK(display.calls==1 && display.first==3 && display.color==0xFF123456u);
    uint32_t obj=42; CHECK(dd.create_device(obj)!=kOk && obj==0);
    // Backend host: PNG RGBA sintético; el runner valida IDAT descomprimido.
    std::filesystem::create_directories("build");
    PngPresenter png("build/test-ddraw.png",2,1);
    const uint8_t indices[2]={3,7};
    png.present(indices,2,dd.palette());
}
