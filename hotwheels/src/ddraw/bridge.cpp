#include "shims/guest_call.h"
#include "bridge.h"
#include "d3d/d3d.h"
#include "abi.h"
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_scratch.h"
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace hw::ddraw {
namespace {
using d2rt::Bridge; using d2rt::Cpu; using d2rt::Shim;
constexpr uint32_t kNo3D=0x887600AAu; // DDERR_NO3D = MAKE_DDHRESULT(170)
constexpr uint32_t kNotFound=0x887600FFu;
constexpr uint32_t kAlready=0x88760005u;
constexpr uint32_t kBltKeySrc=0x8000u;
constexpr uint32_t kGuidDD=0x6C14DB80u,kGuidDD4=0x9C59509Au,
                   kGuidSurf=0x6C14DB81u,kGuidSurf4=0x0B2B8630u,
                   kGuidPal=0x6C14DB84u,kGuidD3D3=0xBB223240u;
constexpr uint32_t kGuidUnknown=0x00000000u;
constexpr uint32_t kTexture=0x1000,kGuidTexture2=0x93281502u;
enum class Kind { DD1,DD4,D3D3,Surf1,Surf4,Pal };
struct Rec { Kind kind; Surface* surface=nullptr; uint32_t refs=1; uint32_t peer=0; };
struct GuestMem : Memory {
    Cpu& cpu; HostOps ops;
    GuestMem(Cpu& c,const HostOps& o):cpu(c),ops(o){}
    uint32_t alloc(size_t n) override { return ops.alloc_pixels?ops.alloc_pixels(n):0; }
    uint8_t* ptr(uint32_t va,size_t n) override { return static_cast<uint8_t*>(cpu.hostptr(va,uint32_t(n))); }
};
struct GuestPresenter : Presenter {
    HostOps ops; explicit GuestPresenter(const HostOps& o):ops(o){}
    void present(const uint8_t* i,uint32_t p,const uint32_t* pal) override {
        if(ops.present) ops.present(i,p,pal);
    }
    void present_sized(const uint8_t* i,uint32_t p,const uint32_t* pal,uint32_t w,uint32_t h) override {
        if(ops.present_sized) ops.present_sized(i,p,pal,w,h);
        else present(i,p,pal);
    }
};
struct Adapter {
    Bridge& br; Cpu& cpu; HostOps ops; GuestMem mem; GuestPresenter pres; Device dev;
    std::map<uint32_t,Rec> recs;
    std::array<uint32_t,6> vt{};
    uint32_t dd1=0,dd4=0,d3d3=0,pal=0;
    uint32_t mode_bpp=8,mode_w=640,mode_h=480,cooperative=0;
    bool has_palette=false;
    Adapter(Bridge& b,Cpu& c,const HostOps& o):br(b),cpu(c),ops(o),mem(c,o),pres(o),dev(mem,pres){}
    uint32_t r(uint32_t a) { return cpu.read_u32(a); }
    void w(uint32_t a,uint32_t v) { cpu.write_u32(a,v); }
    bool mapped(uint32_t a,uint32_t n) { return a && cpu.hostptr(a,n); }
    Rect rect(uint32_t a) { return {int32_t(r(a)),int32_t(r(a+4)),int32_t(r(a+8)),int32_t(r(a+12))}; }
    uint32_t object(Kind k,Surface* s=nullptr) {
        uint32_t a=wx86_scratch_alloc(8); if(!a) return 0;
        w(a,vt[size_t(k)]);w(a+4,0x44524157u);recs.emplace(a,Rec{k,s,1,0});return a;
    }
    Rec* rec(uint32_t a) { auto it=recs.find(a);return it==recs.end()?nullptr:&it->second; }
    bool guid(uint32_t a,uint32_t dword) {
        if(!mapped(a,16)) return false;
        // GUID completo en orden de bytes little-endian del guest.
        static const std::map<uint32_t,std::array<uint8_t,16>> known={
            {kGuidUnknown,{0,0,0,0,0,0,0,0,0xC0,0,0,0,0,0,0,0x46}},
            {kGuidDD,{0x80,0xDB,0x14,0x6C,0x33,0xA7,0xCE,0x11,0xA5,0x21,0,0x20,0xAF,0x0B,0xE5,0x60}},
            {kGuidDD4,{0x9A,0x50,0x59,0x9C,0xBD,0x39,0xD1,0x11,0x8C,0x4A,0,0xC0,0x4F,0xD9,0x30,0xC5}},
            {kGuidSurf,{0x81,0xDB,0x14,0x6C,0x33,0xA7,0xCE,0x11,0xA5,0x21,0,0x20,0xAF,0x0B,0xE5,0x60}},
            {kGuidSurf4,{0x30,0x86,0x2B,0x0B,0x35,0xAD,0xD0,0x11,0x8E,0xA6,0,0x60,0x97,0x97,0xEA,0x5B}},
            {kGuidPal,{0x84,0xDB,0x14,0x6C,0x33,0xA7,0xCE,0x11,0xA5,0x21,0,0x20,0xAF,0x0B,0xE5,0x60}},
            {kGuidD3D3,{0x40,0x32,0x22,0xBB,0x2B,0xE7,0xD0,0x11,0xA9,0xB4,0,0xAA,0,0xC0,0x99,0x3E}},
            {kGuidTexture2,{0x02,0x15,0x28,0x93,0xF8,0x8C,0xD0,0x11,0x89,0xAB,0,0xA0,0xC9,0x05,0x41,0x29}}
        };
        auto it=known.find(dword);if(it==known.end()) return false;
        std::array<uint8_t,16> got{};cpu.read(a,got.data(),16);
        return got==it->second;
    }
    uint32_t invoke_callback(uint32_t fn,const std::vector<uint32_t>& args) {
        if(!fn) return 0;
        std::array<uint32_t,10> regs{};
        for(int i=0;i<10;++i) regs[i]=cpu.reg(d2rt::Reg(i));
        uint32_t result=0;const char* fault=nullptr;
        bool ok=hw::guest_call(br,fn,args,result,&fault);
        for(int i=0;i<10;++i) cpu.set_reg(d2rt::Reg(i),regs[i]);
        return ok?result:0;
    }
    void fill_desc(uint32_t a,const Surface& s,uint32_t len) {
        if(len<108) return;
        for(uint32_t off=4;off+4<=len && off<136;off+=4) w(a+off,0);
        w(a+4,0x180Fu); // CAPS|HEIGHT|WIDTH|PITCH|LPSURFACE|PIXELFORMAT
        w(a+8,s.height);w(a+12,s.width);w(a+16,s.pitch);w(a+36,s.va);
        w(a+72,32);w(a+76,s.bpp==8?0x60:0x40);w(a+84,s.bpp);
        if(s.bpp!=8) { w(a+88,s.red_mask);w(a+92,s.green_mask);w(a+96,s.blue_mask);w(a+100,s.alpha_mask);
            if(s.alpha_mask) w(a+76,0x41); }
        w(a+104,s.caps); // DDSURFACEDESC y DDSURFACEDESC2: ddsCaps en 0x68.
    }
    uint32_t qi(uint32_t self,uint32_t iid,uint32_t out) {
        if(!mapped(out,4)) return kInvalidParam;
        w(out,0);
        auto* p=rec(self);if(!p||!mapped(iid,16)) return kInvalidParam;
        uint32_t target=0;
        if(p->kind==Kind::DD1||p->kind==Kind::DD4||p->kind==Kind::D3D3) {
            if(guid(iid,kGuidDD)) target=dd1;
            else if(guid(iid,kGuidDD4)) target=dd4;
            else if(guid(iid,kGuidD3D3)) { target=mem.ops.d3d3_object?mem.ops.d3d3_object():0;if(!target)target=d3d3; }
        } else if(p->kind==Kind::Surf1||p->kind==Kind::Surf4) {
            if(guid(iid,kGuidSurf)) target=p->kind==Kind::Surf1?self:p->peer;
            else if(guid(iid,kGuidSurf4)) target=p->kind==Kind::Surf4?self:p->peer;
            else if(guid(iid,kGuidD3D3)) { target=mem.ops.d3d3_object?mem.ops.d3d3_object():0;if(!target)target=d3d3; }
            else if(guid(iid,kGuidTexture2) && p->surface && (p->surface->caps&kTexture) && mem.ops.create_texture)
                target=mem.ops.create_texture(p->kind==Kind::Surf4?self:p->peer);
        } else if(p->kind==Kind::Pal && guid(iid,kGuidPal)) target=pal;
        if(!target && guid(iid,kGuidUnknown)) target=self;
        if(!target) return kNoInterface;
        auto* t=rec(target);if(t) ++t->refs;w(out,target);return kOk;
    }
    uint32_t surface_object(Surface* s,Kind requested) {
        for(auto& [a,p]:recs) if(p.surface==s&&p.kind==requested) { ++p.refs; return a; }
        auto a=object(Kind::Surf1,s),b=object(Kind::Surf4,s);
        if(!a||!b) return 0;
        recs[a].peer=b;recs[b].peer=a;
        return requested==Kind::Surf1?a:b;
    }
    uint32_t call(Kind kind,unsigned slot,Cpu& c) {
        uint32_t self=c.arg(0);
        if(slot==0) return qi(self,c.arg(1),c.arg(2));
        auto* p=rec(self);if(!p||p->kind!=kind) return kInvalidParam;
        if(slot==1) return ++p->refs;
        if(slot==2) return p->refs?--p->refs:0;
        if(kind==Kind::DD1||kind==Kind::DD4) return dd_call(kind,slot,c);
        if(kind==Kind::Surf1||kind==Kind::Surf4) return surf_call(kind,slot,c,*p);
        if(kind==Kind::Pal) return pal_call(slot,c);
        if(kind==Kind::D3D3 && slot==8) { if(mapped(c.arg(3),4)) w(c.arg(3),0); return kNo3D; }
        return kUnsupported;
    }
    uint32_t dd_call(Kind kind,unsigned s,Cpu& c) {
        switch(s) {
        case 3: return kOk;
        case 5: {
            auto out=c.arg(3);if(!mapped(out,4)) return kInvalidParam;
            if(!pal) pal=object(Kind::Pal);
            if(!pal) return kUnsupported;
            w(out,pal);if(c.arg(2)&&mapped(c.arg(2),1024)) {
                std::array<PaletteEntry,256> e{};cpu.read(c.arg(2),e.data(),1024);dev.set_entries(0,e.data(),256); }
            return kOk;
        }
        case 6: {
            auto desc=c.arg(1),out=c.arg(2);if(!mapped(desc,108)||!mapped(out,4)) return kInvalidParam;
            w(out,0);uint32_t len=r(desc);if((len!=108&&len!=124&&len!=136)||!mapped(desc,len)) return kInvalidParam;
            uint32_t caps=r(desc+104);
            uint32_t width=(r(desc+4)&4)?r(desc+12):mode_w;
            uint32_t height=(r(desc+4)&2)?r(desc+8):mode_h;
            uint32_t back=(r(desc+4)&0x20)?r(desc+20):0;
            const bool texture=(caps&kTexture)!=0;
            uint32_t bpp=texture&&(r(desc+4)&0x1000)?r(desc+84):mode_bpp;
            uint32_t rm=0xF800,gm=0x07E0,bm=0x001F,am=0;
            if(texture && (r(desc+4)&0x1000) && bpp!=8) {
                rm=r(desc+88);gm=r(desc+92);bm=r(desc+96);am=r(desc+100);
                bool valid=(bpp==16 && ((rm==0xF800&&gm==0x07E0&&bm==0x001F&&am==0)||
                    (rm==0x7C00&&gm==0x03E0&&bm==0x001F&&am==0x8000)||
                    (rm==0x0F00&&gm==0x00F0&&bm==0x000F&&am==0xF000)))||
                    (bpp==32&&rm==0x00FF0000&&gm==0x0000FF00&&bm==0x000000FF&&am==0xFF000000);
                if(!valid)return kUnsupported;
            }
            // D3D6 usa offscreens RGB565 en system memory para HUD y staging.
            // Rechazarlos por no ser primarios fuerza al juego a descartar D3D.
            if(mode_bpp!=8 && !(caps&kPrimary) && !texture && !(caps&kSysmem)) return kNo3D;
            Surface* surf=dev.create_surface(caps,width,height,back,bpp);
            if(!surf) return kUnsupported;
            surf->red_mask=rm;surf->green_mask=gm;surf->blue_mask=bm;surf->alpha_mask=am;
            uint32_t obj=surface_object(surf,kind==Kind::DD4?Kind::Surf4:Kind::Surf1);
            if(!obj) return kUnsupported;
            w(out,obj);return kOk;
        }
        case 8: {
            uint32_t cb=c.arg(4),ctx=c.arg(3); if(!cb) return kInvalidParam;
            uint32_t desc=wx86_scratch_alloc(136);if(!desc) return kUnsupported;
            for(const auto& [width,height]:std::array<std::array<uint32_t,2>,3>{{{{320,240}},{{640,480}},{{800,600}}}}) {
                for(uint32_t bpp:{16u,8u}) {
                    for(uint32_t off=0;off<136;off+=4) w(desc+off,0);
                    w(desc,kind==Kind::DD4?136:108);w(desc+4,0x1007);w(desc+8,height);w(desc+12,width);
                    w(desc+72,32);w(desc+76,bpp==8?0x60:0x40);w(desc+84,bpp);
                    if(bpp==16) { w(desc+88,0xF800);w(desc+92,0x07E0);w(desc+96,0x001F); }
                    if(!invoke_callback(cb,{desc,ctx})) return kOk;
                }
            }
            return kOk;
        }
        case 11: return kUnsupported; // GetCaps requiere DDCORECAPS específico; no observado en traza.
        case 12: {
            uint32_t out=c.arg(1);if(!mapped(out,108)) return kInvalidParam;
            uint32_t len=r(out);if(len!=108&&len!=124&&len!=136) return kInvalidParam;
            for(uint32_t off=4;off+4<=len;off+=4) w(out+off,0);
            w(out+4,0x1007);w(out+8,mode_h);w(out+12,mode_w);
            w(out+72,32);w(out+76,mode_bpp==8?0x60:0x40);w(out+84,mode_bpp);
            if(mode_bpp==16) { w(out+88,0xF800);w(out+92,0x07E0);w(out+96,0x001F); }
            return kOk;
        }
        case 18: return kAlready;
        case 19: mode_bpp=8;dev.reset_primary();return kOk;
        case 20: cooperative=c.arg(2);return kOk;
        case 21: {
            auto w=c.arg(1),h=c.arg(2),bpp=c.arg(3);
            const bool accepted=((w==640&&h==480)||(w==800&&h==600)||(w==320&&h==240))&&(bpp==8||bpp==16);
            if(accepted) {
                dev.reset_primary();
                mode_w=w;mode_h=h;mode_bpp=bpp;
            }
            if(ops.mode_changed) ops.mode_changed(w,h,bpp,accepted,mode_w,mode_h,mode_bpp);
            return accepted?kOk:kUnsupported;
        }
        case 22: return kOk;
        case 23: {
            const uint32_t total=c.arg(2),free=c.arg(3);
            if(!mapped(total,4)||!mapped(free,4)) return kInvalidParam;
            w(total,16u*1024u*1024u);w(free,8u*1024u*1024u);
            return kOk;
        }
        case 25: case 26: return kOk;
        default:return kUnsupported;
        }
    }
    uint32_t surf_call(Kind kind,unsigned s,Cpu& c,Rec& p) {
        Surface* surf=p.surface;if(!surf) return kInvalidParam;
        switch(s) {
        case 5: {
            auto* src=rec(c.arg(3));if(!src||!src->surface) return kInvalidParam;
            Rect dst=c.arg(1)?rect(c.arg(1)):Rect{0,0,int32_t(surf->width),int32_t(surf->height)};
            Rect sr=c.arg(4)?rect(c.arg(4)):Rect{0,0,int32_t(src->surface->width),int32_t(src->surface->height)};
            return dev.blt(*surf,dst,*src->surface,sr,(c.arg(5)&kBltKeySrc)?kSrcColorKey:0);
        }
        case 7: {
            auto* src=rec(c.arg(3));if(!src||!src->surface) return kInvalidParam;
            Rect sr=c.arg(4)?rect(c.arg(4)):Rect{0,0,int32_t(src->surface->width),int32_t(src->surface->height)};
            return dev.blt_fast(*surf,c.arg(1),c.arg(2),*src->surface,sr,c.arg(5)&kSrcColorKey);
        }
        case 11: {
            const uint32_t canonical=kind==Kind::Surf4?c.arg(0):p.peer;
            if((surf->caps&kPrimary) && mem.ops.flip_3d && mem.ops.flip_3d(canonical))return kOk;
            return dev.flip(*surf);
        }
        case 12: {
            auto caps=c.arg(1),out=c.arg(2);if(!mapped(caps,4)||!mapped(out,4)) return kInvalidParam;
            w(out,0);if(!(r(caps)&kBack)||!surf->attached) return kNotFound;
            auto a=surface_object(surf->attached,kind);if(!a) return kUnsupported;w(out,a);return kOk;
        }
        case 13:case 18:case 24:case 27:return kOk;
        case 14: if(!mapped(c.arg(1),4)) return kInvalidParam;w(c.arg(1),surf->caps);return kOk;
        case 16: {
            if(!mapped(c.arg(2),8)||!surf->has_key) return kNotFound;
            w(c.arg(2),surf->key_low);w(c.arg(2)+4,surf->key_high);return kOk;
        }
        case 20: if(!mapped(c.arg(1),4)||!pal) return kNotFound;w(c.arg(1),pal);++recs[pal].refs;return kOk;
        case 21: {
            auto out=c.arg(1);if(!mapped(out,32)) return kInvalidParam;
            for(uint32_t i=0;i<32;i+=4) w(out+i,0);
            w(out,32);w(out+4,surf->bpp==8?0x60:0x40);w(out+12,surf->bpp);
            if(surf->bpp!=8) { w(out+16,surf->red_mask);w(out+20,surf->green_mask);w(out+24,surf->blue_mask);w(out+28,surf->alpha_mask);
                if(surf->alpha_mask)w(out+4,0x41); }
            return kOk;
        }
        case 22: {
            auto out=c.arg(1);if(!mapped(out,4)) return kInvalidParam;
            uint32_t len=r(out);if((len!=108&&len!=124&&len!=136)||!mapped(out,len)) return kInvalidParam;
            fill_desc(out,*surf,len);return kOk;
        }
        case 25: {
            auto out=c.arg(2);if(!mapped(out,4)) return kInvalidParam;
            uint32_t len=r(out);if((len!=108&&len!=124&&len!=136)||!mapped(out,len)) return kInvalidParam;
            Rect rct{0,0,0,0};
            if(c.arg(1)) { rct=rect(c.arg(1));
                if(rct.left<0||rct.top<0||uint32_t(rct.left)>=surf->width||uint32_t(rct.top)>=surf->height) return kInvalidParam; }
            LockInfo li{};auto hr=dev.lock(*surf,li);if(hr) return hr;
            fill_desc(out,*surf,len);
            if(c.arg(1)) w(out+36,li.va+uint32_t(rct.top)*li.pitch+uint32_t(rct.left)*(surf->bpp/8));
            return kOk;
        }
        case 29: {
            auto key=c.arg(2);if(!mapped(key,8)) return kInvalidParam;
            surf->has_key=true;surf->key_low=r(key);surf->key_high=r(key+4);return kOk;
        }
        case 31: has_palette=rec(c.arg(1))&&rec(c.arg(1))->kind==Kind::Pal;return has_palette?kOk:kInvalidParam;
        case 32: return dev.unlock(*surf);
        case 36: {auto out=c.arg(1);if(!mapped(out,4)) return kInvalidParam;w(out,kind==Kind::Surf4?dd4:dd1);return kOk;}
        case 37:case 38:return kOk;
        default:return kUnsupported;
        }
    }
    uint32_t pal_call(unsigned s,Cpu& c) {
        if(s==3) {if(!mapped(c.arg(1),4)) return kInvalidParam;w(c.arg(1),0x44);return kOk;}
        if(s==6) {
            uint32_t start=c.arg(2),count=c.arg(3),a=c.arg(4);
            if(start>256||count>256-start||!mapped(a,count*4)) return kInvalidParam;
            std::vector<PaletteEntry> e(count);cpu.read(a,e.data(),count*4);dev.set_entries(start,e.data(),count);return kOk;
        }
        return kUnsupported;
    }
    void register_vtable(Kind kind,const std::vector<uint8_t>& arity) {
        auto k=size_t(kind);vt[k]=wx86_scratch_alloc(uint32_t(arity.size()*4));
        for(size_t i=0;i<arity.size();++i) {
            std::string name="HW_"+std::to_string(k)+"_"+std::to_string(i);
            Shim sh;sh.argc=1+arity[i];sh.stdcall_cleanup=true;
            sh.fn=[this,kind,i](Cpu& c){return call(kind,unsigned(i),c);};
            br.register_shim("HWDDRAWVT.dll",name,sh);
            w(vt[k]+uint32_t(4*i),br.shim_trap("HWDDRAWVT.dll",name));
        }
    }
    bool setup() {
        register_vtable(Kind::DD1,{abi::ddraw1.begin(),abi::ddraw1.end()});
        register_vtable(Kind::DD4,{abi::ddraw4.begin(),abi::ddraw4.end()});
        register_vtable(Kind::D3D3,{abi::d3d3.begin(),abi::d3d3.end()});
        register_vtable(Kind::Surf1,{abi::surface4.begin(),abi::surface4.end()});
        register_vtable(Kind::Surf4,{abi::surface4.begin(),abi::surface4.end()});
        register_vtable(Kind::Pal,{abi::palette.begin(),abi::palette.end()});
        for(auto va:vt) if(!va) return false;
        dd1=object(Kind::DD1);dd4=object(Kind::DD4);d3d3=object(Kind::D3D3);
        if(!dd1||!dd4||!d3d3) return false;
        auto reg_export=[this](const char* name,uint32_t argc,std::function<uint32_t(Cpu&)> fn) {
            Shim sh;sh.argc=argc;sh.stdcall_cleanup=true;sh.fn=std::move(fn);
            br.register_shim("DDRAW.dll",name,std::move(sh));
        };
        reg_export("DirectDrawCreate",3,[this](Cpu& c) {
            if(!mapped(c.arg(1),4)||c.arg(2)) return kInvalidParam;
            // Una nueva instancia empieza sin primaria: la sonda 16 bpp
            // libera DirectDraw y el juego crea otra para el menú 8 bpp.
            dev.reset_primary(); mode_bpp=8;
            w(c.arg(1),dd1);++recs[dd1].refs;return kOk;
        });
        reg_export("DirectDrawEnumerateExA",3,[this](Cpu& c) {
            auto cb=c.arg(0);if(!cb) return kInvalidParam;
            auto name=wx86_scratch_put_cstr(cpu,"HW DirectDraw software");
            auto desc=wx86_scratch_put_cstr(cpu,"Primary display");
            if(!name||!desc) return kUnsupported;
            invoke_callback(cb,{0,desc,name,c.arg(1),0});return kOk;
        });
        return true;
    }
};
std::unique_ptr<Adapter> g_adapter;
}
bool render_target(uint32_t surface,uint32_t& width,uint32_t& height) {
    auto* rec=g_adapter?g_adapter->rec(surface):nullptr;
    if(!rec||!rec->surface||(rec->surface->caps&kTexture))return false;
    const auto* primary=g_adapter->dev.primary();
    if(!primary || (rec->surface!=primary && rec->surface!=primary->attached))return false;
    width=rec->surface->width;height=rec->surface->height;return true;
}
bool is_primary(uint32_t surface) {
    auto* rec=g_adapter?g_adapter->rec(surface):nullptr;
    return rec&&rec->surface&&g_adapter->dev.primary()==rec->surface;
}
bool texture_source(uint32_t surface,winx86::dx::d3d::TextureSource& out) {
    auto* rec=g_adapter?g_adapter->rec(surface):nullptr;
    if(!rec||!rec->surface||!(rec->surface->caps&kTexture))return false;
    const auto& s=*rec->surface;
    winx86::dx::d3d::TextureSource result;
    result.width=s.width;result.height=s.height;result.pitch=s.pitch;result.generation=s.generation;
    result.pixels=g_adapter->mem.ptr(s.va,size_t(s.pitch)*s.height);
    if(!result.pixels)return false;
    if(s.bpp==8) { result.format=hw::d3d::TexFormat::PAL8;result.palette=g_adapter->dev.palette(); }
    else if(s.bpp==32)result.format=hw::d3d::TexFormat::ARGB8888;
    else if(s.alpha_mask==0x8000)result.format=hw::d3d::TexFormat::ARGB1555;
    else if(s.alpha_mask==0xF000)result.format=hw::d3d::TexFormat::ARGB4444;
    else result.format=hw::d3d::TexFormat::RGB565;
    result.has_key=s.has_key;result.key_low=s.key_low;result.key_high=s.key_high;
    out=result;return true;
}
bool install(Bridge& br,Cpu& cpu,const HostOps& ops) {
    if(!ops.alloc_pixels||(!ops.present&&!ops.present_sized)||!wx86_scratch_base()) return false;
    auto adapter=std::make_unique<Adapter>(br,cpu,ops);
    if(!adapter->setup()) return false;
    g_adapter=std::move(adapter);return true;
}
}
