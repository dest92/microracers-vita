#pragma once
#include "software.h"
#include "../../../winx86-dx/include/winx86-dx/d3d/device.h"
#include <functional>
namespace d2rt { class Bridge; class Cpu; }
namespace hw::ddraw {
struct HostOps {
    // Debe devolver VA guest mapeada y escribible por Cpu::hostptr.
    std::function<uint32_t(size_t)> alloc_pixels;
    // Se invoca en cada Flip. El puntero vale sólo durante la llamada.
    std::function<void(const uint8_t*,uint32_t,const uint32_t*)> present;
    // Opcional; tiene prioridad sobre present y recibe el tamaño de la primaria.
    std::function<void(const uint8_t*,uint32_t,const uint32_t*,uint32_t,uint32_t)> present_sized;
    // Objetos externos: callbacks retornan una referencia COM adquirida.
    std::function<uint32_t()> d3d3_object;
    std::function<uint32_t(uint32_t)> create_texture;
    std::function<bool(uint32_t)> flip_3d;
    // Solicitud guest y modo vigente tras aceptar o rechazar SetDisplayMode.
    std::function<void(uint32_t,uint32_t,uint32_t,bool,uint32_t,uint32_t,uint32_t)> mode_changed;
};
// Requiere scratch guest inicializado. Instalar después de misc y antes de link.
bool install(d2rt::Bridge& bridge,d2rt::Cpu& cpu,const HostOps& ops);
// Lookups para ComHostOps. Punteros prestados hasta cambio de modo/superficie.
bool render_target(uint32_t surface,uint32_t& width,uint32_t& height);
bool texture_source(uint32_t surface,winx86::dx::d3d::TextureSource& out);
bool is_primary(uint32_t surface);
}
