#!/usr/bin/env bash
# tools/run_host.sh [segundos] — HW-007 tier 1: compila hotwheels-host y corre
# el HotWheels.exe REAL bajo qemu-arm (contenedor hw-host-arm), con
# C:\ = extracted/original/Program_Files (cwd) y D:\ = extracted/iso.
# Salida: hotwheels/out/run-<fecha>/ (run.log, frame_*.png, save/) y
# hotwheels/out/latest -> esa corrida. Variables extra al invitado: HW_ENV="A=1 B=2".
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HW/.." && pwd)"
SECS="${1:-30}"
IMAGE=hw-host-arm:24.04
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -q -t "$IMAGE" "$HW/tools/host" >/dev/null
STAMP="$(date +%Y%m%d-%H%M%S%N)-$$"
OUT="$HW/out/run-$STAMP"
mkdir -p "$OUT"
ln -sfn "run-$STAMP" "$HW/out/latest"
[ -n "${HW_RUN_DIR_FILE:-}" ] && printf '%s\n' "$OUT" > "$HW_RUN_DIR_FILE"
GAME=hotwheels/extracted/original/Program_Files
ENVARGS=(); for kv in ${HW_ENV:-}; do ENVARGS+=(-e "$kv"); done
HOST_BUILD_DIR="${HW_HOST_BUILD_DIR:-/work/hotwheels/build-host}"
[ "${HW_FRAMES_ALL:-0}" = 1 ] && ENVARGS+=(-e HW_FRAMES_ALL=1)
[ "${HW_WAV:-0}" = 1 ] && ENVARGS+=(-e HW_WAV=1)
LABELS=(); [ -n "${HW_RUN_ID:-}" ] && LABELS=(--label "hw_run=$HW_RUN_ID")
if docker run --rm "${LABELS[@]}" --user "$(id -u):$(id -g)" "${ENVARGS[@]}" \
    -e "HW_HOST_BUILD_DIR=$HOST_BUILD_DIR" -v "$ROOT":/work -w /work "$IMAGE" bash -c "
  ulimit -c 0
  bash hotwheels/tools/build_host.sh >/dev/null &&
  cd $GAME && timeout $((SECS + 20)) qemu-arm $HOST_BUILD_DIR/hotwheels-host \
    /work/$GAME/HotWheels.exe /work/$GAME /work/hotwheels/extracted/iso /work/hotwheels/out/run-$STAMP $SECS" \
  > "$OUT/stdout.txt" 2>&1; then
  DOCKER_RC=0
else
  DOCKER_RC=$?
  echo "== salida no cero: $DOCKER_RC" >> "$OUT/stdout.txt"
fi
if ! python3 "$HW/tools/finalize_frames.py" "$OUT"; then
  echo "== error: no se pudieron normalizar los PNG recientes" >> "$OUT/stdout.txt"
  DOCKER_RC=1
fi
echo "== $OUT"
echo "== frames: $(ls "$OUT"/frame_*.png 2>/dev/null | wc -l)"
echo "== missing:"; { grep -h "MISSING" "$OUT/run.log" 2>/dev/null || true; } | sed 's/^/   /' | head -40
tail -5 "$OUT/stdout.txt"
if [ "$DOCKER_RC" -eq 0 ] && bash "$HW/tools/smoke_qemu_verdict.sh" "$OUT" > "$OUT/verdict.txt"; then
  VERDICT=PASS
else
  VERDICT=FAIL
fi
cat "$OUT/verdict.txt" 2>/dev/null || true
if [ "$VERDICT" = PASS ] && [ "${HW_KEEP_OUT:-0}" != 1 ]; then
  [ "$(readlink "$HW/out/latest" 2>/dev/null || true)" = "$(basename "$OUT")" ] && rm -f "$HW/out/latest"
  rm -rf -- "$OUT"
fi
if [ "${HW_KEEP_OUT:-0}" = 1 ]; then KEEP=999 bash "$HW/tools/prune_out.sh" >/dev/null 2>&1 || true
else bash "$HW/tools/prune_out.sh" >/dev/null 2>&1 || true
fi
[ "$VERDICT" = PASS ]
