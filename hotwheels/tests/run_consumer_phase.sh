#!/usr/bin/env bash
# HW-073 (perf-host) SPEC-054 CA4: acumulador de fases del consumer D3D.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=gnu++17 -O1 -Wall -Wextra -Werror -DD2RT_CPU_BOX86 -Isrc -I../winx86-dx/include \
    tests/test_consumer_phase.cpp -o build/test_consumer_phase
build/test_consumer_phase
echo "CA4 OK: fases del consumer (busy/wait/replay + ocupación)"
