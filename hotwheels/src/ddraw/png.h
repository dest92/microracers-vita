#pragma once
#include "software.h"
#include <string>
namespace hw::ddraw {
class PngPresenter final : public Presenter {
public:
    PngPresenter(std::string path,uint32_t width,uint32_t height)
      : path_(std::move(path)),width_(width),height_(height) {}
    void present(const uint8_t* idx,uint32_t pitch,const uint32_t pal[256]) override;
private:
    std::string path_;
    uint32_t width_,height_;
};
}
