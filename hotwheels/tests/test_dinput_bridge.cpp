#include "dinput/dinput.h"
#include "integration/dinput_bridge.h"
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_scratch.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort(); } } while (0)

struct CpuHost final : d2rt::Cpu {
    struct CallbackInfo { uint32_t entry=0, instance=0, context=0; std::array<uint8_t,580> bytes{}; };
    std::map<uint32_t,std::vector<uint8_t>> mem;
    uint32_t regs[16]{};
    d2rt::TrapFn trap;
    std::vector<CallbackInfo> callbacks;
    std::vector<uint8_t>* region(uint32_t a,uint32_t n,uint32_t& off) {
        auto it=mem.upper_bound(a); if(it==mem.begin()) return nullptr; --it;
        if(uint64_t(a)-it->first+n>it->second.size()) return nullptr;
        off=a-it->first; return &it->second;
    }
    bool map(uint32_t a,uint32_t n,const void* p,int) override {
        auto& v=mem[a];v.assign(n,0);if(p)std::memcpy(v.data(),p,n);return true;
    }
    bool protect(uint32_t,uint32_t,int) override { return true; }
    bool read(uint32_t a,void* p,uint32_t n) override {uint32_t o;auto* v=region(a,n,o);if(!v)return false;std::memcpy(p,v->data()+o,n);return true;}
    bool write(uint32_t a,const void* p,uint32_t n) override {uint32_t o;auto* v=region(a,n,o);if(!v)return false;std::memcpy(v->data()+o,p,n);return true;}
    void* hostptr(uint32_t a,uint32_t n) override {uint32_t o;auto* v=region(a,n,o);return v?v->data()+o:nullptr;}
    bool mapped(uint32_t a) override { return hostptr(a,1)!=nullptr; }
    uint32_t reg(int i) override { return regs[i]; }
    void set_reg(int i,uint32_t v) override { regs[i]=v; }
    void set_trap(uint32_t,uint32_t,d2rt::TrapFn f) override { trap=std::move(f); }
    bool run(uint32_t entry,const char**) override {
        CallbackInfo cb;cb.entry=entry;cb.instance=read_u32(reg(d2rt::R_ESP)+4);
        cb.context=read_u32(reg(d2rt::R_ESP)+8);
        if(!read(cb.instance,cb.bytes.data(),uint32_t(cb.bytes.size()))) return false;
        callbacks.push_back(cb);set_reg(d2rt::R_EAX,1);return true;
    }
    uint32_t fault_addr() const override { return 0; }
};

struct Source final : winx86::dx::dinput::InputSource {
    winx86::dx::dinput::InputSnapshot state;
    winx86::dx::dinput::InputSnapshot poll() override { return state; }
};

static uint32_t call(CpuHost& c,uint32_t fn,std::initializer_list<uint32_t> args) {
    const uint32_t esp=0x20001000;c.write_u32(esp,0x00401000);
    unsigned i=0;for(uint32_t a:args)c.write_u32(esp+4+4*i++,a);
    c.set_reg(d2rt::R_ESP,esp);c.set_reg(d2rt::R_EIP,fn);
    CHECK(c.trap && c.trap(c,fn));
    CHECK(c.reg(d2rt::R_ESP)==esp+4+4*args.size());
    return c.reg(d2rt::R_EAX);
}
static uint32_t method(CpuHost& c,uint32_t obj,unsigned slot) {
    return c.read_u32(c.read_u32(obj)+4*slot);
}

int main() {
    CpuHost c;CHECK(c.map(0x10000000,0x100000,nullptr,3));
    CHECK(c.map(0x20000000,0x10000,nullptr,3));
    wx86_scratch_init(0x10000000,0x100000);
    d2rt::Bridge br(&c);std::string error;CHECK(br.commit(error));
    Source src;src.state.keys[0x1e]=0x80;
    // BUG-046: el pad debe llegar al teclado DIK con el mapeo de Hot Wheels
    // (Cross -> Return) aunque nadie pase MapperConfig explícito.
    src.state.pad_count=1;src.state.pads[0].buttons=winx86::dx::dinput::kPadCross;
    CHECK(hw::integration::install_dinput(br,c,src));
    const uint32_t out=0x20000100, guid=0x20000200, format=0x20000300, state=0x20000400;
    auto create=br.shim_trap("DINPUT.dll","DirectInputCreateA");CHECK(create);
    CHECK(call(c,create,{0,0x700,out,0})==hw::dinput::DI_OK);
    const uint32_t di=c.read_u32(out);CHECK(di && c.read_u32(di));
    CHECK(call(c,create,{0,0x700,0xDEADBEEFu,0})==hw::dinput::DIERR_INVALIDPARAM);

    c.write(guid,&hw::dinput::kIidDirectInputA,sizeof(hw::dinput::Guid));
    CHECK(call(c,method(c,di,0),{di,guid,0xDEADBEEFu})==hw::dinput::DIERR_INVALIDPARAM);
    CHECK(call(c,method(c,di,0),{di,guid,out})==hw::dinput::DI_OK);
    CHECK(c.read_u32(out)==di);
    constexpr uint32_t enum_callback=0x00402000, enum_context=0x12345678;
    CHECK(call(c,method(c,di,4),{di,hw::dinput::DI8DEVCLASS_ALL,enum_callback,enum_context,0})==hw::dinput::DI_OK);
    CHECK(c.callbacks.size()==3);
    for(const auto& cb:c.callbacks) {
        uint32_t size=0,type=0;std::memcpy(&size,cb.bytes.data(),4);std::memcpy(&type,cb.bytes.data()+36,4);
        CHECK(cb.entry==enum_callback && cb.context==enum_context && size==580);
        CHECK((type==hw::dinput::DIDEVTYPE_KEYBOARD)||(type==hw::dinput::DIDEVTYPE_MOUSE)||
              (type==hw::dinput::DIDEVTYPE_JOYSTICK));
        CHECK(cb.bytes[40]!=0 && cb.bytes[300]!=0);
    }
    c.write(guid,&hw::dinput::kGuidSysKeyboard,sizeof(hw::dinput::Guid));
    CHECK(call(c,method(c,di,3),{di,guid,out,0})==hw::dinput::DI_OK);
    const uint32_t dev=c.read_u32(out);CHECK(dev && c.read_u32(dev));
    CHECK(method(c,dev,9)!=0 && method(c,dev,11)!=0 && method(c,dev,7)!=0);

    c.write_u32(format+12,hw::dinput::kKeyboardStateBytes);
    CHECK(call(c,method(c,dev,11),{dev,format})==hw::dinput::DI_OK);
    CHECK(call(c,method(c,dev,11),{dev,0xDEADBEEFu})==hw::dinput::DIERR_INVALIDPARAM);
    CHECK(call(c,method(c,dev,7),{dev})==hw::dinput::DI_OK);
    CHECK(call(c,method(c,dev,9),{dev,hw::dinput::kKeyboardStateBytes,0xDEADBEEFu})==hw::dinput::DIERR_INVALIDPARAM);
    CHECK(call(c,method(c,dev,9),{dev,hw::dinput::kKeyboardStateBytes,state})==hw::dinput::DI_OK);
    uint8_t key=0;CHECK(c.read(state+0x1e,&key,1) && key==0x80);
    uint8_t ret=0;CHECK(c.read(state+hw::dinput::dik::RETURN,&ret,1) && ret==0x80);
    CHECK(call(c,method(c,dev,2),{dev})==0);
    CHECK(call(c,method(c,dev,9),{dev,hw::dinput::kKeyboardStateBytes,state})==hw::dinput::DIERR_INVALIDPARAM);
    return 0;
}
