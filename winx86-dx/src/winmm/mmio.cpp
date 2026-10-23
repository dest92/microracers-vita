// Portable MMIO RIFF/WAVE implementation.
#include "../../include/winx86-dx/winmm/mmio.h"
#include "../../include/winx86-dx/winmm/detail/mmio_bounds.h"

#include <cstring>

namespace winx86::dx::winmm {
namespace {

uint32_t u32_at(const std::vector<uint8_t>& d, size_t off) {
    return static_cast<uint32_t>(d[off]) | (static_cast<uint32_t>(d[off + 1]) << 8)
        | (static_cast<uint32_t>(d[off + 2]) << 16) | (static_cast<uint32_t>(d[off + 3]) << 24);
}

}  // namespace

Mmio::File* Mmio::get(HMMIO h) {
    auto it = files_.find(h);
    return it == files_.end() ? nullptr : &it->second;
}

int32_t Mmio::read_limit(const File& f) const {
    size_t lim = f.data.size();
    if (!f.stack.empty()) {
        size_t end = f.stack.back().payload + f.stack.back().size;
        if (end < lim) lim = end;
    }
    return static_cast<int32_t>(lim);
}

HMMIO Mmio::open(const char* filename, MmioInfo* info, uint32_t flags) {
    if (info) info->wErrorRet = 0;
    const uint32_t mode = flags & 0x00000003u;  // MMIO_RWMODE
    if (!filename || mode == MMIO_WRITE || mode == MMIO_READWRITE) {
        if (info) info->wErrorRet = MMIOERR_CANNOTOPEN;
        return 0;
    }
    std::vector<uint8_t> data;
    if (!fs_.read(filename, data)) {
        if (info) info->wErrorRet = MMIOERR_FILENOTFOUND;
        return 0;
    }
    HMMIO h = next_++;
    File& f = files_[h];
    f.data = std::move(data);
    f.pos = 0;
    f.flags = flags;
    // MMIO_ALLOCBUF: buffer interno (Win32: MMIO_DEFAULTBUFFER = 8 KiB, o el
    // cchBuffer pedido). Arranca vacío en pos 0 (BUG-009, wavread.c del SDK).
    if (flags & MMIO_ALLOCBUF) {
        const int32_t cap = (info && info->cchBuffer > 0) ? info->cchBuffer : MMIO_DEFAULTBUFFER;
        f.buf.assign(static_cast<size_t>(cap), 0);
        f.buf_off = 0; f.buf_next = 0; f.buf_len = 0;
    }
    if (info) {
        info->dwFlags = flags;
        info->lDiskOffset = 0;
        info->wErrorRet = 0;
    }
    return h;
}

int32_t Mmio::read(HMMIO h, void* buf, int32_t count) {
    File* f = get(h);
    if (!f || !buf || count < 0) return -1;
    const int32_t lim = read_limit(*f);
    if (f->pos >= lim) return 0;
    int32_t n = lim - f->pos;
    if (n > count) n = count;
    if (n <= 0) return 0;
    std::memcpy(buf, f->data.data() + f->pos, static_cast<size_t>(n));
    f->pos += n;
    return n;
}

int32_t Mmio::seek(HMMIO h, int32_t offset, int origin) {
    File* f = get(h);
    if (!f || origin < MMSEEK_SET || origin > MMSEEK_END) return -1;
    int64_t base = (origin == MMSEEK_SET) ? 0
        : (origin == MMSEEK_CUR)            ? f->pos
                                            : static_cast<int64_t>(f->data.size());
    int64_t np = base + offset;
    if (np < 0 || np > static_cast<int64_t>(f->data.size())) return -1;
    f->pos = static_cast<int32_t>(np);
    return f->pos;
}

bool Mmio::scan_chunk(File& f, uint32_t want_ckid, uint32_t want_type, uint32_t flags,
    size_t from, size_t to, MmckInfo& out) const {
    const size_t n = f.data.size();
    if (to > n) to = n;
    size_t p = from;
    while (p <= to && to - p >= 8) {
        const uint32_t ckid = u32_at(f.data, p);
        const uint32_t sz = u32_at(f.data, p + 4);
        const size_t payload = static_cast<size_t>(sz);
        const size_t payload_start = p + 8;
        const size_t available = to - payload_start;
        // RIFF child data and its required word-alignment byte must stay inside
        // the parent. A malformed child ends the scan: later bytes aren't siblings.
        if (payload > available || ((sz & 1u) && payload == available)) return false;
        const bool is_form = ckid == fourcc('R', 'I', 'F', 'F') || ckid == fourcc('L', 'I', 'S', 'T');
        bool hit;
        if (flags == MMIO_FINDRIFF)
            hit = ckid == fourcc('R', 'I', 'F', 'F');
        else if (flags == MMIO_FINDLIST)
            hit = ckid == fourcc('L', 'I', 'S', 'T');
        else if (flags == MMIO_FINDCHUNK)
            hit = ckid == want_ckid;
        else
            hit = true;  // flags 0: el chunk en la posición actual (Win32)
        // FINDRIFF/FINDLIST filtran por fccType si el llamador lo puso.
        if (hit && is_form && payload < 4) return false;
        if (hit && is_form && (flags == MMIO_FINDRIFF || flags == MMIO_FINDLIST) && want_type
            && u32_at(f.data, p + 8) != want_type)
            hit = false;
        if (hit) {
            out.ckid = ckid;
            out.cksize = sz;
            // Win32: para RIFF/LIST dwDataOffset apunta al form/list type.
            out.fccType = is_form ? u32_at(f.data, p + 8) : 0;
            out.dwDataOffset = static_cast<uint32_t>(p + 8);
            out.dwFlags = 0;
            return true;
        }
        p = payload_start + payload + (sz & 1u);
    }
    return false;
}

uint32_t Mmio::descend(HMMIO h, MmckInfo& ck, const MmckInfo* parent, uint32_t flags) {
    File* f = get(h);
    if (!f) return MMIOERR_CHUNKNOTFOUND;
    size_t from, to;
    if (parent) {
        const bool is_form = parent->ckid == fourcc('R', 'I', 'F', 'F')
            || parent->ckid == fourcc('L', 'I', 'S', 'T');
        const uint32_t file_size = static_cast<uint32_t>(std::min<uint64_t>(f->data.size(), UINT32_MAX));
        uint32_t parent_from = 0, parent_to = 0;
        if (!detail::checked_parent_range(parent->dwDataOffset, parent->cksize, file_size,
                is_form, parent_from, parent_to))
            return MMIOERR_CHUNKNOTFOUND;
        from = parent_from;
        to = parent_to;
    } else {
        from = static_cast<size_t>(f->pos);
        to = f->data.size();
    }
    if (from > to) return MMIOERR_CHUNKNOTFOUND;
    MmckInfo out;
    if (!scan_chunk(*f, ck.ckid, ck.fccType, flags, from, to, out)) return MMIOERR_CHUNKNOTFOUND;
    ck = out;
    const bool form = ck.ckid == fourcc('R', 'I', 'F', 'F') || ck.ckid == fourcc('L', 'I', 'S', 'T');
    f->pos = static_cast<int32_t>(ck.dwDataOffset + (form ? 4 : 0));
    f->stack.push_back({ck.dwDataOffset, ck.cksize});
    return 0;
}

uint32_t Mmio::ascend(HMMIO h, MmckInfo& ck, uint32_t) {
    File* f = get(h);
    if (!f) return MMIOERR_CANNOTSEEK;
    int64_t np = static_cast<int64_t>(ck.dwDataOffset) + ck.cksize;
    if (np & 1) np++;
    if (np > static_cast<int64_t>(f->data.size())) np = static_cast<int64_t>(f->data.size());
    f->pos = static_cast<int32_t>(np);
    if (!f->stack.empty()) f->stack.pop_back();
    return 0;
}

uint32_t Mmio::get_info(HMMIO h, MmioInfo& info) {
    File* f = get(h);
    if (!f) return MMIOERR_CANNOTSEEK;
    // Buffer consumido: re-anclarlo en la posición actual (descend/seek/read la
    // movieron), para que pchNext==pchEndRead signifique "pos" y no el offset viejo.
    if (!f->buf.empty() && f->buf_next >= f->buf_len) { f->buf_off = f->pos; f->buf_next = 0; f->buf_len = 0; }
    info.dwFlags = f->flags;
    info.lDiskOffset = f->pos;
    info.cchBuffer = static_cast<int32_t>(f->buf.size());
    info.lBufOffset = f->buf_off;
    if (f->buf.empty()) {
        info.pchBuffer = nullptr;
        info.pchNext = nullptr;
        info.pchEndRead = nullptr;
    } else {
        info.pchBuffer = f->buf.data();
        info.pchNext = f->buf.data() + f->buf_next;
        info.pchEndRead = f->buf.data() + f->buf_len;
    }
    return 0;
}

uint32_t Mmio::set_info(HMMIO h, const MmioInfo& info) {
    File* f = get(h);
    if (!f) return MMIOERR_CANNOTSEEK;
    int64_t pos = info.lDiskOffset;
    if (info.pchBuffer && info.pchNext >= info.pchBuffer && info.pchNext <= info.pchEndRead)
        pos = static_cast<int64_t>(info.lBufOffset) + (info.pchNext - info.pchBuffer);
    if (pos < 0) pos = 0;
    if (pos > static_cast<int64_t>(f->data.size())) pos = static_cast<int64_t>(f->data.size());
    f->pos = static_cast<int32_t>(pos);
    return 0;
}

uint32_t Mmio::advance(HMMIO h, MmioInfo& info, uint32_t) {
    File* f = get(h);
    if (!f) return MMIOERR_CANNOTSEEK;
    if (info.cchBuffer <= 0) return 0;
    f->buf.assign(static_cast<size_t>(info.cchBuffer), 0);
    const int32_t lim = read_limit(*f);
    int32_t n = lim - f->pos;
    if (n < 0) n = 0;
    if (n > info.cchBuffer) n = info.cchBuffer;
    if (n > 0) std::memcpy(f->buf.data(), f->data.data() + f->pos, static_cast<size_t>(n));
    f->buf_off = f->pos;
    f->buf_next = 0;
    f->buf_len = n;
    info.pchBuffer = f->buf.data();
    info.pchNext = f->buf.data();
    info.pchEndRead = f->buf.data() + n;
    info.lBufOffset = f->pos;
    return 0;
}

uint32_t Mmio::close(HMMIO h) {
    auto it = files_.find(h);
    if (it == files_.end()) return MMIOERR_CANNOTCLOSE;
    files_.erase(it);
    return 0;
}

}  // namespace winx86::dx::winmm
