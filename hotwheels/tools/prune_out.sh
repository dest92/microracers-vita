#!/usr/bin/env bash
# Política de disco (AGENTS.md §Disco). Idempotente; corre cada 30 min en segundo plano.
# - Corridas sin cita en git: se conservan sólo las 3 más recientes (debug de la última falla).
# - Corridas citadas en git: se reducen a evidencia mínima (logs/txt/md + 3 PNG: primero, medio, último; sin WAV).
# - Core dumps de qemu: siempre se borran.
set -e
# HW_PRUNE_ROOT: raíz inyectable para tests (sandbox).
ROOT="${HW_PRUNE_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"; cd "$ROOT"
KEEP=${KEEP:-3}
# git grep puede no existir fuera del repo (sandbox de test) => refs vacías.
refs=$(git grep -ohE '(run|hotwheels-vita)-2026[0-9]{4}-[0-9]{6}' 2>/dev/null | sort -u || true)
# Smoke en curso: lock joven (<30 min) => nunca rm -rf de smoke-*.d (bug tanda36:
# prune borró el LOGDIR de un smoke activo y su etapa2/3 falló con ENOENT).
keep_smoke=0
if [ -f hotwheels/out/.smoke-active ]; then
  lock_age=$(( $(date +%s) - $(stat -c %Y hotwheels/out/.smoke-active) ))
  if [ "$lock_age" -lt 1800 ]; then keep_smoke=1; fi
fi
find hotwheels \( -name 'qemu_*.core' -o -name '*.psp2dmp' \) -type f -delete 2>/dev/null || true
find hotwheels/extracted -maxdepth 4 -name core -type f -delete 2>/dev/null || true
shrink(){ local d=$1; find "$d" -type f \( -name '*.wav' -o -name '*.raw' -o -name '*.bin' \) -delete
  mapfile -t p < <(ls -1 "$d"/*.png 2>/dev/null | sort); n=${#p[@]}
  [ "$n" -gt 3 ] && for i in "${!p[@]}"; do [ "$i" -eq 0 ] || [ "$i" -eq $((n/2)) ] || [ "$i" -eq $((n-1)) ] || rm -f "${p[$i]}"; done; true; }
for base in hotwheels/out hotwheels/build-vita3k; do
  [ -d "$base" ] || continue
  i=0; ls -1dt "$base"/*-2026????-??????* 2>/dev/null | while read -r d; do
    n=$(basename "$d")
    # protección de corrida activa: nada con mtime <3 min se toca.
    d_age=$(( $(date +%s) - $(stat -c %Y "$d") ))
    if [ "$d_age" -lt 180 ]; then continue; fi
    i=$((i+1))
    ref=$(printf '%s\n' "$n" | sed -E 's/^((run|hotwheels-vita)-[0-9]{8}-[0-9]{6}).*$/\1/')
    if echo "$refs" | grep -qx "$ref"; then shrink "$d"
    elif [ $i -gt "$KEEP" ]; then
      case "$n" in
        smoke-*) [ "$keep_smoke" = 1 ] || rm -rf -- "$d" ;;
        *) rm -rf -- "$d" ;;
      esac
    fi
  done
done
du -sh hotwheels/out hotwheels/build-vita3k 2>/dev/null || true
