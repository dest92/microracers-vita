#include "shims/guest_call.h"
#include "integration/dinput_bridge.h"
#include "integration/scripted_input.h"
#include "dinput/dinput.h"
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_scratch.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hw::integration {
namespace {
using d2rt::Bridge; using d2rt::Cpu; using d2rt::Shim;
using namespace hw::dinput;
struct Rec { Device* dev; uint32_t refs; };
struct Adapter {
    Bridge& br; Cpu& cpu; RacePilotInput pilot; WaypointInput waypoints; DirectInput di;
    uint32_t di_vt=0, dev_vt=0, di_obj=0;
    std::map<uint32_t,Rec> objs;
    Adapter(Bridge& b,Cpu& c,InputSource& s,const MapperConfig& cfg)
        : br(b), cpu(c),
          pilot(s, [&c](RacePilotState& state) {
              constexpr uint32_t kRaceStateFn = 0x00475108;
              constexpr uint32_t kPlayers = 0x00475460;
              constexpr uint32_t kRaceLoop = 0x00411d70;
              uint32_t active_fn = 0, players = 0;
              if (!c.read(kRaceStateFn, &active_fn, sizeof active_fn)) return false;
              state.race_active = active_fn == kRaceLoop;
              if (!state.race_active) return true;
              if (!c.read(kPlayers, &players, sizeof players) || !players ||
                  players > UINT32_MAX - 0x1c) return false;
              int16_t laps = 0;
              if (!c.read(players + 0x1c, &laps, sizeof laps)) return false;
              state.laps = laps;
              return true;
          }, [] {
              const char* value = std::getenv("HW_RACE_PILOT");
              return value && value[0] && value[0] != '0';
          }()),
          waypoints(pilot, [&c](RacePosition& state) {
              constexpr uint32_t kRaceStateFn = 0x00475108;
              constexpr uint32_t kPlayers = 0x00475460;
              constexpr uint32_t kRaceLoop = 0x00411d70;
              uint32_t active_fn = 0, players = 0, vehicle = 0, physics = 0;
              if (!c.read(kRaceStateFn, &active_fn, sizeof active_fn)) return false;
              state.race_active = active_fn == kRaceLoop;
              if (!state.race_active) return true;
              if (!c.read(kPlayers, &players, sizeof players) || !players ||
                  players > UINT32_MAX - 0x1c0) return false;
              if (!c.read(players + 0x1c0, &vehicle, sizeof vehicle) || !vehicle ||
                  vehicle > UINT32_MAX - 0x1c) return false;
              if (!c.read(vehicle + 0x1c, &physics, sizeof physics) || !physics ||
                  physics > UINT32_MAX - 0x24) return false;
              return c.read(physics + 0x1c, &state.x_q12, sizeof state.x_q12) &&
                     c.read(physics + 0x24, &state.z_q12, sizeof state.z_q12);
          }, [] {
              const char* value = std::getenv("HW_RACE_RECORD");
              return value && value[0] && value[0] != '0';
          }(), std::getenv("HW_RACE_WAYPOINTS")),
          di(waypoints, cfg) {}
    bool valid(uint32_t p,uint32_t n) { return p && n && cpu.hostptr(p,n); }
    uint32_t rd(uint32_t p) { return cpu.read_u32(p); }
    void wr(uint32_t p,uint32_t v) { cpu.write_u32(p,v); }
    bool guid(uint32_t p,Guid& g) { return valid(p,16) && cpu.read(p,&g,16); }
    uint32_t make_obj(Device* d) {
        uint32_t a=wx86_scratch_alloc(8); if(!a) return 0;
        wr(a,d?dev_vt:di_vt); wr(a+4,0x44494e50u);
        objs[a]={d,1}; return a;
    }
    uint32_t query(uint32_t self,uint32_t iid,uint32_t out) {
        if(!valid(out,4)) return DIERR_INVALIDPARAM;
        wr(out,0);
        auto it=objs.find(self); Guid g{};
        if(it==objs.end()||!guid(iid,g)) return DIERR_INVALIDPARAM;
        if(it->second.dev ? !it->second.dev->query_interface(g) : !di.query_interface(g))
            return DIERR_NOINTERFACE;
        ++it->second.refs; wr(out,self); return DI_OK;
    }
    uint32_t call(bool device,unsigned slot,Cpu& c) {
        uint32_t self=c.arg(0);
        if(slot==0) return query(self,c.arg(1),c.arg(2));
        auto it=objs.find(self);
        if(it==objs.end()||bool(it->second.dev)!=device) return DIERR_INVALIDPARAM;
        if(slot==1) return ++it->second.refs;
        if(slot==2) {
            if(!it->second.refs) return 0;
            if(--it->second.refs) return it->second.refs;
            Device* dead=it->second.dev;
            objs.erase(it);
            if(dead) di.release_device(dead);
            return 0;
        }
        if(!device) return call_di(slot,c);
        return call_dev(*it->second.dev,slot,c);
    }
    uint32_t call_di(unsigned slot,Cpu& c) {
        if(slot==3) {
            Guid g{}; uint32_t out=c.arg(2);
            if(!valid(out,4)||!guid(c.arg(1),g)||c.arg(3)) return DIERR_INVALIDPARAM;
            wr(out,0); Device* d=di.create_device(g);
            if(!d) return DIERR_INVALIDPARAM;
            uint32_t obj=make_obj(d);if(!obj) { di.release_device(d); return DIERR_UNSUPPORTED; }
            wr(out,obj);return DI_OK;
        }
        if(slot==4) {
            uint32_t fn=c.arg(2),context=c.arg(3);
            if(!fn) return DIERR_INVALIDPARAM;
            di.enumerate(c.arg(1),[&](const Guid& g,const char* name) {
                uint32_t p=wx86_scratch_alloc(580);if(!p) return false;
                std::array<uint8_t,580> info{};
                uint32_t sz=580;std::memcpy(info.data(),&sz,4);
                std::memcpy(info.data()+4,&g,16);
                std::memcpy(info.data()+20,&g,16);
                uint32_t type=(g==kGuidSysKeyboard?DIDEVTYPE_KEYBOARD:g==kGuidSysMouse?DIDEVTYPE_MOUSE:DIDEVTYPE_JOYSTICK);
                std::memcpy(info.data()+36,&type,4);
                std::strncpy(reinterpret_cast<char*>(info.data()+40),name,259);
                std::strncpy(reinterpret_cast<char*>(info.data()+300),name,259);
                if(!cpu.write(p,info.data(),uint32_t(info.size()))) return false;
                std::array<uint32_t,10> regs{};
                for(int i=0;i<10;++i) regs[i]=cpu.reg(d2rt::Reg(i));
                uint32_t result=0; const char* fault=nullptr;
                bool ok=hw::guest_call(br,fn,{p,context},result,&fault);
                for(int i=0;i<10;++i) cpu.set_reg(d2rt::Reg(i),regs[i]);
                return ok && result!=0;
            });
            return DI_OK;
        }
        if(slot==5) { Guid g{};return guid(c.arg(1),g)&&
            (g==kGuidSysKeyboard||g==kGuidSysMouse||g==kGuidJoystick)?DI_OK:DIERR_INVALIDPARAM; }
        if(slot==6||slot==7) return DI_OK;
        return DIERR_UNSUPPORTED;
    }
    uint32_t call_dev(Device& d,unsigned slot,Cpu& c) {
        switch(slot) {
        case 3: {
            uint32_t p=c.arg(1); if(!valid(p,44)||rd(p)<44) return DIERR_INVALIDPARAM;
            uint32_t axes=0,buttons=0,povs=0;d.get_capabilities(axes,buttons,povs);
            wr(p+4,d.type()==DeviceType::Keyboard?DIDEVTYPE_KEYBOARD:d.type()==DeviceType::Mouse?DIDEVTYPE_MOUSE:DIDEVTYPE_JOYSTICK);
            wr(p+12,axes);wr(p+16,buttons);wr(p+20,povs);return DI_OK;
        }
        case 5: case 6: {
            uint32_t prop=c.arg(1),p=c.arg(2);
            if(!valid(p,16)) return DIERR_INVALIDPARAM;
            uint32_t len=rd(p);if(len<16||len>256||!valid(p,len)) return DIERR_INVALIDPARAM;
            void* ptr=cpu.hostptr(p,len);
            return slot==5?d.get_property(prop,ptr):d.set_property(prop,ptr);
        }
        case 7:return d.acquire();
        case 8:return d.unacquire();
        case 9: {
            uint32_t n=c.arg(1),p=c.arg(2);
            if(!n||n>1024||!valid(p,n)) return DIERR_INVALIDPARAM;
            // El juego usa DIMOUSESTATE (16 B); el core portable también
            // produce DIMOUSESTATE2 (20 B). Copiamos el prefijo ABI común.
            if(n==16&&d.type()==DeviceType::Mouse) {
                std::array<uint8_t,20> state{};
                uint32_t hr=d.get_device_state(20,state.data());
                if(hr==DI_OK) cpu.write(p,state.data(),16);
                return hr;
            }
            return d.get_device_state(n,cpu.hostptr(p,n));
        }
        case 10:return DIERR_NOTBUFFERED;
        case 11: {
            uint32_t p=c.arg(1);if(!valid(p,16)) return DIERR_INVALIDPARAM;
            uint32_t size=rd(p+12);
            DataFormat f=size==kKeyboardStateBytes?DataFormat::Keyboard:
                (size==16||size==kMouseStateBytes)?DataFormat::Mouse:
                size==kJoystickStateBytes?DataFormat::Joystick:DataFormat::None;
            return d.set_data_format(f);
        }
        case 12:return DI_OK;
        case 13:return d.set_cooperative_level(c.arg(2));
        case 15: {
            uint32_t p=c.arg(1);if(!valid(p,40)||rd(p)<40) return DIERR_INVALIDPARAM;
            Guid g{};std::string name;d.get_device_info(g,name);
            cpu.write(p+4,&g,16);cpu.write(p+20,&g,16);
            if(rd(p)>=560&&valid(p,560)) {
                std::array<char,260> s{};std::strncpy(s.data(),name.c_str(),259);
                cpu.write(p+40,s.data(),260);cpu.write(p+300,s.data(),260);
            }
            return DI_OK;
        }
        case 16:case 17:return DI_OK;
        case 25:return d.poll();
        default:return DIERR_UNSUPPORTED;
        }
    }
    bool setup() {
        // Aridad sin `this`, en orden de dinput.h i686-w64-mingw32.
        const std::vector<uint8_t> di_args={2,0,0,3,4,1,2,2};
        const std::vector<uint8_t> dev_args={2,0,0,1,3,2,2,0,0,2,4,1,1,2,3,1,2,3,
                                              4,3,2,1,1,3,1,0,4};
        auto vt=[&](bool dev,const std::vector<uint8_t>& arities) {
            uint32_t base=wx86_scratch_alloc(uint32_t(arities.size()*4));if(!base) return uint32_t(0);
            for(size_t i=0;i<arities.size();++i) {
                std::string n=std::string(dev?"DEV_":"DI_")+std::to_string(i);
                Shim s;s.tag="HWDINPUTVT!"+n;s.argc=1+arities[i];s.stdcall_cleanup=true;
                s.fn=[this,dev,i](Cpu& c){return call(dev,unsigned(i),c);};
                br.register_shim("HWDINPUTVT.dll",n,s);
                wr(base+uint32_t(i*4),br.shim_trap("HWDINPUTVT.dll",n));
            }
            return base;
        };
        di_vt=vt(false,di_args);dev_vt=vt(true,dev_args);
        if(!di_vt||!dev_vt) return false;
        di_obj=make_obj(nullptr);if(!di_obj) return false;
        Shim s;s.tag="DINPUT.dll!DirectInputCreateA";s.argc=4;s.stdcall_cleanup=true;
        s.fn=[this](Cpu& c) {
            uint32_t out=c.arg(2);if(!valid(out,4)||c.arg(3)) return DIERR_INVALIDPARAM;
            wr(out,di_obj);++objs[di_obj].refs;return DI_OK;
        };
        br.register_shim("DINPUT.dll","DirectInputCreateA",s);
        return true;
    }
};
std::unique_ptr<Adapter> g_adapter;
}
bool install_dinput(Bridge& br,Cpu& cpu,InputSource& source,const MapperConfig& cfg) {
    if(!wx86_scratch_base()) return false;
    auto a=std::make_unique<Adapter>(br,cpu,source,cfg);
    if(!a->setup()) return false;
    g_adapter=std::move(a);return true;
}
}
