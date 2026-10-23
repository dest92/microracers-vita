#!/usr/bin/env bash
# run_dinput_vita.sh — tests host del mapeo Vita de DINPUT (spec 007b, HW-022).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  tests/test_dinput_vita.cpp src/dinput/vita_input.cpp src/dinput/dinput.cpp src/io/config.cpp \
  -o build/test_dinput_vita
build/test_dinput_vita
