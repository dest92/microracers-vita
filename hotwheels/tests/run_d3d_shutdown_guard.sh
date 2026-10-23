#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -O1 -Wall -Wextra -Werror -pthread -Isrc \
    tests/test_d3d_shutdown_guard.cpp -o build/test_d3d_shutdown_guard
build/test_d3d_shutdown_guard
