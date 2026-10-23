// Portable MMIO RIFF/WAVE API.
#pragma once

#include "winmm.h"

#include <cstdint>
#include <map>
#include <vector>

namespace winx86::dx::winmm {

using HMMIO = uint32_t;  // 0 = inválido

struct MmckInfo {
    uint32_t ckid = 0;
    uint32_t cksize = 0;
    uint32_t fccType = 0;
    uint32_t dwDataOffset = 0;
    uint32_t dwFlags = 0;
};

// Vista host de buffers MMIO, no wire/layout de MMIOINFO PE32. Un bridge debe
// validar las direcciones guest y copiar los bytes campo por campo.
struct MmioInfo {
    uint32_t dwFlags = 0;
    uint32_t wErrorRet = 0;
    int32_t cchBuffer = 0;
    uint8_t* pchBuffer = nullptr;
    uint8_t* pchNext = nullptr;
    uint8_t* pchEndRead = nullptr;
    int32_t lBufOffset = 0;
    int32_t lDiskOffset = 0;
};

class Mmio {
public:
    explicit Mmio(FileSource& fs) : fs_(fs) {}
    Mmio(const Mmio&) = delete;
    Mmio& operator=(const Mmio&) = delete;
    Mmio(Mmio&&) = delete;
    Mmio& operator=(Mmio&&) = delete;

    HMMIO open(const char* filename, MmioInfo* info, uint32_t flags);
    int32_t read(HMMIO h, void* buf, int32_t count);
    int32_t seek(HMMIO h, int32_t offset, int origin);
    uint32_t descend(HMMIO h, MmckInfo& ck, const MmckInfo* parent, uint32_t flags);
    uint32_t ascend(HMMIO h, MmckInfo& ck, uint32_t flags);
    uint32_t get_info(HMMIO h, MmioInfo& info);
    uint32_t set_info(HMMIO h, const MmioInfo& info);
    uint32_t advance(HMMIO h, MmioInfo& info, uint32_t flags);
    uint32_t close(HMMIO h);

private:
    struct Chunk {
        size_t payload;
        size_t size;
    };
    struct File {
        std::vector<uint8_t> data;
        int32_t pos = 0;
        uint32_t flags = 0;
        std::vector<Chunk> stack;  // chunks descendidos (rango de lectura)
        std::vector<uint8_t> buf;  // buffer de advance/get_info
        int32_t buf_off = 0;
        int32_t buf_next = 0;
        int32_t buf_len = 0;
    };

    File* get(HMMIO h);
    int32_t read_limit(const File& f) const;
    bool scan_chunk(File& f, uint32_t want_ckid, uint32_t want_type, uint32_t flags,
                    size_t from, size_t to, MmckInfo& out) const;

    FileSource& fs_;
    std::map<HMMIO, File> files_;
    HMMIO next_ = 1;
};

}  // namespace winx86::dx::winmm
