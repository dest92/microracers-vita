#!/usr/bin/env bash
# wine_run.sh <segundos> <out_dir> [eventos xkeys] — HW-067: Wine x86 de referencia headless.
# Corre HotWheels.exe del prefijo hotwheels/wineprefix bajo Xvfb DENTRO del contenedor vita3k-headless
# (no toca el escritorio del usuario; escritorio virtual de Wine porque Xvfb no cambia de modo), inyecta teclas XTest ('seg:keysym:dur,...') y captura la pantalla
# cada HW_WINE_SHOT_EVERY segundos (default 2) en <out_dir>/shot_NNNN.png (NNNN = segundos*10).
# Env: HW_WINE_SETTINGS=<settings.dat> (se copia al prefijo y se restaura al salir). Las corridas sobre un mismo
#      prefijo se serializan con flock: Wine lee settings.dat durante toda la corrida, no sólo al copiarlo.
#      HW_WINE_PREFIX (default hotwheels/wineprefix) y HW_WINE_DOCKER (default docker) existen para tests/test_wine_run.sh.
# Sale != 0 si el contenedor falla, si el juego murió antes del final o si no hubo capturas.
# HW_WINE_DEBUG=<canales> pisa WINEDEBUG (default -all); <out_dir>/windows.txt = árbol de ventanas al final.
# Identidad de pista (SPEC-046 CA1b): <out_dir>/opens.log = aperturas inotify en DATA/{WAVE,SLUMBER,OFFICE}
#   (WINEDEBUG=+file frena tanto al juego que no pasa de la intro).
set -euo pipefail
ulimit -c 0
SECS="${1:?segundos}"; OUT="$(realpath -m "${2:?out_dir}")"; EVENTS="${3:-}"
HW="$(cd "$(dirname "$0")/../.." && pwd)"; ROOT="$(cd "$HW/.." && pwd)"
OPT="$(readlink -f "$ROOT/shared/opt")"
PREFIX="${HW_WINE_PREFIX:-$HW/wineprefix}"
GAME="$PREFIX/drive_c/Games/Hot Wheels - Micro Racers"
EVERY="${HW_WINE_SHOT_EVERY:-2}"
mkdir -p "$OUT"
case "$OUT" in "$ROOT"/*) ;; *) echo "out_dir debe estar en el repo" >&2; exit 2;; esac
SET="$GAME/DATA/MENU/settings.dat"; BAK="$OUT/.settings.bak"
exec 9>"$PREFIX/.wine_run.lock"
flock 9
cp "$SET" "$BAK"
trap 'cp "$BAK" "$SET"; rm -f "$BAK"' EXIT
[ -n "${HW_WINE_SETTINGS:-}" ] && cp "$HW_WINE_SETTINGS" "$SET"
if "${HW_WINE_DOCKER:-docker}" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT":"$ROOT" -v "$OPT":"$OPT":ro \
  --label hw_run=wine-opus-gfx vita3k-headless:26.10-xtest2 bash -c "
set -u; ulimit -c 0
gcc -O2 -o /tmp/xkeys '$HW/tools/wine/xkeys.c' -lX11 -lXtst || exit 3
gcc -O2 -o /tmp/fwatch '$HW/tools/wine/fwatch.c' || exit 3
/tmp/fwatch '$GAME'/DATA/WAVE '$GAME'/DATA/SLUMBER '$GAME'/DATA/OFFICE > '$OUT/opens.log' 2>&1 &
Xvfb :97 -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
for i in \$(seq 50); do DISPLAY=:97 xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
export DISPLAY=:97 WINEPREFIX='$PREFIX' PATH='$OPT/wine-11.18-amd64-wow64/bin':\$PATH
export WINEDLLOVERRIDES='mscoree,mshtml=d;ddraw,d3d8,d3d9=b' WINEDEBUG='${HW_WINE_DEBUG:--all}'
cd '$GAME'
wine explorer /desktop=hw,1024x768 HotWheels.exe > '$OUT/wine.log' 2>&1 &
[ -n '$EVENTS' ] && /tmp/xkeys '$EVENTS' > '$OUT/keys.log' 2>&1 &
t=0; while [ \$t -le \$(( $SECS * 10 )) ]; do
  import -window root \"\$(printf '$OUT/shot_%04d.png' \$t)\" 2>/dev/null
  sleep $EVERY; t=\$(( t + $EVERY * 10 ))
done
xwininfo -root -tree > '$OUT/windows.txt' 2>&1
alive=0; pgrep -x HotWheels.exe >/dev/null || alive=4
wineserver -k 2>/dev/null; sleep 1
exit \$alive
"; then rc=0; else rc=$?; fi
shots=$(find "$OUT" -maxdepth 1 -name 'shot_*.png' | wc -l)
echo "$shots"
[ "$rc" = 0 ] || { echo "wine_run: contenedor rc=$rc (4 = el juego murió antes del final)" >&2; exit "$rc"; }
[ "$shots" -gt 0 ] || { echo "wine_run: sin capturas" >&2; exit 1; }
