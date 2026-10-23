#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HW/.." && pwd)"
DEST="${1:?uso: export_ghidra_functions.sh <csv-destino>}"
GHIDRA="${GHIDRA_HOME:-$ROOT/shared/opt/ghidra_11.3.2_PUBLIC}"
HEADLESS="$GHIDRA/support/analyzeHeadless"
[ -x "$HEADLESS" ] || { echo "FAIL: falta $HEADLESS" >&2; exit 2; }
mkdir -p "$(dirname "$DEST")"
"$HEADLESS" "$ROOT/shared/ghidra-proyectos" HotWheels \
  -process HotWheels.exe -noanalysis -readOnly \
  -scriptPath "$HW/tools/ghidra" -postScript ExportFunctionRanges.java "$DEST"
[ -s "$DEST" ] || { echo "FAIL: mapa Ghidra vacío: $DEST" >&2; exit 2; }
echo "OK: mapa de funciones Ghidra -> $DEST"
