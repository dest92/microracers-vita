#!/usr/bin/env bash
# tests/run_audio_tap_lifecycle.sh — SPEC-048: ciclo de vida de TapSink (open/close vs worker).
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/build"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$HW/src" \
    "$HW/tests/test_audio_tap_lifecycle.cpp" "$HW/src/audio/wav.cpp" -o "$HW/build/test_audio_tap_lifecycle"
"$HW/build/test_audio_tap_lifecycle"
