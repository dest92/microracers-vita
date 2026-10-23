#!/usr/bin/env bash
# tools/ab_cpu_frame.sh — A/B de rendimiento en qemu con métrica limpia (opus, 2026-10-03).
#
# Por qué: frames y tiempo de pared NO sirven en qemu. El guest gira en
# timerWaitFrame40Hz (QPC) con la holgura y la carga del host mueve la pared.
# Con HW_QPC_IDLE_US el giro duerme, y los ms de CPU del proceso qemu por frame
# miden sólo trabajo real (ver hotwheels/docs/hle-prep.md).
#
# uso: ab_cpu_frame.sh <segs> <patas_por_modo> "<ENV_A>" "<ENV_B>"
#   ENV_*: "K=V K2=V2" (se suman a HW_QPC_IDLE_US=300 y race_enter.script)
#   AB_TREE=<dir dentro del repo>  árbol a compilar (default: el repo; usar un
#                                  worktree para aislarse de WIP ajeno)
#   AB_BASE_ENV="..."              env común extra
#   AB_PRE_A / AB_PRE_B="cmd"      comando previo a cada pata (A/B de CÓDIGO:
#                                  p.ej. copiar la variante de un .c; el build
#                                  incremental recompila por mtime)
# Salida: una línea por pata y un resumen "A=media±desv B=media±desv delta=%".
#
# Locale: la métrica es un número que consume luego `float()`; con LC_NUMERIC=es_AR
# `awk printf`/TIMEFORMAT escriben "10,000" y el agregado revienta (BUG-052). El
# script fija C para que salida y parseo sean independientes del locale del usuario.
export LC_ALL=C
set -euo pipefail
SECS=${1:?segs}; LEGS=${2:?patas}; ENV_A=${3-}; ENV_B=${4-}
HW="$(cd "$(dirname "$0")/.." && pwd)"; ROOT="$(cd "$HW/.." && pwd)"
TREE="$(cd "${AB_TREE:-$ROOT}" && pwd)"
case "$TREE" in "$ROOT"*) REL="${TREE#$ROOT}"; REL="${REL#/}";; *) echo "AB_TREE debe estar dentro de $ROOT" >&2; exit 2;; esac
REL_HW="${REL:+$REL/}hotwheels"
GAME=hotwheels/extracted/original/Program_Files
BASE="HW_QPC_IDLE_US=300 HW_INPUT_SCRIPT=/work/hotwheels/tools/race_enter.script ${AB_BASE_ENV:-}"
run_leg() {  # $1 = env extra → imprime "frames ms_cpu_por_frame"
    local stamp out; stamp="ab-$(date +%H%M%S%N)"; out="$HW/out/$stamp"; mkdir -p "$out"
    local envargs=(); for kv in $BASE $1; do envargs+=(-e "$kv"); done
    docker run --rm --user "$(id -u):$(id -g)" "${envargs[@]}" \
      -e HW_HOST_BUILD_DIR="/work/$REL_HW/build-host" -e WINX86_DIR=/work/d2vita/D2Vita/third_party/winx86 \
      -v "$ROOT":/work -w /work hw-host-arm:24.04 bash -c "
        ulimit -c 0
        bash $REL_HW/tools/build_host.sh >/dev/null &&
        cd $GAME && TIMEFORMAT='cpu_user=%U cpu_sys=%S' && time timeout $((SECS + 60)) qemu-arm /work/$REL_HW/build-host/hotwheels-host \
          /work/$GAME/HotWheels.exe /work/$GAME /work/hotwheels/extracted/iso /work/hotwheels/out/$stamp $SECS" > "$out/stdout.txt" 2>&1 || true
    local f c
    f=$(grep -h "fin: " "$out/stdout.txt" | grep -o "frames=[0-9]*" | head -1 | cut -d= -f2)
    c=$(grep -ho "cpu_user=[0-9.]* cpu_sys=[0-9.]*" "$out/stdout.txt" | sed 's/cpu_user=//; s/cpu_sys=//')
    if [ -z "$f" ] || [ -z "$c" ]; then echo "ERROR $out"; return; fi  # se conserva para debug
    echo "$f $(echo "$c $f" | awk '{printf "%.3f", ($1+$2)*1000/$3}')"
    rm -rf "$out"
}
A=(); B=()
for i in $(seq 1 "$LEGS"); do
    leg_a=; leg_b=
    for m in A B; do
        envx=$ENV_A; [ $m = B ] && envx=$ENV_B
        pre=${AB_PRE_A:-}; [ $m = B ] && pre=${AB_PRE_B:-}
        [ -n "$pre" ] && bash -c "$pre"
        r=$(run_leg "$envx"); load=$(cut -d' ' -f1 /proc/loadavg)
        echo "pata $i $m: frames/ms_cpu_frame=$r load=$load"
        awk -v l="$load" 'BEGIN { exit !(l > 3.0) }' && echo "  AVISO: load>3 — el CPU/frame se infla por contención; considerar repetir"
        case "$r" in ERROR*) continue;; esac
        v=${r#* }; if [ $m = A ]; then leg_a=$v; else leg_b=$v; fi
    done
    # Keep paired arrays aligned: a failed leg invalidates this iteration's pair.
    if [ -n "$leg_a" ] && [ -n "$leg_b" ]; then A+=("$leg_a"); B+=("$leg_b"); fi
done
printf '%s\n' "${A[@]}" > "$HW/out/.ab_a"; printf '%s\n' "${B[@]}" > "$HW/out/.ab_b"
python3 - "$HW/out/.ab_a" "$HW/out/.ab_b" <<'PY'
import statistics as st, sys
a = [float(x) for x in open(sys.argv[1]) if x.strip()]
b = [float(x) for x in open(sys.argv[2]) if x.strip()]
def s(v): return f"{st.mean(v):.2f}±{(st.stdev(v) if len(v) > 1 else 0):.2f} (n={len(v)})"
if a and b:
    n = min(len(a), len(b))
    paired = sorted(100 * (b[i] - a[i]) / a[i] for i in range(n))  # patas intercaladas → pares
    print(f"RESUMEN ms_cpu/frame A={s(a)} B={s(b)} delta_medias={100 * (st.mean(b) - st.mean(a)) / st.mean(a):+.1f}% "
          f"delta_pareado_mediana={st.median(paired):+.1f}% pares={['%+.1f' % d for d in paired]}")
else:
    print("RESUMEN incompleto (patas con ERROR)")
PY
rm -f "$HW/out/.ab_a" "$HW/out/.ab_b"
