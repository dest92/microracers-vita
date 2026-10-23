#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/build"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$HW/src" \
    "$HW/tests/test_audio_output_contract.cpp" -o "$HW/build/test_audio_output_contract"
"$HW/build/test_audio_output_contract"
