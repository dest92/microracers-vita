#pragma once
#include <cstdint>
#include "presenter_init_state.h"
namespace hw::present {
class VitaPresenter {
public:
    VitaPresenter() = default;
    VitaPresenter(const VitaPresenter&) = delete;
    VitaPresenter& operator=(const VitaPresenter&) = delete;
    // Pools fijos: 8 MiB RAM + 16 MiB CDRAM, MSAA desactivado.
    bool init(uint32_t width,uint32_t height);
    bool resize(uint32_t width,uint32_t height);
    void present(const uint8_t* idx,uint32_t pitch,const uint32_t pal[256]);
    void shutdown();
    const char* error() const { return error_; }
    uint64_t frames() const { return frames_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
private:
    bool make_texture(uint32_t width,uint32_t height);
    void release_resources();
    uint32_t width_=0,height_=0,texture_=0,program_=0;
    uint8_t* texture_data_=nullptr;
    uint32_t* palette_data_=nullptr;
    uint64_t frames_=0;
    const char* error_="not initialized";
    PresenterInitState init_state_;
};
}
