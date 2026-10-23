#!/usr/bin/env bash
# tests/run_ds_bridge.sh — HW-020 audio: seam ds_emul → Engine (WAV no vacío).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=gnu++17 -Wall -Wextra -Werror -Isrc \
  tests/test_ds_bridge.cpp \
  src/audio/mixer.cpp src/audio/wav.cpp src/audio/music.cpp \
  src/audio/dsound_bus.cpp src/audio/engine.cpp src/audio/ds_bridge.cpp \
  -o build/test_ds_bridge
build/test_ds_bridge
