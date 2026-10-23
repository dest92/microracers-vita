#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I../winx86-dx/include tests/test_audio_boundaries.cpp ../winx86-dx/src/audio/mixer.cpp \
    -o build/test_audio_boundaries
build/test_audio_boundaries
