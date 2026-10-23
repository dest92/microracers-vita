#!/usr/bin/env bash
# tests/run_ds_resample.sh — mezcla por voz de ds_emul (patch winx86-0012).
set -euo pipefail
cd "$(dirname "$0")/.."
W="${WINX86_DIR:-$(cd .. && pwd)/d2vita/D2Vita/third_party/winx86}"
if [ ! -f "$W/src/runtime/ds_resample.h" ]; then
    echo "SKIP run_ds_resample: falta $W/src/runtime/ds_resample.h (aplicar patches/winx86-0012)"; exit 0
fi
mkdir -p build
c++ -std=gnu++17 -Wall -Wextra -Werror -I"$W/src" tests/test_ds_resample.cpp -o build/test_ds_resample
build/test_ds_resample
