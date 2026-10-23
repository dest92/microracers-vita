#!/usr/bin/env bash
# tests/run_io.sh — HW-020: tests host del IO Vita (spec 010). Sin vitasdk.
set -euo pipefail
cd "$(dirname "$0")/.."
W=../d2vita/D2Vita/third_party/winx86/src
mkdir -p build
c++ -std=gnu++17 -Wall -Wextra -Werror -Isrc -I"$W" -I../winx86-dx/include \
  tests/test_io.cpp \
  src/io/paths.cpp src/io/read_cache.cpp src/io/config.cpp src/io/boot_menu.cpp src/io/boot_selector.cpp \
  src/shims/core.cpp src/io/read_stream.cpp src/dinput/dinput.cpp \
  -o build/test_io
build/test_io
c++ -std=gnu++17 -Wall -Wextra -Werror -Isrc tests/test_read_stream.cpp src/io/read_stream.cpp -o build/test_read_stream
build/test_read_stream
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined \
  -I../winx86-dx/include \
  tests/test_winx86_dx_io.cpp ../winx86-dx/src/io/read_stream.cpp \
  -o build/test_winx86_dx_io
build/test_winx86_dx_io
c++ -std=c++17 -Wall -Wextra -Werror \
  -I../winx86-dx/include \
  tests/test_winx86_dx_vfs.cpp ../winx86-dx/src/io/vfs.cpp \
  -o build/test_winx86_dx_vfs
build/test_winx86_dx_vfs
c++ -std=gnu++17 -Wall -Wextra -Werror -Isrc tests/test_log_buffer.cpp src/io/log_buffer.cpp -o build/test_log_buffer
build/test_log_buffer
