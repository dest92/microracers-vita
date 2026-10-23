#include "d3d/bridge.h"
#include "d3d/consumer_phase.h"
#include "d3d/frame_queue.h"
#include "shims/guest_call.h"
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "runtime/guest_scratch.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include <thread>

namespace hw::d3d {
namespace {
using d2rt::Bridge; using d2rt::Cpu; using d2rt::Shim;
constexpr uint32_t kInvalid=0x80070057u, kNoInterface=0x80004002u,
    kNotImpl=0x80004001u, kNoViewport=0x88760307u;
constexpr uint32_t kGuidD3D3=0xBB223240u, kGuidHAL=0x84E63DE0u, kGuidDevice3=0xB0AB3B60u,
    kGuidViewport3=0xB0AB3B61u, kGuidTexture2=0x93281502u;
enum class Kind : uint8_t { D3D3, Device3, Viewport3, Texture2 };
struct Rec { Kind kind; uint32_t refs=1, surface=0, vp_id=0; Rect rect{}; };

bool guid(Cpu& c,uint32_t p,uint32_t dword) {
    if(!p||!c.hostptr(p,16)) return false;
    static const std::map<uint32_t,std::array<uint8_t,16>> values={
        {0,{0,0,0,0,0,0,0,0,0xC0,0,0,0,0,0,0,0x46}},
        {kGuidD3D3,{0x40,0x32,0x22,0xBB,0x2B,0xE7,0xD0,0x11,0xA9,0xB4,0,0xAA,0,0xC0,0x99,0x3E}},
        {kGuidHAL,{0xE0,0x3D,0xE6,0x84,0xAA,0x46,0xCF,0x11,0x81,0x6F,0,0,0xC0,0x20,0x15,0x6E}},
        {kGuidDevice3,{0x60,0x3B,0xAB,0xB0,0xD7,0x33,0xD1,0x11,0xA9,0x81,0,0xC0,0x4F,0xD7,0xB1,0x74}},
        {kGuidViewport3,{0x61,0x3B,0xAB,0xB0,0xD7,0x33,0xD1,0x11,0xA9,0x81,0,0xC0,0x4F,0xD7,0xB1,0x74}},
        {kGuidTexture2,{0x02,0x15,0x28,0x93,0xF8,0x8C,0xD0,0x11,0x89,0xAB,0,0xA0,0xC9,0x05,0x41,0x29}}
    };
    auto it=values.find(dword); if(it==values.end()) return false;
    std::array<uint8_t,16> got{}; c.read(p,got.data(),got.size()); return got==it->second;
}
}

struct ComBridge::Impl {
    Bridge& br; Cpu& cpu; Backend& backend; ComHostOps ops;
    std::map<uint32_t,Rec> objects;
    std::array<uint32_t,4> vt{};
    uint32_t d3d=0, device_obj=0, target=0, current_vp=0;
    FrameQueue frames;
    FrameQueue::Writer writer;
    std::unique_ptr<Device> device; Replayer replay;
    std::thread consumer;
    uint32_t completed_frames = 0;
    bool sync_baseline = false;
    using Clock = std::chrono::steady_clock;
    ConsumerPhases phases;   // HW-073: ocupado vs esperando, lo escribe sólo el consumer
    static uint64_t us_since(Clock::time_point t) {
        return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t).count());
    }
    Clock::time_point metrics_start = Clock::now(), metrics_report = metrics_start;
    uint64_t reported_completed = 0, reported_submitted = 0, reported_uploads = 0,
             reported_backend_uploads = 0, reported_dropped_resource_ops = 0,
             reported_carried_resource_ops = 0, last_second = 0,
             reported_draws_guest = 0, reported_draws_emitted = 0;   // SPEC-062
    std::array<uint32_t, 10> second_frames{};
    Impl(Bridge& b,Cpu& c,Backend& be,ComHostOps o):br(b),cpu(c),backend(be),ops(std::move(o)),replay(be) {
        const char* sync = std::getenv("HW_D3D_SYNC_BASELINE");
        sync_baseline = sync && sync[0] == '1';
        if (sync_baseline) {
            if (ops.log) ops.log("D3D synchronous baseline enabled");
        } else consumer = std::thread([this] { consume(); });
    }
    ~Impl() { device.reset(); writer.abandon(); frames.close(true); if (consumer.joinable()) consumer.join(); }
    void consume_frame(FrameQueue::Lease lease) {
        const Ring* ring = lease.ring();
        auto frame = std::shared_ptr<const Ring>(ring,
            [hold = std::move(lease)](const Ring*) mutable { hold.finish(); });
        uint32_t frame_uploads = 0;
        frame->for_each([&](uint16_t op, const uint32_t*, uint32_t) {
            if (op == uint16_t(Op::TexUpload)) ++frame_uploads;
            else count_op(op, phases);   // draws y cambios de estado (HW-073)
        });
        if (ops.frame_closed) ops.frame_closed(frame);
        bool consumed = false;
        if (ops.present_frame) {
            const auto t = Clock::now();
            consumed = ops.present_frame(frame);
            phases.replay_us += us_since(t);
        }
        if (!consumed) {
            if (ops.lock_backend) ops.lock_backend();
            const auto t = Clock::now();
            replay.play(*frame);
            phases.replay_us += us_since(t);
            if (ops.unlock_backend) ops.unlock_backend();
        }
        if (ops.frame_presented) ops.frame_presented(++completed_frames);
        reported_uploads += frame_uploads;
        frame.reset();
        const auto now = Clock::now();
        const uint64_t sec = uint64_t(std::chrono::duration_cast<std::chrono::seconds>(now - metrics_start).count());
        if (sec > last_second) {
            const uint64_t gap = sec - last_second;
            if (gap >= second_frames.size()) second_frames.fill(0);
            else for (uint64_t i = 1; i <= gap; ++i) second_frames[(last_second + i) % second_frames.size()] = 0;
            last_second = sec;
        }
        ++second_frames[sec % second_frames.size()];
        if (now - metrics_report >= std::chrono::seconds(10) && ops.log) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - metrics_report).count();
            const auto delta_completed = uint64_t(completed_frames) - reported_completed;
            const auto stats = frames.stats();
            const auto delta_submitted = stats.submitted - reported_submitted;
            const auto delta_dropped_resource_ops = stats.dropped_resource_ops - reported_dropped_resource_ops;
            const auto delta_carried_resource_ops = stats.carried_resource_ops - reported_carried_resource_ops;
            const auto delta_uploads = reported_uploads;
            if (ops.lock_backend) ops.lock_backend();
            const uint64_t backend_uploads = backend.texture_upload_count();
            if (ops.unlock_backend) ops.unlock_backend();
            const uint64_t delta_backend_uploads = backend_uploads - reported_backend_uploads;
            uint32_t minimum = UINT32_MAX;
            const uint64_t complete_seconds = std::min<uint64_t>(10, sec);
            for (uint64_t i = 0; i < complete_seconds; ++i)
                minimum = std::min(minimum, second_frames[(sec - 1 - i) % second_frames.size()]);
            char fields[320];
            phases.format(fields, sizeof(fields), delta_completed, uint64_t(elapsed));
            char message[768];
            std::snprintf(message, sizeof(message),
                "fps-window mode=%s guest_d3d_hz_milli=%llu consumer_hz_milli=%llu min_consumer_1s_hz_milli=%u completed=%llu submitted=%llu dropped=%llu tex_uploads_per_frame_milli=%llu tex_backend_uploads_per_frame_milli=%llu resource_ops_dropped=%llu resource_ops_carried=%llu draws_guest_per_frame_milli=%llu draws_emitted_per_frame_milli=%llu %s",
                sync_baseline ? "original" : "pipeline",
                (unsigned long long)(elapsed ? delta_submitted * 1000000000ull / uint64_t(elapsed) : 0),
                (unsigned long long)(elapsed ? delta_completed * 1000000000ull / uint64_t(elapsed) : 0),
                minimum * 1000u, (unsigned long long)stats.completed,
                (unsigned long long)stats.submitted, (unsigned long long)stats.dropped,
                (unsigned long long)(delta_completed ? delta_uploads * 1000ull / delta_completed : 0),
                (unsigned long long)(delta_completed ? delta_backend_uploads * 1000ull / delta_completed : 0),
                (unsigned long long)delta_dropped_resource_ops,
                (unsigned long long)delta_carried_resource_ops,
                (unsigned long long)(delta_completed ? (replay.draws_guest() - reported_draws_guest) * 1000ull / delta_completed : 0),
                (unsigned long long)(delta_completed ? (replay.draws_emitted() - reported_draws_emitted) * 1000ull / delta_completed : 0),
                fields);
            ops.log(message);
            phases.reset();
            metrics_report = now; reported_completed = completed_frames;
            reported_submitted = stats.submitted; reported_uploads = 0;
            reported_backend_uploads = backend_uploads;
            reported_dropped_resource_ops = stats.dropped_resource_ops;
            reported_carried_resource_ops = stats.carried_resource_ops;
            reported_draws_guest = replay.draws_guest(); reported_draws_emitted = replay.draws_emitted();
        }
    }
    void consume() {
        if (ops.consumer_start) ops.consumer_start();
        for (;;) {
            const auto t_wait = Clock::now();
            auto lease = frames.wait_take();
            phases.wait_us += us_since(t_wait);
            if (!lease) break;
            const auto t_busy = Clock::now();
            consume_frame(std::move(lease));
            phases.busy_us += us_since(t_busy);
        }
    }
    uint32_t r(uint32_t p){return cpu.read_u32(p);} void w(uint32_t p,uint32_t v){cpu.write_u32(p,v);}
    bool mapped(uint32_t p,uint32_t n){return p&&cpu.hostptr(p,n);}
    Rec* rec(uint32_t p){auto i=objects.find(p);return i==objects.end()?nullptr:&i->second;}
    uint32_t object(Kind k){uint32_t p=wx86_scratch_alloc(8);if(!p)return 0;w(p,vt[size_t(k)]);w(p+4,0x33443348u);objects.emplace(p,Rec{k});return p;}
    bool texture_lookup(uint32_t h,TextureSource& out){auto* t=rec(h);return t&&t->kind==Kind::Texture2&&ops.texture_source&&ops.texture_source(t->surface,out);}
    uint32_t callback(uint32_t fn,const std::vector<uint32_t>& args){
        if(!fn) return 0;
        std::array<uint32_t,10> saved{};
        for(int i=0;i<10;++i) saved[i]=cpu.reg(d2rt::Reg(i));
        uint32_t result=0;const char* fault=nullptr;bool ok=hw::guest_call(br,fn,args,result,&fault);
        for(int i=0;i<10;++i) cpu.set_reg(d2rt::Reg(i),saved[i]);
        return ok?result:0;
    }
    void register_vtable(Kind k,const std::vector<uint8_t>& argc){
        uint32_t n=wx86_scratch_alloc(uint32_t(argc.size()*4));vt[size_t(k)]=n;
        for(size_t i=0;i<argc.size();++i){
            std::string name="D3D_"+std::to_string(size_t(k))+"_"+std::to_string(i);
            Shim s;s.argc=1+argc[i];s.stdcall_cleanup=true;
            s.fn=[this,k,i](Cpu& c){return call(k,unsigned(i),c);};
            br.register_shim("HWD3DVT.dll",name,s);w(n+uint32_t(i*4),br.shim_trap("HWD3DVT.dll",name));
        }
    }
    uint32_t qi(uint32_t self,uint32_t iid,uint32_t out){
        if(!mapped(out,4)) return kInvalid;
        w(out,0);auto* p=rec(self);if(!p)return kInvalid;
        uint32_t g=0;if(p->kind==Kind::D3D3)g=kGuidD3D3;else if(p->kind==Kind::Device3)g=kGuidDevice3;
        else if(p->kind==Kind::Viewport3)g=kGuidViewport3;else if(p->kind==Kind::Texture2)g=kGuidTexture2;
        if(!guid(cpu,iid,g)&&!guid(cpu,iid,0))return kNoInterface;
        ++p->refs;w(out,self);return kOk;
    }
    uint32_t call(Kind k,unsigned s,Cpu& c){
        uint32_t self=c.arg(0);auto* p=rec(self);if(!p||p->kind!=k)return kInvalid;
        if(s==0)return qi(self,c.arg(1),c.arg(2));
        if(s==1)return ++p->refs;
        if(s==2){uint32_t n=p->refs?p->refs-1:0;p->refs=n;if(!n){if(k==Kind::Texture2&&device)device->forget_texture(self);if(k==Kind::Device3){device.reset();device_obj=target=current_vp=0;writer.abandon();}objects.erase(self);}return n;}
        if(k==Kind::D3D3)return d3d_call(s,c);
        if(k==Kind::Device3)return device_call(s,c);
        if(k==Kind::Viewport3)return viewport_call(s,c,*p);
        return kNotImpl;
    }
    uint32_t d3d_call(unsigned s,Cpu& c){
        if(s==6){if(!mapped(c.arg(1),4)||c.arg(2))return kInvalid;uint32_t v=object(Kind::Viewport3);if(!v)return kInvalid;w(c.arg(1),v);return kOk;}
        if(s==8){
            uint32_t iid=c.arg(1), surf=c.arg(2), out=c.arg(3), wpx=0,hpx=0;
            if(!mapped(out,4)) return kInvalid;
            w(out,0);
            if(!guid(cpu,iid,kGuidHAL))return kInvalid;
            if(c.arg(4))return kInvalid;
            const bool target_ok=ops.render_target&&ops.render_target(surf,wpx,hpx)&&wpx&&hpx;
            if(ops.log){char msg[160];std::snprintf(msg,sizeof(msg),"D3D CreateDevice surface=%08x target=%u size=%ux%u",surf,unsigned(target_ok),wpx,hpx);ops.log(msg);}
            if(ops.image_log){char msg[96];std::snprintf(msg,sizeof(msg),"D3D target effective=%ux%u valid=%u",wpx,hpx,unsigned(target_ok));ops.image_log(msg);}
            if(!target_ok)return kInvalid;
            writer=frames.begin_write();if(!writer)return kInvalid;device=std::make_unique<Device>(*writer.ring(),wpx,hpx,[this](uint32_t h,TextureSource& t){return texture_lookup(h,t);});
            device->log=ops.log;target=surf;device_obj=object(Kind::Device3);if(!device_obj){device.reset();return kInvalid;}
            w(out,device_obj);
            if(ops.log){char msg[96];std::snprintf(msg,sizeof(msg),"D3D CreateDevice return=00000000 device=%08x",device_obj);ops.log(msg);}
            return kOk;
        }
        if(s==3){
            // La enumeración declara un HAL; CreateDevice valida su GUID.
            uint32_t cb=c.arg(1);if(!cb)return kInvalid;
            // D3DDEVICEDESC (DX6) is 252 bytes with 4-byte ABI packing.
            uint32_t desc=wx86_scratch_alloc(252),guidva=wx86_scratch_alloc(16);
            uint32_t desc2=wx86_scratch_alloc(252);if(!desc||!guidva||!desc2)return kInvalid;
            std::array<uint8_t,252> zero{};cpu.write(desc,zero.data(),zero.size());cpu.write(desc2,zero.data(),zero.size());
            static const uint8_t hal[16]={0xE0,0x3D,0xE6,0x84,0xAA,0x46,0xCF,0x11,0x81,0x6F,0,0,0xC0,0x20,0x15,0x6E};
            cpu.write(guidva,hal,16);w(desc,252);w(desc2,252);
            uint32_t name=wx86_scratch_put_cstr(cpu,"Hot Wheels vitaGL HAL");uint32_t descs=wx86_scratch_put_cstr(cpu,"D3D6 subset");
            if(!name||!descs)return kInvalid;
            callback(cb,{guidva,descs,name,desc,desc2,c.arg(2)});
            return kOk;
        }
        return kNotImpl;
    }
    uint32_t device_call(unsigned s,Cpu& c){
        if(!device)return kInvalid;
        switch(s){
        case 5:{auto* v=rec(c.arg(1));if(!v||v->kind!=Kind::Viewport3)return kInvalid;
            if(!v->vp_id) v->vp_id=device->create_viewport();
            if(v->rect.w>0&&v->rect.h>0&&device->set_viewport2(v->vp_id,v->rect.x,v->rect.y,v->rect.w,v->rect.h)!=kOk)return kInvalid;
            return kOk;}
        case 6:{auto* v=rec(c.arg(1));if(!v||v->kind!=Kind::Viewport3)return kInvalid;v->vp_id=0;if(current_vp==c.arg(1))current_vp=0;return kOk;}
        case 8:{uint32_t cb=c.arg(1),ctx=c.arg(2),fmt=wx86_scratch_alloc(32);if(!cb||!fmt)return kInvalid;
            for(TexFormat f:Device::texture_formats()){
                std::array<uint32_t,8> x{};x[0]=32;
                switch(f){case TexFormat::ARGB1555:x[1]=0x41;x[3]=16;x[4]=0x7c00;x[5]=0x03e0;x[6]=0x001f;x[7]=0x8000;break;
                case TexFormat::RGB565:x[1]=0x40;x[3]=16;x[4]=0xf800;x[5]=0x07e0;x[6]=0x001f;break;
                case TexFormat::ARGB4444:x[1]=0x41;x[3]=16;x[4]=0x0f00;x[5]=0x00f0;x[6]=0x000f;x[7]=0xf000;break;
                case TexFormat::PAL8:x[1]=0x20;x[3]=8;break;default:break;}
                cpu.write(fmt,x.data(),32);if(callback(cb,{fmt,ctx})==0)break;
            }return kOk;}
        case 9:return device->begin_scene();
        case 10:return device->end_scene();
        case 12:{auto* v=rec(c.arg(1));if(!v||v->kind!=Kind::Viewport3||!v->vp_id)return kInvalid;
            current_vp=c.arg(1);return device->set_current_viewport(v->vp_id);}
        case 13:if(!mapped(c.arg(1),4))return kInvalid;if(!current_vp){w(c.arg(1),0);return kNoViewport;}w(c.arg(1),current_vp);return kOk;
        case 22:return device->set_render_state(c.arg(1),c.arg(2));
        case 28:{uint32_t prim=c.arg(1),fvf=c.arg(2),ptr=c.arg(3),count=c.arg(4),flags=c.arg(5);
            if(count>65536||!mapped(ptr,count*24u)) return kInvalid;
            std::vector<float> v(size_t(count)*6);
            if(!cpu.read(ptr,v.data(),v.size()*sizeof(float)))return kInvalid;
            return device->draw_primitive(prim,fvf,v.data(),count,flags);}
        case 38:{uint32_t stage=c.arg(1),tex=c.arg(2);if(tex){auto* t=rec(tex);if(!t||t->kind!=Kind::Texture2)return kInvalid;}
            return device->set_texture(stage,tex);}
        case 40:return device->set_texture_stage_state(c.arg(1),c.arg(2),c.arg(3));
        default:return kNotImpl;
        }
    }
    uint32_t viewport_call(unsigned s,Cpu& c,Rec& p){
        if(s==12){
            uint32_t count=c.arg(1),rp=c.arg(2),flags=c.arg(3);if(!device)return kInvalid;
            std::vector<Rect> rs;if(count>4096)return kInvalid;
            if(count){if(!mapped(rp,count*16u))return kInvalid;for(uint32_t i=0;i<count;++i){uint32_t a=rp+i*16;int32_t x=int32_t(r(a)),y=int32_t(r(a+4)),x2=int32_t(r(a+8)),y2=int32_t(r(a+12));rs.push_back({x,y,x2-x,y2-y});}}
            return device->viewport_clear(p.vp_id,count,rs.empty()?nullptr:rs.data(),flags);
        }
        if(s==17){uint32_t a=c.arg(1);if(!mapped(a,44))return kInvalid;
            uint32_t n=r(a);if(n<44)return kInvalid;int32_t x=int32_t(r(a+4)),y=int32_t(r(a+8)),wpx=int32_t(r(a+12)),hpx=int32_t(r(a+16));
            if(wpx<=0||hpx<=0) return kInvalid;
            p.rect={x,y,wpx,hpx};
            if(ops.image_log){char msg[96];std::snprintf(msg,sizeof(msg),"D3D Viewport3 x=%d y=%d w=%d h=%d",x,y,wpx,hpx);ops.image_log(msg);}
            if(device&&p.vp_id) return device->set_viewport2(p.vp_id,x,y,wpx,hpx);
            return kOk;
        }
        return kNotImpl;
    }
    bool setup(){
        register_vtable(Kind::D3D3,{2,0,0,2,0,0,2,0,4,0,0,0});
        std::vector<uint8_t> dv(41,0);dv[0]=2;dv[5]=1;dv[6]=1;dv[8]=2;dv[9]=0;dv[10]=0;dv[12]=1;dv[13]=1;dv[22]=2;dv[28]=5;dv[38]=2;dv[40]=3;register_vtable(Kind::Device3,dv);
        std::vector<uint8_t> vv(18,0);vv[0]=2;vv[12]=3;vv[17]=1;register_vtable(Kind::Viewport3,vv);
        register_vtable(Kind::Texture2,{2,0,0,1,1,1,0});
        for(auto x:vt) if(!x) return false;
        d3d=object(Kind::D3D3);return d3d!=0;
    }
};

ComBridge::ComBridge(Bridge& b,Cpu& c,Backend& be,ComHostOps o):p_(new Impl(b,c,be,std::move(o))){}
ComBridge::~ComBridge(){delete p_;}
bool ComBridge::install(){return p_&&wx86_scratch_base()&&p_->setup();}
uint32_t ComBridge::d3d3_object()const{return p_?p_->d3d:0;}
uint32_t ComBridge::acquire_d3d3_object(){
    if(!p_)return 0;
    auto* d=p_->rec(p_->d3d);
    if(!d||d->kind!=Kind::D3D3)return 0;
    ++d->refs;
    return p_->d3d;
}
uint32_t ComBridge::create_texture(uint32_t surface){
    if(!p_||!surface||!p_->ops.texture_source) return 0;
    TextureSource t;
    if(!p_->ops.texture_source(surface,t)) return 0;
    uint32_t a=p_->object(Kind::Texture2);if(a)p_->rec(a)->surface=surface;return a;
}
bool ComBridge::flip(uint32_t surface){
    if(!p_||!p_->device||!p_->target||!p_->ops.is_primary||!p_->ops.is_primary(surface))return false;
    if (!p_->writer) return false;
    p_->device->present();
    // Run only once the queue has accepted this producer slot; READY drops
    // affect presentation only, never guest audio/input time.
    if (!p_->writer.publish(p_->ops.producer_frame)) return false;
    p_->writer = p_->frames.begin_write();
    if (!p_->writer) return false;
    p_->device->set_ring(*p_->writer.ring());
    if (p_->sync_baseline) {
        auto lease = p_->frames.take();
        if (lease) p_->consume_frame(std::move(lease));
    }
    return true;
}
}
