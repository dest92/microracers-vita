#!/usr/bin/env bash
# tests/run_audio_tap.sh — SPEC-048 CA10/CA11: tap del PCM entregado al sink.
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/build"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$HW/src" \
    "$HW/tests/test_audio_tap.cpp" "$HW/src/audio/wav.cpp" -o "$HW/build/test_audio_tap"
"$HW/build/test_audio_tap"
