#!/usr/bin/env bash
# tests/run_bug001.sh — regresión BUG-001: con el scheduler NATIVO (default)
# el exe real debe cruzar DirectDrawEnumerateExA (callback anidado desde un
# shim, src/shims/guest_call.h) y el scheduler debe apagarse limpio.
# Antes del fix: se colgaba tras la trampa 45 con EIP=centinela y GIL busy.
set -euo pipefail
cd "$(dirname "$0")/.."
HW_ENV="TRAPTAG=1" tools/run_host.sh "${1:-8}" >/dev/null 2>&1 || true
O=out/latest; f=0
grep -q 'DDRAW.dll!DirectDrawEnumerateExA' $O/stdout.txt || { echo "FAIL: no cruzó DirectDrawEnumerateExA"; f=1; }
grep -q '^\[hw\] sched: nativo' $O/run.log || { echo "FAIL: no corrió con scheduler nativo"; f=1; }
grep -q '^\[hw\] fin: shutdown requested' $O/run.log || { echo "FAIL: el scheduler nativo no se apagó limpio"; f=1; }
[ $f = 0 ] && echo "BUG-001: OK ($(grep -c '\[tag' $O/stdout.txt) trampas, $O)"; exit $f
