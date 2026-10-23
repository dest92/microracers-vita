#!/usr/bin/env bash
# HW-072 (perf-cpu) SPEC-055 CA2: muestreo del dt del invitado desde el host.
# Standalone (no depende de CMake) para poder correrlo sin tocar el build
# compartido; el target de ctest equivalente está en CMakeLists.txt.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=gnu++17 -O1 -Wall -Wextra -Werror -Isrc \
    tests/test_guest_dt_profile.cpp src/runtime/guest_dt_profile.cpp -o build/test_guest_dt_profile
build/test_guest_dt_profile
echo "CA2 OK: dt del invitado (secuencia por espera + histograma + percentiles)"
