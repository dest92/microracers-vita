// src/io/read_cache.h — caché RAM de lecturas completas (HW-020, spec 010).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hw::io {

struct CacheStats {
    uint64_t files = 0;       // archivos en caché
    uint64_t bytes = 0;       // bytes residentes
    uint64_t hits = 0;        // get() que encontró
    uint64_t misses = 0;      // get() que no encontró
    uint64_t bypasses = 0;    // load() rechazado por tamaño/límite
    uint64_t preload_us = 0;  // tiempo del último preload_dir
};

// Cachea en RAM archivos chicos leídos enteros (patrón D2Vita read-ahead):
// evita los "tirones" de decenas de ms por ReadFile en la memory card.
// Single-thread (el juego es monohilo, FINDINGS #6).
class ReadCache {
public:
    void configure(size_t max_file_bytes, size_t max_total_bytes);
    void clear();

    // Carga un archivo entero si entra en el umbral. false = bypass (no cachear).
    bool load(const std::string& host_path);

    // Puntero a los bytes cacheados, o nullptr. Actualiza hits/misses.
    const std::vector<uint8_t>* get(const std::string& host_path) const;

    // Precarga todos los archivos <= max_file de `host_dir`. Devuelve cuántos cargó.
    size_t preload_dir(const std::string& host_dir, bool recursive = true);

    const CacheStats& stats() const { return stats_; }
    size_t max_file_bytes() const { return max_file_; }
    size_t max_total_bytes() const { return max_total_; }

private:
    mutable CacheStats stats_;
    std::map<std::string, std::vector<uint8_t>> blobs_;
    size_t max_file_ = 2u << 20;
    size_t max_total_ = 32u << 20;
};

}  // namespace hw::io
