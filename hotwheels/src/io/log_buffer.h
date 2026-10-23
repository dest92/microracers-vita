// src/io/log_buffer.h — logger con buffer (HW-PERF). Antes: sceIoOpen+Write+Close
// por línea (3 syscalls a la memory card c/u). Ahora: acumula y vuelca por lote.
// Las primeras `sync_lines` líneas van síncronas (boot/cuelgues: un log sin
// hilo flusher no debe quedar atrapado en el buffer si la app se congela).
// Política: vuelca al llenar `cap`, al pasar `interval_ms` desde el último volcado,
// o de inmediato si la línea es crítica (FATAL/fault/error/exit) para no perder
// la causa de un cuelgue. El estado y el sink se serializan internamente.
#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace hw::io {

class LogBuffer {
public:
    using Sink = std::function<void(const std::string&)>;
    LogBuffer(Sink sink, size_t cap = 8192, uint32_t interval_ms = 250, uint32_t sync_lines = 256)
        : sink_(std::move(sink)), cap_(cap), interval_(interval_ms), sync_left_(sync_lines) {}
    void write(const char* line, uint32_t now_ms);
    void flush();
    // Volcado periódico desde un hilo vigía: si venció el intervalo vuelca lo
    // pendiente. try_lock: nunca bloquea al llamador. true si volcó.
    bool tick(uint32_t now_ms);
    uint64_t flushes() const;
    static bool critical(const std::string& s);
private:
    void flush_unlocked();
    mutable std::mutex mutex_;
    Sink sink_;
    std::string buf_;
    size_t cap_;
    uint32_t interval_, sync_left_, last_ms_ = 0;
    uint64_t flushes_ = 0;
};

}  // namespace hw::io
