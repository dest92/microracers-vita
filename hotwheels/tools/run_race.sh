#!/usr/bin/env bash
# Carrera real qemu: run_race.sh [segundos=40] [sw|d3d=sw]. Datos fuente intactos.
# HW-067: HW_RACE_SCRIPT=<guion> y HW_RACE_SETTINGS=<settings.dat> (rutas /work/...) eligen pista/navegación.
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
SECS="${1:-40}"; MODE="${2:-sw}"
case "$MODE" in
  sw) D3D=0 ;;
  d3d) D3D=1 ;;
  *) echo 'modo debe ser sw o d3d' >&2; exit 2 ;;
esac
export HW_ENV="${HW_ENV:-} HW_D3D=$D3D HW_INPUT_SCRIPT=${HW_RACE_SCRIPT:-/work/hotwheels/tools/race_enter.script} HW_SETTINGS_FILE=${HW_RACE_SETTINGS:-/work/hotwheels/tools/race-$MODE-settings.dat}"
if "$HW/tools/run_host.sh" "$SECS"; then rc=0; else rc=$?; fi
bash "$HW/tools/prune_out.sh" >/dev/null 2>&1 || true
exit "$rc"
