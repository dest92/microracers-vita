#!/usr/bin/env bash
# Regenera src/hle/aot_raster.c desde HotWheels.exe (local; no se commitea el exe).
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
EXE="${HW_EXE:-$HW/build-vita/stage/hotwheels/HotWheels.exe}"
OD="${OBJDUMP:-$(ls "$HW"/../shared/opt/llvm-mingw*/bin/llvm-objdump | head -1)}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$OD" -d --no-show-raw-insn "$EXE" > "$T/all.dis"
awk '/^  41d5a0:/,/^  41e70d:/' "$T/all.dis" > "$T/ft3.dis"
awk '/^  420d30:/,/^  421eb9:/' "$T/all.dis" > "$T/semi.dis"
{
  echo '// src/hle/aot_raster.c — GENERADO por tools/gen_aot_raster.sh (tools/aot_x86.py)'
  echo '// desde HotWheels.exe md5 4fbf44e9…: swRasterFT3 (0x41d5a0) y swRasterFT3Semi'
  echo '// (0x420d30) traducidos instrucción por instrucción. NO EDITAR A MANO.'
  echo '#include "hle/aot_runtime.h"'
  echo '#pragma GCC diagnostic ignored "-Wunused-label"'
  python3 "$HW/tools/aot_x86.py" "$T/ft3.dis" aot_swRasterFT3
  python3 "$HW/tools/aot_x86.py" "$T/semi.dis" aot_swRasterFT3Semi
} > "$HW/src/hle/aot_raster.c"
echo "OK $(wc -l < "$HW/src/hle/aot_raster.c") líneas"
