// Adaptador COM guest de Direct3D3/Device3/Viewport3/Texture2 (HW-010).
#pragma once
#include "d3d/d3d.h"
#include <functional>
#include <string>

namespace d2rt { class Bridge; class Cpu; }

namespace hw::d3d {
struct ComHostOps {
    // surface es el puntero COM IDirectDrawSurface4 entregado por el shim DDraw.
    std::function<bool(uint32_t surface, uint32_t& width, uint32_t& height)> render_target;
    std::function<bool(uint32_t surface, TextureSource& out)> texture_source;
    std::function<bool(uint32_t surface)> is_primary;
    // Called on the serialized consumer. Retaining the frame pins one of three slots.
    std::function<void(std::shared_ptr<const Ring>)> frame_closed;
    // Optional replacement for replay, such as interp60 capture. Runs on the consumer;
    // returning true skips direct replay for this frame.
    std::function<bool(std::shared_ptr<const Ring>)> present_frame;
    std::function<void(const std::string&)> log;
    std::function<void(const std::string&)> image_log;
    std::function<void()> consumer_start;
    // Guest-only clocks/audio run once after closing and before ring publication.
    std::function<void()> producer_frame;
    std::function<void()> lock_backend;
    std::function<void()> unlock_backend;
    std::function<void(uint32_t)> frame_presented;
};

class ComBridge {
public:
    ComBridge(d2rt::Bridge& bridge, d2rt::Cpu& cpu, Backend& backend, ComHostOps ops);
    ~ComBridge();
    bool install();
    uint32_t d3d3_object() const;
    // Retorna una referencia COM propia para entregar por QI a DirectDraw.
    uint32_t acquire_d3d3_object();
    // QI desde Surface4 a IDirect3DTexture2. Devuelve 0 si la superficie no es textura.
    uint32_t create_texture(uint32_t surface_com);
    // Lo llama Surface4::Flip de la primaria; el target D3D puede ser su backbuffer.
    bool flip(uint32_t surface_com);
private:
    struct Impl;
    Impl* p_;
};
}
