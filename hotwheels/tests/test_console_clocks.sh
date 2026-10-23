#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/hotwheels/src/runtime_vita.cpp"
for sym in \
  scePowerSetArmClockFrequency scePowerSetBusClockFrequency \
  scePowerSetGpuClockFrequency scePowerSetGpuXbarClockFrequency \
  scePowerGetArmClockFrequency scePowerGetBusClockFrequency \
  scePowerGetGpuClockFrequency scePowerGetGpuXbarClockFrequency; do
  grep -qF -- "$sym" "$SRC" || { echo "FAIL: falta $sym en init Vita" >&2; exit 1; }
done
grep -qE -- 'clocks request arm=444 bus=222 gpu=222 xbar=166.*effective arm=' "$SRC" || {
  echo "FAIL: init Vita no registra clocks efectivos" >&2
  exit 1
}
echo "PASS: init Vita solicita y registra ARM/bus/GPU/xbar efectivos"
