#!/usr/bin/env bash
# Test de tools/load_metrics.sh sobre un RUNDIR sintético (spec 024).
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
LM="$HW/tools/load_metrics.sh"
[ -f "$LM" ] || { echo "load_metrics.sh inexistente (spec 024)"; exit 1; }
bash -n "$LM"
TD="$(mktemp -d)"; trap 'rm -rf "$TD"' EXIT
# origen (run.log) en t0; frame0 en t=12; frames 1..20 en t=13..32 (1/s);
# frames 21..30 en t=50..59.  fps_10_30=(n<=30 - n<=10)/20=(19-0)/20=0.95
touch -d "@1000000000" "$TD/run.log"
touch -d "@1000000012" "$TD/frame_00000.png"
for i in $(seq 1 20); do touch -d "@$((1000000000 + 12 + i))" "$TD/frame_$(printf '%05d' $i).png"; done
for i in $(seq 21 30); do touch -d "@$((1000000000 + 29 + i))" "$TD/frame_$(printf '%05d' $i).png"; done
OUT="$("$LM" --analyze "$TD")"
echo "$OUT"
get() { grep -m1 "^$1=" <<<"$OUT" | cut -d= -f2; }
[ "$(get ttf1_s)" = "12" ] || { echo "FAIL ttf1_s=$(get ttf1_s) != 12"; exit 1; }
fps="$(get fps_10_30)"
awk -v f="$fps" 'BEGIN{exit !(f>=0.85 && f<=1.05)}' || { echo "FAIL fps_10_30=$fps !=0.95"; exit 1; }
[ "$(get frames_60)" = "31" ] || { echo "FAIL frames_60=$(get frames_60) != 31"; exit 1; }
[ "$(get frames_total)" = "31" ] || { echo "FAIL frames_total=$(get frames_total) != 31"; exit 1; }
# run sin frames -> NA, rc0
TD2="$(mktemp -d)"; touch -d "@1000000000" "$TD2/run.log"
OUT2="$("$LM" --analyze "$TD2")"
grep -q "^ttf1_s=NA$" <<<"$OUT2" || { echo "FAIL sin-frames: $OUT2"; exit 1; }
# regresión rev P1: run_host falla (rc!=0) con al menos un PNG -> RUN_FAIL,
# rc!=0 y el dir NO se borra (evidencia conservada).
FAKE="$TD/fake_run_host.sh"
cat > "$FAKE" <<'FAKEEOF'
#!/usr/bin/env bash
d="$(dirname "$0")/out/run-fail"
mkdir -p "$d"
touch "$d/run.log"
touch "$d/frame_00000.png"
echo "== $d"
exit 1
FAKEEOF
chmod +x "$FAKE"
CSV="$HW/out/lm-test-$$.csv"
rc=0
HW_RUN_HOST="$FAKE" "$LM" 1 sw > "$TD/invocation.log" 2>&1 || rc=$?
[ "$rc" != 0 ] || { echo "FAIL run_host fallido no propago rc (falso verde)"; exit 1; }
CSVFILE="$(grep -m1 '^CSV=' "$TD/invocation.log" | cut -d= -f2)"
[ -f "$CSVFILE" ] || { echo "FAIL sin CSV"; exit 1; }
grep -q "RUN_FAIL" "$CSVFILE" || { echo "FAIL fila RUN_FAIL ausente: $(cat "$CSVFILE")"; exit 1; }
[ -d "$TD/out/run-fail" ] || { echo "FAIL dir de corrida fallida borrado"; exit 1; }
rm -f "$CSVFILE"
# regresión rev P2: run OK sin PNGs -> SIN_FRAMES; rc1 sólo con gate.
FAKE2="$TD/fake_run_host_nof.sh"
cat > "$FAKE2" <<'FAKE2EOF'
#!/usr/bin/env bash
d="$(dirname "$0")/out/run-nof"
mkdir -p "$d"; touch "$d/run.log"; echo "== $d"; exit 0
FAKE2EOF
chmod +x "$FAKE2"
rc=0; HW_RUN_HOST="$FAKE2" "$LM" 1 sw > "$TD/inv2.log" 2>&1 || rc=$?
[ "$rc" = 0 ] || { echo "FAIL sin frames y sin gate devolvio rc=$rc"; exit 1; }
CSVFILE="$(grep -m1 '^CSV=' "$TD/inv2.log" | cut -d= -f2)"
grep -q ",SIN_FRAMES$" "$CSVFILE" || { echo "FAIL fila SIN_FRAMES ausente: $(cat "$CSVFILE")"; exit 1; }
[ -d "$TD/out/run-nof" ] || { echo "FAIL dir SIN_FRAMES borrado"; exit 1; }
rm -f "$CSVFILE"
rc=0; HW_RUN_HOST="$FAKE2" LOAD_FPS_MIN=0.05 "$LM" 1 sw > "$TD/inv3.log" 2>&1 || rc=$?
[ "$rc" != 0 ] || { echo "FAIL SIN_FRAMES con gate no fallo"; exit 1; }
CSVFILE="$(grep -m1 '^CSV=' "$TD/inv3.log" | cut -d= -f2)"; rm -f "$CSVFILE"
echo "load_metrics: PASS"
