#!/usr/bin/env bash
# HW-006: tests host de los shims base (spec 006). Sin x86: CPU falsa.
set -euo pipefail
cd "$(dirname "$0")/.."
W=../d2vita/D2Vita/third_party/winx86/src
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Isrc -I"$W" tests/test_shims.cpp \
    src/io/read_stream.cpp $(ls src/shims/*.cpp 2>/dev/null | grep -v install.cpp) "$W/runtime/cpu.cpp" -o build/test_shims
build/test_shims
c++ -std=c++17 -Wall -Wextra -Werror -I../winx86-dx/include \
    tests/test_winx86_dx_shims.cpp ../winx86-dx/src/shims/base.cpp ../winx86-dx/src/shims/imports.cpp \
    ../winx86-dx/src/io/vfs.cpp ../winx86-dx/src/io/read_stream.cpp \
    -o build/test_winx86_dx_shims
build/test_winx86_dx_shims
