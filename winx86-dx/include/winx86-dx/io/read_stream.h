// Portable buffered stream for host files or immutable in-memory blobs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace winx86::dx::io {

class ReadStream {
public:
    // FILE* queda prestado: el caller conserva ownership y debe mantenerlo
    // abierto hasta destruir este stream. El blob sí queda retenido por shared_ptr.
    ReadStream(FILE* f, size_t window = 64 * 1024);
    // Modo RAM: el archivo entero ya está en memoria (caché compartida, 0 syscalls).
    explicit ReadStream(std::shared_ptr<const std::vector<uint8_t>> blob);
    ReadStream(const ReadStream&) = delete;
    ReadStream& operator=(const ReadStream&) = delete;
    ReadStream(ReadStream&&) = delete;
    ReadStream& operator=(ReadStream&&) = delete;
    size_t read(void* dst, size_t n);               // bytes leídos (0 en EOF)
    int64_t seek(int64_t dist, int whence);          // nueva pos o -1 (pos intacta)
    uint64_t tell() const { return pos_; }
    uint64_t size() const { return size_; }
    uint64_t syscalls() const { return sys_; }       // fseek/fread reales
private:
    FILE* f_;
    uint64_t size_ = 0, pos_ = 0, win_off_ = 0, sys_ = 0;
    std::shared_ptr<const std::vector<uint8_t>> blob_;
    std::vector<uint8_t> win_;
    size_t win_cap_;
};

}  // namespace winx86::dx::io
