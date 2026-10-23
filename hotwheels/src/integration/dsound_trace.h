// SPEC-048: bounded diagnostic transport. All integers on disk are LE u32.
// Header HWDS001\0, then {kind, word_count, payload_bytes}, words, payload.
// Calls serialized by this mutex: the output callback can run without g_mx.
#pragma once
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace hw::integration {
enum DsTraceKind : uint32_t {
    DsCreate=1, DsDuplicate, DsRelease, DsLock, DsUnlock, DsFrequency,
    DsVolume, DsPan, DsPosition, DsPlay, DsStop, DsGetPosition, DsFormat,
    DsMix, DsOutput, DsEnd, DsInvalid
};
class DsoundTrace {
public:
    ~DsoundTrace() { close(); }
    bool open(const char* path, uint64_t cap=64ull*1024*1024, const char* magic="HWDS001") {
        std::lock_guard<std::mutex> lk(mx_);
        if (file_ || !path || !*path || cap<24) return false;
        file_=std::fopen(path,"wb");
        if (!file_) return false;
        cap_=cap; used_=0;
        std::setvbuf(file_,nullptr,_IOFBF,64*1024);
        write(magic,8);
        return file_!=nullptr;
    }
    bool active() const { std::lock_guard<std::mutex> lk(mx_); return file_!=nullptr; }
    void event(uint32_t kind,const uint32_t* words,uint32_t n,const void* payload,uint32_t bytes) {
        std::lock_guard<std::mutex> lk(mx_);
        if (!file_) return;
        if (kind==DsInvalid) { finish(2); return; }
        const uint64_t size=12ull+4ull*n+bytes;
        if (n>16 || (n&&!words) || (bytes&&!payload) || size+used_+16>cap_) {
            finish(1); return;
        }
        word(kind); word(n); word(bytes);
        for (uint32_t i=0;i<n;++i) word(words[i]);
        if (bytes) write(payload,bytes);
    }
    void close() { std::lock_guard<std::mutex> lk(mx_); if(file_) finish(0); }
private:
    void write(const void* data,uint32_t bytes) {
        if (!file_) return;
        if (std::fwrite(data,1,bytes,file_)!=bytes) {
            // A partial record deliberately lacks END; reader must reject it.
            std::fclose(file_); file_=nullptr; return;
        }
        used_+=bytes;
    }
    void word(uint32_t v) {
        const uint8_t b[]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};
        write(b,4);
    }
    void finish(uint32_t status) {
        word(DsEnd); word(1); word(0); word(status);
        if(file_) { std::fclose(file_); file_=nullptr; }
    }
    mutable std::mutex mx_;
    FILE* file_=nullptr;
    uint64_t used_=0,cap_=0;
};
} // namespace hw::integration
