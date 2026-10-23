#!/usr/bin/env bash
# HW-068 (SPEC-052 CA1-CA4): tests host del módulo de diagnóstico de BUG-047.
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -DHW_D3D_LINK_WINX86_DX=1 -Isrc -I../winx86-dx/include \
    tests/test_hw_diag.cpp src/d3d/core.cpp \
    ../winx86-dx/src/d3d/device.cpp -pthread -o build/test_hw_diag
build/test_hw_diag
