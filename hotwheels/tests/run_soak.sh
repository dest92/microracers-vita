#!/usr/bin/env bash
# Regresión del runner de soak HW-027 (sin qemu): parseo de RSS y veredicto de run.log.
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
"$HW/tools/soak.sh" --selftest
echo "SOAK: OK (selftest)"
