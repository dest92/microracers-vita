#include "ddraw/bridge.h"
#include "ddraw/abi.h"
#include "d3d/d3d.h"
#include "d3d/bridge.h"
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_scratch.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort(); } } while(0)
struct CpuHost final : d2rt::Cpu {
    std::map<uint32_t,std::vector<uint8_t>> maps;
    uint32_t regs[16]{};
    d2rt::TrapFn trap;
    uint32_t callback_entry=0,callback_arg0=0,callback_arg1=0;
    unsigned callback_calls=0;
    std::vector<std::array<uint32_t,3>> display_modes;
    std::vector<std::array<uint32_t,7>> mode_changes;
    std::vector<uint8_t>* region(uint32_t va,uint32_t n,uint32_t& off) {
        auto it=maps.upper_bound(va);if(it==maps.begin()) return nullptr;--it;
        if(uint64_t(va)-it->first+n>it->second.size()) return nullptr;
        off=va-it->first;return &it->second;
    }
    bool map(uint32_t va,uint32_t size,const void* init,int) override {
        auto& v=maps[va];v.assign(size,0);if(init) std::memcpy(v.data(),init,size);return true;
    }
    bool protect(uint32_t,uint32_t,int) override {return true;}
    bool read(uint32_t va,void* dst,uint32_t n) override {uint32_t o;auto* v=region(va,n,o);if(!v)return false;std::memcpy(dst,v->data()+o,n);return true;}
    bool write(uint32_t va,const void* src,uint32_t n) override {uint32_t o;auto* v=region(va,n,o);if(!v)return false;std::memcpy(v->data()+o,src,n);return true;}
    void* hostptr(uint32_t va,uint32_t n) override {uint32_t o;auto* v=region(va,n,o);return v?v->data()+o:nullptr;}
    bool mapped(uint32_t va) override {return hostptr(va,1)!=nullptr;}
    uint32_t reg(int i) override {return regs[i];}
    void set_reg(int i,uint32_t v) override {regs[i]=v;}
    void set_trap(uint32_t,uint32_t,d2rt::TrapFn f) override {trap=std::move(f);}
    bool run(uint32_t entry,const char**) override {
        callback_entry=entry;callback_arg0=read_u32(reg(d2rt::R_ESP)+4);
        callback_arg1=read_u32(reg(d2rt::R_ESP)+8);
        if(callback_arg0 && hostptr(callback_arg0,88))
            display_modes.push_back({read_u32(callback_arg0+12),read_u32(callback_arg0+8),read_u32(callback_arg0+84)});
        ++callback_calls;set_reg(d2rt::R_EAX,1);return true;
    }
    uint32_t fault_addr() const override {return 0;}
};
static uint32_t invoke(CpuHost& c,uint32_t fn,std::initializer_list<uint32_t> args) {
    uint32_t esp=0x60010000;
    c.write_u32(esp,0x00401000);
    unsigned i=0;for(uint32_t v:args)c.write_u32(esp+4+4*i++,v);
    c.set_reg(d2rt::R_ESP,esp);c.set_reg(d2rt::R_EIP,fn);
    CHECK(c.trap(c,fn));
    CHECK(c.reg(d2rt::R_ESP)==esp+4+4*args.size());
    return c.reg(d2rt::R_EAX);
}
static uint32_t method(CpuHost& c,uint32_t obj,unsigned slot) {return c.read_u32(c.read_u32(obj)+4*slot);}
int main(int argc,char**) {
    CpuHost c;
    CHECK(c.map(0x10000000,0x100000,nullptr,3));
    CHECK(c.map(0x20000000,0x800000,nullptr,3));
    wx86_scratch_init(0x10000000,0x100000);
    d2rt::Bridge br(&c);std::string err;CHECK(br.commit(err));
    uint32_t next=0x20000000;unsigned frames=0;uint8_t frame0=0;uint32_t color0=0;
    hw::ddraw::HostOps ops;
    bool d3d_enabled=false,consume_flip=false;uint32_t texture_surface=0;unsigned flips3d=0;
    hw::d3d::ComBridge* real_d3d=nullptr;
    ops.d3d3_object=[&](){return real_d3d?real_d3d->acquire_d3d3_object():(d3d_enabled?0x100e0000u:0u);};
    ops.create_texture=[&](uint32_t surface){texture_surface=surface;return real_d3d?real_d3d->create_texture(surface):0x100e0010u;};
    ops.flip_3d=[&](uint32_t surface){CHECK(hw::ddraw::is_primary(surface));++flips3d;return real_d3d?real_d3d->flip(surface):consume_flip;};
    ops.mode_changed=[&](uint32_t w,uint32_t h,uint32_t bpp,bool accepted,uint32_t ew,uint32_t eh,uint32_t ebpp) {
        c.mode_changes.push_back({w,h,bpp,uint32_t(accepted),ew,eh,ebpp});
    };
    ops.alloc_pixels=[&](size_t bytes){uint32_t va=next;next+=uint32_t((bytes+4095)&~4095u);return va;};
    ops.present=[&](const uint8_t* p,uint32_t pitch,const uint32_t* pal){CHECK(pitch==640);++frames;frame0=p[0];color0=pal[p[0]];};
    if(argc>1) {
        ops.present_sized=[&](const uint8_t* p,uint32_t pitch,const uint32_t* pal,uint32_t w,uint32_t h) {
            CHECK(w==640 && h==480);CHECK(pitch==640);
            ++frames;frame0=p[0];color0=pal[p[0]];
        };
        ops.present=[](const uint8_t*,uint32_t,const uint32_t*) { CHECK(false); };
    }
    CHECK(hw::ddraw::install(br,c,ops));
    uint32_t out=0x100f0000,desc=out+0x100,entries=out+0x300;
    auto create=br.shim_trap("DDRAW.dll","DirectDrawCreate");CHECK(create);
    CHECK(invoke(c,create,{0,out,0})==0);
    uint32_t dd1=c.read_u32(out);CHECK(dd1 && c.read_u32(dd1));
    const uint8_t iid4[16]={0x9A,0x50,0x59,0x9C,0xBD,0x39,0xD1,0x11,0x8C,0x4A,0,0xC0,0x4F,0xD9,0x30,0xC5};
    c.write(entries,iid4,16);
    CHECK(invoke(c,method(c,dd1,0),{dd1,entries,out})==0);
    uint32_t dd4=c.read_u32(out);CHECK(dd4 && dd4!=dd1);
    auto enumerate=br.shim_trap("DDRAW.dll","DirectDrawEnumerateExA");
    CHECK(invoke(c,enumerate,{0x00402000,0x9876,4})==0);
    CHECK(c.callback_calls==1 && c.callback_entry==0x00402000 && c.callback_arg0==0);
    CHECK(c.callback_arg1 && c.hostptr(c.callback_arg1,1));
    CHECK(invoke(c,method(c,dd4,8),{dd4,0,0,0x4567,0x00402001})==0);
    CHECK(c.callback_calls==7 && c.callback_entry==0x00402001);
    const std::vector<std::array<uint32_t,3>> expected_modes={
        {320,240,16},{320,240,8},{640,480,16},{640,480,8},{800,600,16},{800,600,8}};
    CHECK(c.display_modes==expected_modes);
    CHECK(invoke(c,method(c,dd4,20),{dd4,0x1234,0x13})==0);
    // Sonda D3D: antes del error CreateDevice, la primaria debe ser RGB565 real.
    CHECK(invoke(c,method(c,dd4,21),{dd4,640,480,16,0,0})==0);
    c.write_u32(desc,136);c.write_u32(desc+4,1);c.write_u32(desc+104,0x2200);
    CHECK(invoke(c,method(c,dd4,6),{dd4,desc,out,0})==0);
    uint32_t probe=c.read_u32(out);CHECK(probe);
    CHECK(invoke(c,method(c,probe,21),{probe,entries})==0);
    CHECK(c.read_u32(entries+12)==16 && c.read_u32(entries+16)==0xF800);
    CHECK(c.read_u32(entries+20)==0x07E0 && c.read_u32(entries+24)==0x001F);
    c.write_u32(desc,136);
    CHECK(invoke(c,method(c,probe,22),{probe,desc})==0);
    CHECK(c.read_u32(desc+16)==1280 && c.read_u32(desc+84)==16);
    CHECK(c.read_u32(desc+104)==0x2200 && c.read_u32(desc+116)==0);
    CHECK(invoke(c,method(c,dd4,19),{dd4})==0); // libera el modo de sonda
    CHECK(invoke(c,method(c,dd4,21),{dd4,640,480,8,0,0})==0);
    // DDSURFACEDESC2: primaria + flip con un backbuffer.
    c.write_u32(desc,136);c.write_u32(desc+4,0x21);c.write_u32(desc+20,1);c.write_u32(desc+104,0x218);
    CHECK(invoke(c,method(c,dd4,6),{dd4,desc,out,0})==0);
    uint32_t primary=c.read_u32(out);CHECK(primary);
    c.write_u32(entries,4);
    CHECK(invoke(c,method(c,primary,12),{primary,entries,out})==0);
    uint32_t back=c.read_u32(out);CHECK(back);
    // ABI caps: DDSURFACEDESC (108) y DDSURFACEDESC2 (124/136) comparten +104.
    for(uint32_t len:{108u,124u,136u}) {
        c.write_u32(desc,len);c.write_u32(desc+116,0xDEADBEEFu);
        CHECK(invoke(c,method(c,primary,22),{primary,desc})==0);
        CHECK(c.read_u32(desc+104)==0x218);
        CHECK(c.read_u32(desc+116)==(len==108?0xDEADBEEFu:0u));
        c.write_u32(desc,len);
        CHECK(invoke(c,method(c,back,22),{back,desc})==0);
        CHECK(c.read_u32(desc+104)==(hw::ddraw::kBack|hw::ddraw::kFlip));
    }
    // Paleta inicial + SetEntries.
    c.write_u32(entries,0x00563412u); // PALETTEENTRY red=0x12, green=0x34, blue=0x56.
    CHECK(invoke(c,method(c,dd4,5),{dd4,0x44,entries,out,0})==0);
    uint32_t pal=c.read_u32(out);CHECK(pal);
    CHECK(invoke(c,method(c,primary,31),{primary,pal})==0);
    CHECK(invoke(c,method(c,pal,6),{pal,0,3,1,entries})==0);
    // Offscreen 640x480 SYSTEMMEMORY, lock y escritura real en guest.
    c.write_u32(desc,136);c.write_u32(desc+4,0x7);c.write_u32(desc+8,480);
    c.write_u32(desc+12,640);c.write_u32(desc+104,0x840);
    CHECK(invoke(c,method(c,dd4,6),{dd4,desc,out,0})==0);
    uint32_t src=c.read_u32(out);CHECK(src);
    c.write_u32(desc,136);
    CHECK(invoke(c,method(c,src,25),{src,0,desc,0,0})==0);
    CHECK(c.read_u32(desc+16)==640 && (c.read_u32(desc+4)&0x800));
    uint32_t pixels=c.read_u32(desc+36);CHECK(pixels>=0x20000000);
    uint8_t index=3;CHECK(c.write(pixels,&index,1));
    CHECK(invoke(c,method(c,src,32),{src,0})==0);
    c.write_u32(entries,0);c.write_u32(entries+4,0);c.write_u32(entries+8,640);c.write_u32(entries+12,480);
    CHECK(invoke(c,method(c,back,7),{back,0,0,src,entries,0})==0);
    CHECK(invoke(c,method(c,primary,11),{primary,0,1})==0);
    CHECK(frames==1 && frame0==3 && color0==0xFF123456u);
    const uint8_t iid3d[16]={0x40,0x32,0x22,0xBB,0x2B,0xE7,0xD0,0x11,0xA9,0xB4,0,0xAA,0,0xC0,0x99,0x3E};
    c.write(entries,iid3d,16);
    CHECK(invoke(c,method(c,dd4,0),{dd4,entries,out})==0);
    uint32_t d3d=c.read_u32(out);CHECK(d3d);
    c.write_u32(out,0xdeadbeefu);
    CHECK(invoke(c,method(c,d3d,8),{d3d,entries,primary,out,0})!=0);
    CHECK(c.read_u32(out)==0);
    uint32_t rw=0,rh=0;
    CHECK(hw::ddraw::render_target(primary,rw,rh) && rw==640 && rh==480);
    CHECK(hw::ddraw::is_primary(primary) && !hw::ddraw::is_primary(back));
    CHECK(!hw::ddraw::render_target(0xBAD,rw,rh));
    CHECK(hw::ddraw::render_target(back,rw,rh) && rw==640 && rh==480);
    CHECK(!hw::ddraw::render_target(probe,rw,rh)); // sonda de modo cerrado
    d3d_enabled=true;
    CHECK(invoke(c,method(c,dd4,0),{dd4,entries,out})==0);
    CHECK(c.read_u32(out)==0x100e0000);
    consume_flip=true;unsigned old_frames=frames;
    CHECK(invoke(c,method(c,primary,11),{primary,0,1})==0);
    CHECK(frames==old_frames && flips3d==2);
    consume_flip=false;
    CHECK(invoke(c,method(c,primary,11),{primary,0,1})==0);
    CHECK(frames==old_frames+1 && flips3d==3);
    // Texture1555 también en modo8: formato explícito independiente del display.
    for(unsigned off=0;off<136;off+=4)c.write_u32(desc+off,0);
    c.write_u32(desc,136);c.write_u32(desc+4,0x1007);
    c.write_u32(desc+8,16);c.write_u32(desc+12,32);c.write_u32(desc+104,0x1000);
    c.write_u32(desc+72,32);c.write_u32(desc+76,0x41);c.write_u32(desc+84,16);
    c.write_u32(desc+88,0x7C00);c.write_u32(desc+92,0x3E0);c.write_u32(desc+96,0x1F);c.write_u32(desc+100,0x8000);
    CHECK(invoke(c,method(c,dd4,6),{dd4,desc,out,0})==0);
    uint32_t texture=c.read_u32(out);
    hw::d3d::TextureSource ts{};
    CHECK(!hw::ddraw::texture_source(primary,ts));
    CHECK(hw::ddraw::texture_source(texture,ts));
    CHECK(ts.width==32 && ts.height==16 && ts.pitch==64 && ts.generation==0);
    CHECK(ts.format==hw::d3d::TexFormat::ARGB1555 && ts.pixels);
    c.write_u32(desc,136);
    CHECK(invoke(c,method(c,texture,25),{texture,0,desc,0,0})==0);
    CHECK(c.read_u32(desc+88)==0x7C00 && c.read_u32(desc+100)==0x8000);
    CHECK(invoke(c,method(c,texture,32),{texture,0})==0);
    CHECK(hw::ddraw::texture_source(texture,ts) && ts.generation==1);
    CHECK(invoke(c,method(c,texture,32),{texture,0})!=0);
    CHECK(hw::ddraw::texture_source(texture,ts) && ts.generation==1);
    c.write_u32(entries,0x8123);c.write_u32(entries+4,0xABCD);
    CHECK(invoke(c,method(c,texture,29),{texture,8,entries})==0);
    CHECK(hw::ddraw::texture_source(texture,ts) && ts.has_key && ts.key_low==0x8123 && ts.key_high==0xABCD);
    const uint8_t iidtex[16]={0x02,0x15,0x28,0x93,0xF8,0x8C,0xD0,0x11,0x89,0xAB,0,0xA0,0xC9,5,0x41,0x29};
    c.write(entries,iidtex,16);
    CHECK(invoke(c,method(c,texture,0),{texture,entries,out})==0);
    CHECK(c.read_u32(out)==0x100e0010 && texture_surface==texture);
    CHECK(invoke(c,method(c,primary,0),{primary,entries,out})!=0);
    hw::d3d::RefRaster raster;
    hw::d3d::ComHostOps dops;
    dops.render_target=hw::ddraw::render_target;
    dops.texture_source=hw::ddraw::texture_source;
    dops.is_primary=hw::ddraw::is_primary;
    hw::d3d::ComBridge real(br,c,raster,dops);
    CHECK(real.install());real_d3d=&real;
    c.write(entries,iid3d,16);
    CHECK(invoke(c,method(c,dd4,0),{dd4,entries,out})==0);
    CHECK(c.read_u32(out)==real.d3d3_object());
    const uint32_t acquired=c.read_u32(out);
    CHECK(invoke(c,method(c,acquired,2),{acquired})==1);
    CHECK(invoke(c,method(c,dd4,0),{dd4,entries,out})==0);
    CHECK(c.read_u32(out)==acquired);
    CHECK(invoke(c,method(c,acquired,2),{acquired})==1);
    const uint8_t hal[16]={0xE0,0x3D,0xE6,0x84,0xAA,0x46,0xCF,0x11,0x81,0x6F,0,0,0xC0,0x20,0x15,0x6E};
    c.write(entries,hal,16);
    CHECK(invoke(c,method(c,real.d3d3_object(),8),{real.d3d3_object(),entries,primary,out,0})==0);
    CHECK(c.read_u32(out));
    c.write(entries,iidtex,16);
    CHECK(invoke(c,method(c,texture,0),{texture,entries,out})==0);
    CHECK(c.read_u32(out) && method(c,c.read_u32(out),0));
    old_frames=frames;
    CHECK(invoke(c,method(c,primary,11),{primary,0,1})==0);
    CHECK(frames==old_frames);
    real_d3d=nullptr;
    for(auto format:{hw::d3d::TexFormat::PAL8,hw::d3d::TexFormat::RGB565,
                     hw::d3d::TexFormat::ARGB4444,hw::d3d::TexFormat::ARGB8888}) {
        for(unsigned off=0;off<136;off+=4)c.write_u32(desc+off,0);
        c.write_u32(desc,136);c.write_u32(desc+4,0x1007);
        c.write_u32(desc+8,16);c.write_u32(desc+12,32);c.write_u32(desc+104,0x1000);
        uint32_t bits=format==hw::d3d::TexFormat::PAL8?8:format==hw::d3d::TexFormat::ARGB8888?32:16;
        c.write_u32(desc+72,32);c.write_u32(desc+76,bits==8?0x60:0x41);c.write_u32(desc+84,bits);
        uint32_t rm=0xF800,gm=0x7E0,bm=0x1F,am=0;
        if(format==hw::d3d::TexFormat::ARGB4444){rm=0xF00;gm=0xF0;bm=0xF;am=0xF000;}
        if(bits==32){rm=0xFF0000;gm=0xFF00;bm=0xFF;am=0xFF000000;}
        c.write_u32(desc+88,rm);c.write_u32(desc+92,gm);c.write_u32(desc+96,bm);c.write_u32(desc+100,am);
        CHECK(invoke(c,method(c,dd4,6),{dd4,desc,out,0})==0);
        uint32_t surface=c.read_u32(out);
        CHECK(hw::ddraw::texture_source(surface,ts) && ts.format==format && ts.pitch==32*(bits/8));
        if(bits==8) {
            CHECK(ts.palette && ts.generation==0);
            c.write_u32(entries,0x00563412);
            CHECK(invoke(c,method(c,pal,6),{pal,0,0,1,entries})==0);
            CHECK(hw::ddraw::texture_source(surface,ts) && ts.generation==1 && ts.palette[0]==0xFF123456);
        }
    }
    c.write_u32(desc+84,16);c.write_u32(desc+88,1);
    CHECK(invoke(c,method(c,dd4,6),{dd4,desc,out,0})!=0 && c.read_u32(out)==0);
    const size_t mode_base=c.mode_changes.size();
    CHECK(invoke(c,method(c,dd4,21),{dd4,800,600,16,0,0})==0);
    CHECK(invoke(c,method(c,dd4,21),{dd4,320,240,8,0,0})==0);
    CHECK(invoke(c,method(c,dd4,21),{dd4,800,480,8,0,0})!=0);
    CHECK(c.mode_changes.size()==mode_base+3);
    CHECK((c.mode_changes[mode_base]==std::array<uint32_t,7>{800,600,16,1,800,600,16}));
    CHECK((c.mode_changes[mode_base+1]==std::array<uint32_t,7>{320,240,8,1,320,240,8}));
    CHECK((c.mode_changes[mode_base+2]==std::array<uint32_t,7>{800,480,8,0,320,240,8}));
    // A mode switch releases the active-primary designation so the game can
    // create its new 800x600 primary/backbuffer pair while old COM refs live.
    CHECK(invoke(c,method(c,dd4,21),{dd4,640,480,8,0,0})==0);
    CHECK(invoke(c,method(c,dd4,21),{dd4,800,600,8,0,0})==0);
    CHECK(!hw::ddraw::is_primary(primary));
    CHECK(!hw::ddraw::render_target(primary,rw,rh));
    CHECK(invoke(c,method(c,primary,14),{primary,entries})==0);
    CHECK((c.read_u32(entries)&hw::ddraw::kPrimary)==0);
    CHECK(invoke(c,method(c,primary,11),{primary,0,1})!=0);
    c.write_u32(desc,136);c.write_u32(desc+4,0x21);c.write_u32(desc+20,1);c.write_u32(desc+104,0x218);
    CHECK(invoke(c,method(c,dd4,6),{dd4,desc,out,0})==0);
    uint32_t primary800=c.read_u32(out);CHECK(primary800 && hw::ddraw::is_primary(primary800));
    c.write_u32(entries,4);
    CHECK(invoke(c,method(c,primary800,12),{primary800,entries,out})==0);
    uint32_t back800=c.read_u32(out);CHECK(back800 && !hw::ddraw::is_primary(back800));
    CHECK(hw::ddraw::render_target(back800,rw,rh) && rw==800 && rh==600);
}
