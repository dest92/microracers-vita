#!/usr/bin/env bash
# Uso: vita_deploy.sh [--data-only] [VPK] [carpeta-datos].
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "${1:-}" == --data-only ]]; then
  shift
  exec python3 "$HW/tools/vita_deploy.py" --data-only --vpk "${1:-$HW/build-vita/hotwheels-vita.vpk}" --data "${2:-$HW/build-vita/stage/hotwheels}"
fi
exec python3 "$HW/tools/vita_deploy.py" --vpk "${1:-$HW/build-vita/hotwheels-vita.vpk}" --data "${2:-$HW/build-vita/stage/hotwheels}"
