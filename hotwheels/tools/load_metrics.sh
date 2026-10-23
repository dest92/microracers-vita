#!/usr/bin/env bash
# tools/load_metrics.sh [runs=3] [sw|d3d] — spec 024: métricas de carga en qemu.
# --analyze RUNDIR: calcula ttf1_s, fps_10_30, frames_60, frames_total,
# duration_s (origen = mtime de run.log). Nunca truena ante dirs incompletos.
set -uo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"

analyze() {
  local d="$1" origin f first t tf1=NA n10=0 n30=0 n60=0 total=0 last=0 bn
  # origen = instante de creación del run, parseado del STAMP del nombre
  # (run-YYYYmmdd-HHMMSS...). El mtime de run.log NO sirve: crece con cada
  # escritura y daba ttf1 negativo. Dirs sintéticos: fallback al mtime.
  bn="$(basename "$d")"
  if [[ "$bn" =~ ^run-([0-9]{4})([0-9]{2})([0-9]{2})-([0-9]{2})([0-9]{2})([0-9]{2}) ]]; then
    origin=$(date -d "${BASH_REMATCH[1]}-${BASH_REMATCH[2]}-${BASH_REMATCH[3]} ${BASH_REMATCH[4]}:${BASH_REMATCH[5]}:${BASH_REMATCH[6]}" +%s 2>/dev/null) || origin=""
  fi
  [ -n "${origin:-}" ] || origin=$(stat -c %Y "$d/run.log" 2>/dev/null) || { echo "error=sin_origen"; return 0; }
  first=$(ls "$d"/frame_*.png 2>/dev/null | sort | head -1)
  if [ -z "$first" ]; then
    printf 'ttf1_s=NA\nfps_10_30=0\nframes_60=0\nframes_total=0\nduration_s=0\n'
    return 0
  fi
  tf1=$(($(stat -c %Y "$first") - origin))
  for f in "$d"/frame_*.png; do
    t=$(($(stat -c %Y "$f") - origin))
    total=$((total + 1))
    [ "$t" -gt "$last" ] && last=$t
    [ "$t" -le 10 ] && n10=$((n10 + 1))
    [ "$t" -le 30 ] && n30=$((n30 + 1))
    [ "$t" -le 60 ] && n60=$((n60 + 1))
  done
  printf 'ttf1_s=%d\nfps_10_30=%s\nframes_60=%d\nframes_total=%d\nduration_s=%d\n' \
    "$tf1" "$(awk -v a="$n30" -v b="$n10" 'BEGIN{printf "%.2f",(a-b)/20}')" \
    "$n60" "$total" "$last"
}

if [ "${1:-}" = "--analyze" ]; then
  analyze "${2:-}"
  exit 0
fi

RUNS="${1:-3}"; MODE="${2:-sw}"
STAMP="$(date +%Y%m%d-%H%M%S%N)-$$"
CSV="$HW/out/load-metrics-$STAMP.csv"
mkdir -p "$HW/out"
echo "run,ttf1_s,fps_10_30,frames_60,frames_total,duration_s,verdict" > "$CSV"
export HW_INPUT_SCRIPT="${HW_INPUT_SCRIPT:-/work/hotwheels/tools/race_enter.script}"
export HW_KEEP_OUT=1   # run_host borra el dir en PASS; acá se necesita para analizar
RUN_HOST="${HW_RUN_HOST:-$HW/tools/run_host.sh}"  # inyectable p/ tests
T1=(); FPS=(); HARDFAIL=0
for i in $(seq 1 "$RUNS"); do
  rm -f "$HW/out/run_dir"
  # rc de run_host ES evidencia: docker/qemu o smoke fallido ⇒ RUN_FAIL.
  # sin esto un fallo con PNGs quedaba OK y se borraba (falso verde, rev P1).
  rc=0
  out="$("$RUN_HOST" 60 "$MODE" 2>&1)" || rc=$?
  rundir="$(grep -m1 '^== .*out/run-' <<<"$out" | sed 's/^== //')"
  line="run$i"; verdict=OK
  if [ "$rc" -ne 0 ]; then verdict=RUN_FAIL; HARDFAIL=1; fi
  if [ -z "$rundir" ] || [ ! -d "$rundir" ]; then
    [ "$verdict" = OK ] && verdict=SIN_RUN
    HARDFAIL=1
    line="$line,NA,NA,0,0,0,$verdict"
  else
    a="$(analyze "$rundir")"
    t="$(grep -m1 '^ttf1_s=' <<<"$a" | cut -d= -f2)"
    fp="$(grep -m1 '^fps_10_30=' <<<"$a" | cut -d= -f2)"
    f6="$(grep -m1 '^frames_60=' <<<"$a" | cut -d= -f2)"
    ft="$(grep -m1 '^frames_total=' <<<"$a" | cut -d= -f2)"
    du="$(grep -m1 '^duration_s=' <<<"$a" | cut -d= -f2)"
    if [ "$verdict" = OK ]; then
      if [ -n "${LOAD_TTF1_MAX_S:-}" ] && [ "$t" != NA ] && [ "${t%.*}" -gt "$LOAD_TTF1_MAX_S" ]; then verdict=TTF1_SOBREPASADO; HARDFAIL=1; fi
      if [ -n "${LOAD_FPS_MIN:-}" ] && ! awk -v f="$fp" -v m="$LOAD_FPS_MIN" 'BEGIN{exit !(f>=m)}'; then verdict=FPS_BAJO; HARDFAIL=1; fi
      # SIN_FRAMES sólo es fallo con gate (spec 024, criterio 2); sin gate
      # es reporte y el dir se conserva (la política de borrado exige OK).
      if [ "$ft" = 0 ]; then
        verdict=SIN_FRAMES
        [ -n "${LOAD_TTF1_MAX_S:-}${LOAD_FPS_MIN:-}" ] && HARDFAIL=1
      fi
    fi
    line="$line,$t,$fp,$f6,$ft,$du,$verdict"
    # política efímera: sólo un run OK se borra tras analizar; RUN_FAIL conserva
    # su dir como evidencia del fallo.
    [ "$verdict" = OK ] && rm -rf -- "$rundir"
    if [ "$verdict" = OK ]; then
      [ "$t" != NA ] && T1+=("$t")
      [ -n "$fp" ] && FPS+=("$fp")
    fi
  fi
  echo "$line" >> "$CSV"
done
agg() { # lista -> MIN MAX MED por stdout
  [ "$#" -gt 0 ] || { echo "NA NA NA"; return; }
  printf '%s\n' "$@" | sort -n | awk 'NR==1{min=$1} {a[NR]=$1} END{med=a[int((NR+1)/2)]; printf "%s %s %s\n", min, a[NR], med}'
}
read -r tmin tmax tmed <<<"$(agg "${T1[@]:-}")"
read -r fmin fmax fmed <<<"$(agg "${FPS[@]:-}")"
echo "AGG TTF1_MIN=$tmin TTF1_MAX=$tmax TTF1_MED=$tmed"
echo "AGG FPS_MIN=$fmin FPS_MAX=$fmax FPS_MED=$fmed"
echo "CSV=$CSV"
exit "$HARDFAIL"
