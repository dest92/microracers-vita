#!/usr/bin/env bash
# run_dinput.sh — tests host de DirectInput (spec 007, HW-016). Fuente fake.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  tests/test_dinput.cpp src/dinput/dinput.cpp \
  -o build/test_dinput
build/test_dinput

# API pública del módulo reutilizable, compilada sin headers privados de HW.
c++ -std=c++17 -Wall -Wextra -Werror \
  -I../winx86-dx/include \
  tests/test_winx86_dx_dinput.cpp ../winx86-dx/src/dinput/dinput.cpp \
  -o build/test_winx86_dx_dinput
build/test_winx86_dx_dinput
