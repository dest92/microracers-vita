#!/usr/bin/env bash
# HW-067 (SPEC-046 CA2): tests host de ring_stats.
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -DHW_D3D_LINK_WINX86_DX=1 -Isrc -I../winx86-dx/include \
    tests/test_ring_stats.cpp src/d3d/core.cpp src/d3d/refraster.cpp src/d3d/frame_queue.cpp src/fps/interp60.cpp \
    ../winx86-dx/src/d3d/device.cpp -pthread -o build/test_ring_stats
build/test_ring_stats
