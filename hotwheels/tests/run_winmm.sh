#!/usr/bin/env bash
# run_winmm.sh — tests host de WINMM (spec 005, HW-015). WAVs sintéticos.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  tests/test_winmm.cpp src/winmm/mmio.cpp src/winmm/mci.cpp \
  -o build/test_winmm
build/test_winmm

c++ -std=c++17 -Wall -Wextra -Werror \
  -I../winx86-dx/include \
  tests/test_winx86_dx_winmm.cpp ../winx86-dx/src/winmm/mmio.cpp \
  -o build/test_winx86_dx_winmm
build/test_winx86_dx_winmm

c++ -std=c++17 -Wall -Wextra -Werror \
  -I../winx86-dx/include \
  tests/test_winx86_dx_mci.cpp ../winx86-dx/src/winmm/mci.cpp \
  -o build/test_winx86_dx_mci
build/test_winx86_dx_mci
