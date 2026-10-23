#!/usr/bin/env bash
# tools/arm_insn_measure.sh <segs> <dir> [ENV_EXTRA] — instrucciones ARM ejecutadas.
#
# Corre HotWheels.exe bajo qemu-arm y cuenta las instrucciones ARM que correría
# la A9 (runtime + bloques del dynarec). Mide TRABAJO, no tiempo: el número es el
# mismo en cualquier host, así que sobrevive la lentitud de qemu.
#   <dir>      ruta relativa al repo (p.ej. hotwheels/out/arm-insn-race)
#   ENV_EXTRA  "K=V K2=V2" se suman al entorno del guest
#   AI_TREE    árbol hotwheels a compilar (default el repo; usar un worktree)
#   AI_WX86    árbol winx86 a compilar (default el del repo)
#   AI_MODE    trace (default) | blk
#     trace: qemu -d nochain,exec,in_asm + tools/qemu_insn_count.py (exacto, pero
#            frena y PERTURBA al guest ~200x: no alcanza para llegar a una carrera)
#     blk:   contador del dynarec (parche winx86-0015) con WX86_CHAIN=0, sin trace:
#            corre a velocidad casi normal, sirve para la carrera. Salida blk-insns.
set -euo pipefail
ulimit -c 0
SECS=${1:?segs}; DIR=${2:?dir}; EXTRA=${3-}
MODE=${AI_MODE:-trace}
HW="$(cd "$(dirname "$0")/.." && pwd)"; ROOT="$(cd "$HW/.." && pwd)"
TREE="$(cd "${AI_TREE:-$ROOT}" && pwd)"
case "$TREE" in "$ROOT"*) REL="${TREE#$ROOT}"; REL="${REL#/}";; *) echo "AI_TREE debe estar dentro de $ROOT" >&2; exit 2;; esac
REL_HW="${REL:+$REL/}hotwheels"
WX="${AI_WX86:-$ROOT/d2vita/D2Vita/third_party/winx86}"
case "$WX" in "$ROOT"*) WX_W="/work/${WX#$ROOT/}";; *) echo "AI_WX86 debe estar dentro de $ROOT" >&2; exit 2;; esac
case "$MODE" in
  trace) RUN='qemu-arm -d nochain,exec,in_asm /work/'"$REL_HW"'/build-host/hotwheels-host \
           /work/'"$GAME"'/HotWheels.exe /work/'"$GAME"' /work/hotwheels/extracted/iso /work/'"$DIR"' '"$SECS"' \
           2>&1 1>/work/'"$DIR"'/stdout.txt | python3 /work/'"$REL_HW"'/tools/qemu_insn_count.py --per-tb - \
             > /work/'"$DIR"'/tb.txt 2> /work/'"$DIR"'/parse.err'; RES='qemu-insn';;
  blk)   ENVARGS_MODE=(-e WX86_CHAIN=0 -e HW_BLK_INSNS=1)
         RUN='qemu-arm /work/'"$REL_HW"'/build-host/hotwheels-host \
           /work/'"$GAME"'/HotWheels.exe /work/'"$GAME"' /work/hotwheels/extracted/iso /work/'"$DIR"' '"$SECS"' \
           >/work/'"$DIR"'/stdout.txt 2>&1'; RES='blk-insns';;
  *) echo "AI_MODE debe ser trace o blk" >&2; exit 2;;
esac
IMAGE=hw-host-arm:24.04
mkdir -p "$ROOT/$DIR"
GAME=hotwheels/extracted/original/Program_Files
ENVARGS=("${ENVARGS_MODE[@]:-}")
for kv in ${EXTRA}; do ENVARGS+=(-e "$kv"); done
docker run --rm --user "$(id -u):$(id -g)" "${ENVARGS[@]}" \
  -e "HW_HOST_BUILD_DIR=/work/$REL_HW/build-host" -e "WINX86_DIR=$WX_W" \
  -e HW_QPC_IDLE_US=300 -e "HW_INPUT_SCRIPT=/work/$REL_HW/tools/race_enter.script" \
  -v "$ROOT":/work -w /work "$IMAGE" bash -c "
    ulimit -c 0
    bash $REL_HW/tools/build_host.sh >/dev/null &&
    cd $GAME && timeout $((SECS + 300)) $RUN
  " >/dev/null 2>&1 || true
# `2>&1 1>file |`: el stderr de qemu (el trace, que puede ser de GB) va al contador en
# streaming y el stdout del programa a stdout.txt; el shell espera al contador.
FRAMES=$( { grep -ho "frames=[0-9]*" "$DIR/stdout.txt" 2>/dev/null | head -1 | cut -d= -f2; } || true )
echo "== $DIR (modo $MODE)"; grep -h "$RES:" "$DIR/stdout.txt" "$DIR/tb.txt" 2>/dev/null || echo "(sin conteo)"; echo "== frames=$FRAMES"
[ -s "$DIR/parse.err" ] && { echo "== parse.err"; cat "$DIR/parse.err"; } || true
