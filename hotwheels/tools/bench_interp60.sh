#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${TMPDIR:-/tmp}/hw035-bench-interp60-$$"
trap 'rm -f "$BIN"' EXIT
c++ -O2 -DNDEBUG -std=c++17 -Wall -Wextra -Werror -I"$HW/src" \
  "$HW/tests/bench_interp60.cpp" "$HW/src/fps/interp60.cpp" "$HW/src/d3d/core.cpp" \
  -o "$BIN"
"$BIN" "${1:-765}" "${2:-400}" "${3:-0}"
