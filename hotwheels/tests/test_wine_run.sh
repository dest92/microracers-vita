#!/usr/bin/env bash
# review-7d87219 P2#1/P2#2: wine_run.sh propaga el RC del contenedor y serializa el settings.dat del prefijo.
# El contenedor se reemplaza por un mock (HW_WINE_DOCKER) que simula la corrida sin Wine.
set -uo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d "$HW/out/test-wine-run.XXXXXX")"; trap 'rm -rf "$T"' EXIT
fails=0; fail() { echo "FAIL: $*"; fails=$((fails + 1)); }
# Prefijo temporal: el test nunca toca hotwheels/wineprefix.
export HW_WINE_PREFIX="$T/prefix"
SET="$HW_WINE_PREFIX/drive_c/Games/Hot Wheels - Micro Racers/DATA/MENU/settings.dat"
mkdir -p "$(dirname "$SET")"; printf 'original\n' > "$SET"
ORIG="$(sha256sum "$SET" | cut -d' ' -f1)"

# Mock: lee "rc sleep" de $T/mock.cfg; durante el "juego" verifica que settings.dat sea el de esta corrida.
cat > "$T/docker" <<'MOCK'
#!/usr/bin/env bash
read -r rc secs < "$MOCK_CFG"
want="$(cat "$MOCK_WANT")"
for i in $(seq "$secs"); do
  [ "$(cat "$MOCK_SET")" = "$want" ] || echo intercalado >> "$MOCK_LOG"
  sleep 0.2
done
[ "$rc" = 0 ] || [ "$MOCK_ONE_SHOT" = 1 ] && : > "$MOCK_OUT/shot_0000.png"
exit "$rc"
MOCK
chmod +x "$T/docker"
run() {  # run <id> <rc> <ticks> <one_shot>
  mkdir -p "$T/$1"; printf '%s %s\n' "$2" "$3" > "$T/$1.cfg"; printf 'settings-%s\n' "$1" > "$T/$1.dat"
  cp "$T/$1.dat" "$T/$1.want"
  MOCK_CFG="$T/$1.cfg" MOCK_WANT="$T/$1.want" MOCK_SET="$SET" MOCK_LOG="$T/intercalado.log" MOCK_OUT="$T/$1" \
  MOCK_ONE_SHOT="$4" HW_WINE_DOCKER="$T/docker" HW_WINE_SETTINGS="$T/$1.dat" \
    bash "$HW/tools/wine/wine_run.sh" 1 "$T/$1" "" > "$T/$1.out" 2>&1
}

# P2#1: contenedor sale 44 dejando una captura → el wrapper falla.
run a 44 1 1; rc=$?
[ "$rc" -ne 0 ] || fail "RC 44 del contenedor terminó en 0"
# Contenedor OK pero sin capturas → falla.
cat > "$T/docker0" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK
chmod +x "$T/docker0"
mkdir -p "$T/b"; HW_WINE_DOCKER="$T/docker0" bash "$HW/tools/wine/wine_run.sh" 1 "$T/b" "" > "$T/b.out" 2>&1 \
  && fail "sin capturas terminó en 0"

# P2#2: dos corridas intercaladas no ven el settings de la otra y el prefijo queda restaurado.
run c 0 6 0 & p1=$!; sleep 0.3; run d 0 3 0 & p2=$!
wait "$p1"; r1=$?; wait "$p2"; r2=$?
[ "$r1" = 0 ] && [ "$r2" = 0 ] || fail "corridas concurrentes rc=$r1/$r2"
[ ! -s "$T/intercalado.log" ] || fail "una corrida vio el settings.dat de la otra"
[ "$(sha256sum "$SET" | cut -d' ' -f1)" = "$ORIG" ] || fail "settings.dat del prefijo no quedó restaurado"

[ "$fails" = 0 ] && echo "test_wine_run: OK" || { echo "test_wine_run: $fails FALLOS"; exit 1; }
