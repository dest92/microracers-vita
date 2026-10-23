#include "d3d/bridge.h"
#include "fps/interp60.h"
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_scratch.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include <condition_variable>
#include <mutex>

struct CpuHost final : d2rt::Cpu {
    std::map<uint32_t,std::vector<uint8_t>> maps;
    uint32_t regs[16]{};
    d2rt::TrapFn trap;
    std::vector<uint8_t>* region(uint32_t va,uint32_t n,uint32_t& off) {
        auto it=maps.upper_bound(va); if(it==maps.begin()) return nullptr; --it;
        if(uint64_t(va)-it->first+n>it->second.size()) return nullptr;
        off=va-it->first; return &it->second;
    }
    bool map(uint32_t va,uint32_t n) { maps[va].assign(n,0); return true; }
    bool protect(uint32_t,uint32_t,int) override { return true; }
    bool map(uint32_t va,uint32_t n,const void* p,int) override { auto& v=maps[va];v.assign(n,0);if(p)std::memcpy(v.data(),p,n);return true; }
    bool read(uint32_t va,void* p,uint32_t n) override {uint32_t o;auto* v=region(va,n,o);if(!v)return false;std::memcpy(p,v->data()+o,n);return true;}
    bool write(uint32_t va,const void* p,uint32_t n) override {uint32_t o;auto* v=region(va,n,o);if(!v)return false;std::memcpy(v->data()+o,p,n);return true;}
    void* hostptr(uint32_t va,uint32_t n) override {uint32_t o;auto* v=region(va,n,o);return v?v->data()+o:nullptr;}
    bool mapped(uint32_t va) override {return hostptr(va,1)!=nullptr;}
    uint32_t reg(int i) override {return regs[i];}
    void set_reg(int i,uint32_t v) override {regs[i]=v;}
    void set_trap(uint32_t,uint32_t,d2rt::TrapFn f) override {trap=std::move(f);}
    bool run(uint32_t,const char**) override {set_reg(d2rt::R_EAX,1);return true;}
    uint32_t fault_addr() const override {return 0;}
};

static uint32_t invoke(CpuHost& c,uint32_t fn,std::initializer_list<uint32_t> args) {
    uint32_t esp=0x30010000;c.write_u32(esp,0x00401000);unsigned i=0;
    for(uint32_t v:args)c.write_u32(esp+4+4*i++,v);
    c.set_reg(d2rt::R_ESP,esp);c.set_reg(d2rt::R_EIP,fn);assert(c.trap(c,fn));
    assert(c.reg(d2rt::R_ESP)==esp+4+4*args.size());return c.reg(d2rt::R_EAX);
}
static uint32_t method(CpuHost& c,uint32_t obj,unsigned slot) {return c.read_u32(c.read_u32(obj)+slot*4);}
static uint16_t p1555(unsigned r,unsigned g,unsigned b){return uint16_t(0x8000|(r<<10)|(g<<5)|b);}

int main() {
    CpuHost cpu;assert(cpu.map(0x10000000,0x100000,nullptr,3));assert(cpu.map(0x30000000,0x100000,nullptr,3));
    wx86_scratch_init(0x10000000,0x100000);d2rt::Bridge br(&cpu);std::string err;assert(br.commit(err));
    uint16_t texels[4]={p1555(31,0,0),p1555(0,31,0),p1555(0,0,31),p1555(31,31,31)};
    hw::d3d::TextureSource source;source.width=source.height=2;source.pitch=4;source.generation=1;
    source.format=hw::d3d::TexFormat::ARGB1555;source.pixels=reinterpret_cast<const uint8_t*>(texels);
    hw::d3d::RefRaster raster;hw::d3d::ComHostOps ops;
    ops.render_target=[](uint32_t s,uint32_t& w,uint32_t& h){w=2;h=2;return s==0x1234;};
    ops.texture_source=[&](uint32_t s,hw::d3d::TextureSource& out){if(s!=0x5678)return false;out=source;return true;};
    ops.is_primary=[](uint32_t s){return s==0x9999;};
    std::vector<std::shared_ptr<const hw::d3d::Ring>> closed_frames;
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    unsigned completed_frames = 0;
    bool capture_interp = true;
    unsigned producer_pumps = 0, logical_guest_frames = 0;
    std::mutex consumer_gate_mutex;
    std::condition_variable consumer_gate_cv;
    bool stall_consumer = false, consumer_stalled = false, release_consumer = false;
    ops.producer_frame = [&] { ++producer_pumps; ++logical_guest_frames; };
    ops.frame_presented = [&](uint32_t) {
        std::lock_guard<std::mutex> lock(frame_mutex);
        ++completed_frames;
        frame_cv.notify_all();
    };
    auto wait_frames = [&](unsigned target) {
        std::unique_lock<std::mutex> lock(frame_mutex);
        assert(frame_cv.wait_for(lock, std::chrono::seconds(2), [&] { return completed_frames >= target; }));
    };
    ops.frame_closed=[&](std::shared_ptr<const hw::d3d::Ring> frame){
        {
            std::unique_lock<std::mutex> lock(consumer_gate_mutex);
            if (stall_consumer) {
                consumer_stalled = true;
                consumer_gate_cv.notify_all();
                consumer_gate_cv.wait(lock, [&] { return release_consumer; });
            }
        }
        closed_frames.push_back(std::make_shared<const hw::d3d::Ring>(*frame));
    };
    bool consume_frame=false;
    uint32_t offered_frames=0;
    hw::fps::Interp60 interpolator;
    ops.present_frame=[&](std::shared_ptr<const hw::d3d::Ring> frame){
        assert(frame && !frame->words().empty());
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            if (!capture_interp) return false;
        }
        assert(interpolator.capture(*frame, 100000 + uint64_t(offered_frames) * 25000, {0}));
        ++offered_frames;
        return consume_frame;
    };
    hw::d3d::ComBridge com(br,cpu,raster,ops);assert(com.install());
    uint32_t d3d_external=com.acquire_d3d3_object();assert(d3d_external==com.d3d3_object());
    assert(invoke(cpu,method(cpu,d3d_external,2),{d3d_external})==1);
    uint32_t out=0x30000000,iid=out+0x40,view=out+0x80,vpdesc=out+0x100,verts=out+0x200;
    uint8_t hal_guid[16]={0xE0,0x3D,0xE6,0x84,0xAA,0x46,0xCF,0x11,0x81,0x6F,0,0,0xC0,0x20,0x15,0x6E};
    cpu.write(iid,hal_guid,16);
    uint8_t wrong_guid[16]{};cpu.write(iid,wrong_guid,16);
    assert(invoke(cpu,method(cpu,d3d_external,8),{d3d_external,iid,0x1234,out,0})==0x80070057u);
    cpu.write(iid,hal_guid,16);
    assert(invoke(cpu,method(cpu,d3d_external,8),{d3d_external,iid,0x1234,out,0})==0);
    uint32_t dev=cpu.read_u32(out);assert(dev);
    assert(invoke(cpu,method(cpu,dev,13),{dev,out})==0x88760307u);
    assert(cpu.read_u32(out)==0);
    assert(invoke(cpu,method(cpu,com.d3d3_object(),6),{com.d3d3_object(),view,0})==0);
    uint32_t vp=cpu.read_u32(view);assert(vp);
    assert(invoke(cpu,method(cpu,dev,5),{dev,vp})==0);
    cpu.write_u32(vpdesc,44);cpu.write_u32(vpdesc+12,2);cpu.write_u32(vpdesc+16,2);
    assert(invoke(cpu,method(cpu,vp,17),{vp,vpdesc})==0);
    assert(invoke(cpu,method(cpu,dev,12),{dev,vp})==0);
    assert(invoke(cpu,method(cpu,vp,12),{vp,0,0,1})==0);
    uint32_t tex=com.create_texture(0x5678);assert(tex);
    assert(invoke(cpu,method(cpu,dev,38),{dev,0,tex})==0);
    assert(invoke(cpu,method(cpu,dev,9),{dev})==0);
    float quad[4][6]={{-.5f,-.5f,0,1,0,0},{1.5f,-.5f,0,1,1,0},{-.5f,1.5f,0,1,0,1},{1.5f,1.5f,0,1,1,1}};
    cpu.write(verts,quad,sizeof(quad));
    assert(invoke(cpu,method(cpu,dev,28),{dev,5,0x104,verts,4,0x10})==0);
    assert(invoke(cpu,method(cpu,dev,10),{dev})==0);
    assert(com.flip(0x9999));
    assert(producer_pumps == 1 && logical_guest_frames == 1);
    wait_frames(1);
    assert(raster.pixel(0,0)==0xFFFF0000u&&raster.pixel(1,0)==0xFF00FF00u);
    assert(raster.pixel(0,1)==0xFF0000FFu&&raster.pixel(1,1)==0xFFFFFFFFu);
    assert(closed_frames.size()==1 && offered_frames==1 && raster.presents==1);
    const auto first_words=closed_frames[0]->words();
    bool uploaded=false,presented=false;
    closed_frames[0]->for_each([&](uint16_t op,const uint32_t* payload,uint32_t n){
        if(op==uint16_t(hw::d3d::Op::TexUpload)){
            const uint32_t p0=uint32_t(texels[0])|(uint32_t(texels[1])<<16);
            const uint32_t p1=uint32_t(texels[2])|(uint32_t(texels[3])<<16);
            uploaded=n>=10&&payload[1]==2&&payload[2]==2&&payload[8]==p0&&payload[9]==p1;
        }
        if(op==uint16_t(hw::d3d::Op::Present))presented=true;
    });
    assert(uploaded&&presented);
    assert(invoke(cpu,method(cpu,dev,9),{dev})==0);
    for (auto& v : quad) v[0] += 0.5f;
    cpu.write(verts,quad,sizeof(quad));
    assert(invoke(cpu,method(cpu,dev,38),{dev,0,tex})==0);
    assert(invoke(cpu,method(cpu,dev,28),{dev,5,0x104,verts,4,0x10})==0);
    assert(invoke(cpu,method(cpu,dev,10),{dev})==0);
    consume_frame=true;
    assert(com.flip(0x9999));
    assert(producer_pumps == 2 && logical_guest_frames == 2);
    wait_frames(2);
    assert(closed_frames.size()==2 && offered_frames==2 && raster.presents==1);
    assert(closed_frames[0]->words()==first_words);
    hw::d3d::Ring interpolated;
    hw::fps::InterpStats interp_stats;
    assert(interpolator.tick(141667, interpolated, &interp_stats));
    assert(interp_stats.interpolated==1 && interp_stats.fallback==0);
    float interpolated_x=0.f;
    interpolated.for_each([&](uint16_t op,const uint32_t* payload,uint32_t n){
        if(op==uint16_t(hw::d3d::Op::Draw)&&n>=8)
            std::memcpy(&interpolated_x,&payload[2],sizeof(interpolated_x));
    });
    assert(interpolated_x>-0.5f && interpolated_x<0.f);
    assert(!com.flip(0x9998));
    assert(producer_pumps == 2 && logical_guest_frames == 2);

    // Pause the consumer with a ring in flight. Later producer flips must each
    // pump audio and advance the guest clock once even while READY rings drop.
    unsigned completed_before = 0;
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        completed_before = completed_frames;
        capture_interp = false;
    }
    {
        std::lock_guard<std::mutex> lock(consumer_gate_mutex);
        stall_consumer = true;
    }
    assert(com.flip(0x9999));
    {
        std::unique_lock<std::mutex> lock(consumer_gate_mutex);
        assert(consumer_gate_cv.wait_for(lock, std::chrono::seconds(2), [&] { return consumer_stalled; }));
    }
    constexpr unsigned submitted_while_stalled = 8;
    for (unsigned i = 0; i < submitted_while_stalled; ++i)
        assert(com.flip(0x9999));
    assert(producer_pumps == 3 + submitted_while_stalled);
    assert(logical_guest_frames == producer_pumps);
    {
        std::lock_guard<std::mutex> lock(consumer_gate_mutex);
        release_consumer = true;
        consumer_gate_cv.notify_all();
    }
    wait_frames(completed_before + 2); // in-flight + newest READY; middle READY rings dropped
    assert(com.flip(0x9999)); // after drain, this sentinel must also tick once
    assert(producer_pumps == 4 + submitted_while_stalled);
    assert(logical_guest_frames == producer_pumps);
    wait_frames(completed_before + 3);
    unsigned completed_after = 0;
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        assert(completed_frames == completed_before + 3);
        completed_after = completed_frames;
    }
    assert(completed_after < producer_pumps);
}
