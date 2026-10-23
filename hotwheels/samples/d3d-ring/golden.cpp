#include "scene.h"
#include <cstdio>

int main(int argc, char** argv) {
    ring_scene::init_textures();
    hw::d3d::Ring ring;
    hw::d3d::Device device(ring, 160, 120, ring_scene::lookup);
    ring_scene::build(device);
    hw::d3d::RefRaster raster;
    hw::d3d::Replayer(raster).play(ring);
    const char* path = argc > 1 ? argv[1] : "build/golden.png";
    if (!raster.write_png(path)) return 1;
    std::printf("golden %ux%u hash=0x%016llx path=%s\n", raster.width(), raster.height(),
                (unsigned long long)raster.hash(), path);
    return 0;
}
