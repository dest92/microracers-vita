#!/usr/bin/env bash
# remote_disk_guard.sh — guarda de disco de los jobs del worker remoto (SPEC-051).
#
# Falla si la WSL supera el tope de uso o si el drive de Windows que hospeda el
# vhdx queda por debajo del mínimo libre. Son dos límites distintos y ambos
# importan: el `df /` de la WSL mide el presupuesto del ext4 dentro del vhdx y
# el `df` del drive mide el espacio físico que el usuario puede perder. Medir
# sólo el primero confunde unidades: el vhdx puede reportar cientos de GB
# libres mientras el disco de Windows está por llenarse.
#
#   HW_DISK_MAX_USED_GB  tope de GB usados en la WSL (default 60)
#   HW_DISK_MIN_FREE_GB  mínimo de GB libres en el drive de Windows (default 50)
#   HW_DISK_WIN_DRIVE    mount del drive de Windows (default /mnt/d)
#   HW_GUARD_*_KB        medidas inyectadas; sin ellas se leen de df
#
# rc 0 = dentro de límites · 1 = guarda violada · 2 = no se pudo medir
set -uo pipefail

MAX_USED_GB="${HW_DISK_MAX_USED_GB:-60}"
MIN_FREE_GB="${HW_DISK_MIN_FREE_GB:-50}"
WIN_DRIVE="${HW_DISK_WIN_DRIVE:-/mnt/d}"
GB_KB=1048576

root_used_kb="${HW_GUARD_ROOT_USED_KB:-}"
root_avail_kb="${HW_GUARD_ROOT_AVAIL_KB:-}"
win_free_kb="${HW_GUARD_WIN_FREE_KB:-}"

if [[ -z "$root_used_kb" || -z "$root_avail_kb" ]]; then
  read -r root_used_kb root_avail_kb < <(df -Pk / 2>/dev/null | awk 'NR==2 {print $3, $4}')
fi
if [[ -z "$win_free_kb" ]]; then
  win_free_kb="$(df -Pk "$WIN_DRIVE" 2>/dev/null | awk 'NR==2 {print $4}')"
fi
[[ "$root_used_kb" =~ ^[0-9]+$ && "$root_avail_kb" =~ ^[0-9]+$ ]] || {
  echo "guarda de disco: no se pudo medir el uso de la WSL (usado=${root_used_kb:-?} libre=${root_avail_kb:-?})" >&2
  exit 2
}
[[ "$win_free_kb" =~ ^[0-9]+$ ]] || {
  echo "guarda de disco: no se puede medir el drive de Windows ($WIN_DRIVE)" >&2
  exit 2
}

used_gb=$((root_used_kb / GB_KB))
avail_gb=$((root_avail_kb / GB_KB))
win_gb=$((win_free_kb / GB_KB))
printf 'disco: wsl_usado_gb=%s wsl_libre_gb=%s drive=%s drive_libre_gb=%s\n' \
  "$used_gb" "$avail_gb" "$WIN_DRIVE" "$win_gb"

if ((used_gb > MAX_USED_GB)); then
  echo "guarda de disco: la WSL usa ${used_gb} GB (tope ${MAX_USED_GB} GB); job abortado" >&2
  exit 1
fi
if ((win_gb < MIN_FREE_GB)); then
  echo "guarda de disco: el drive ${WIN_DRIVE} tiene ${win_gb} GB libres (mínimo ${MIN_FREE_GB} GB); job abortado" >&2
  exit 1
fi
exit 0
