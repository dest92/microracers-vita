#!/usr/bin/env bash
# HW-010: tests host del renderer D3D6 (spec 003).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -O1 -Wall -Wextra -Werror -DHW_D3D_LINK_WINX86_DX=1 -Isrc tests/test_d3d.cpp \
    $(ls src/d3d/*.cpp 2>/dev/null | grep -v -e bridge.cpp -e vitagl) \
    ../winx86-dx/src/d3d/device.cpp -I../winx86-dx/include -o build/test_d3d
build/test_d3d
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I../winx86-dx/include \
    tests/test_winx86_dx_d3d.cpp ../winx86-dx/src/d3d/device.cpp \
    -o build/test_winx86_dx_d3d
build/test_winx86_dx_d3d
