#!/usr/bin/env bash
# tests/run_audio_port_contract.sh — SPEC-048 CA11: contrato del puerto con el driver.
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/build"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$HW/src" \
    "$HW/tests/test_audio_port_contract.cpp" -o "$HW/build/test_audio_port_contract"
"$HW/build/test_audio_port_contract"
