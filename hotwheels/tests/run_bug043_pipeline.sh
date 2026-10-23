#!/usr/bin/env bash
# Integración lenta: usa run_host.sh dentro de tools/run_race.sh (QEMU ARM).
# En run_all queda marcada integration y se salta salvo RUN_ALL_INTEGRATION=1.
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(bash "$HW/tools/run_race.sh" 25 d3d 2>&1)" || {
    printf '%s\n' "$OUT"
    exit 1
}
printf '%s\n' "$OUT"
grep -q 'fin=limpio' <<<"$OUT"
