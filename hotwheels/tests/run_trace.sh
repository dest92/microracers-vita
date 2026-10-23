#!/usr/bin/env bash
# run_trace.sh — BUG-002: el formateo de traza debe ser aceptado por diff_trace.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  tests/test_trace.cpp src/trace/trace.cpp -o build/test_trace
# "corrida corta": prefijo de la referencia re-emitido por el módulo.
build/test_trace build/trace_short.log docs/wine-ref/boot-trace.txt 20
set +e
out="$(python3 tools/diff_trace.py --ref docs/wine-ref/boot-trace.txt build/trace_short.log 2>&1)"
rc=$?
set -e
echo "$out"
if [ "$rc" -ne 0 ]; then
  echo "TRACE: diff_trace rc=$rc (esperado 0: OK u OK parcial)"
  exit 1
fi
echo "TRACE: OK (diff_trace rc=0)"
