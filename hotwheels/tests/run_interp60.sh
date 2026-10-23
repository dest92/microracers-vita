#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -O1 -Wall -Wextra -Werror -pthread -DHW_D3D_LINK_WINX86_DX=1 -Isrc \
    -I../winx86-dx/include tests/test_interp60.cpp src/fps/interp60.cpp \
    src/d3d/core.cpp ../winx86-dx/src/d3d/device.cpp \
    -o build/test_interp60
build/test_interp60
