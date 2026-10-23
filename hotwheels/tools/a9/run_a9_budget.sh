#!/usr/bin/env bash
# HW-073 (perf-host) SPEC-054 CA2/CA3: mide instrucciones dinámicas por frame de
# salida del kernel mix_voice del ds_emul, por variante, con qemu-arm.
#
# Método: -singlestep hace un TB por instrucción, así que cada "Trace" de -d exec
# es una instrucción retirada. Se corren DOS longitudes (F y 2F) y se resta para
# aislar el costo por frame del arranque/teardown: insn_frame = (I2F - IF)/F.
# Esto son INSTRUCCIONES, no ciclos A9 (el modelo de ciclos vive en a9_budget.py).
#
# Requisitos: docker con la imagen hw-host-arm:24.04 (trae arm-linux-gnueabihf-g++
# y qemu-arm). No usa sudo; ulimit -c 0.
#
# Env:
#   A9_VARIANTS   dir con {linear,cubic,sinc8,sinc16}/runtime/ds_resample.h
#                 (default hotwheels/out/hw069-variants; se regenera si falta)
#   A9_WINX86     dir del kernel de producción (con src/runtime/ds_resample.h)
#   A9_FRAMES     frames de la corrida corta (default 400)
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/../.." && pwd)"
ROOT="$(cd "$HW/.." && pwd)"
FRAMES="${A9_FRAMES:-400}"
VARIANTS="${A9_VARIANTS:-$HW/out/hw069-variants}"
WX86="${A9_WINX86:-$ROOT/d2vita/D2Vita/third_party/winx86}"
IMAGE=hw-host-arm:24.04
BENCH="$HW/tools/a9/resample_bench.cpp"
HOISTED="$HW/tools/a9/sinc16_hoisted.h"
SCRATCH="$HW/out/a9-budget"
mkdir -p "$SCRATCH"
# Rutas vistas DESDE el contenedor: ROOT se monta en /work.
rel() { local p="$1"; printf '/work%s' "${p#"$ROOT"}"; }
BENCH_C="$(rel "$BENCH")"; WX86_C="$(rel "$WX86")"; VARIANTS_C="$(rel "$VARIANTS")"

# 1) Variantes sinc: reusar las de cc-audio o regenerarlas con SU tool (sin duplicar).
if [ ! -f "$VARIANTS/sinc16/runtime/ds_coefficients.h" ]; then
  echo "regenerando variantes en $VARIANTS desde $WX86 ..." >&2
  PYTHONPATH="$HW/tools" python3 - "$WX86" "$VARIANTS" <<'PY'
import sys
from pathlib import Path
import dsound_resampler_variants as gen
wx86, out = Path(sys.argv[1]), Path(sys.argv[2])
base = (wx86 / "src/runtime/ds_resample.h").read_text()
for kind in ("linear", "cubic", "sinc8", "sinc16"):
    dst = out / kind
    (dst / "runtime").mkdir(parents=True, exist_ok=True)
    (dst / "runtime/ds_resample.h").write_text(base if kind == "linear" else gen.header(base, kind, dst))
PY
fi
# Prototipo hoisted: reusa la tabla del sinc16 y el header propio.
mkdir -p "$SCRATCH/hoisted"
cp "$VARIANTS/sinc16/runtime/ds_coefficients.h" "$SCRATCH/hoisted/ds_coefficients.h"
cp "$HOISTED" "$SCRATCH/hoisted/ds_resample.h"

# 2) Lógica del contenedor en un script montado (evita el infierno de comillas).
cat > "$SCRATCH/measure.sh" <<SH
set -e
CXX='arm-linux-gnueabihf-g++ -std=gnu++17 -O2 -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard'
measure() { # \$1=tag  \$2=includedir
  \$CXX -I "\$2" $BENCH_C -static -o /tmp/b_\$1
  qemu-arm -singlestep -d exec /tmp/b_\$1 $FRAMES 2>/tmp/e1 1>/tmp/o1
  qemu-arm -singlestep -d exec /tmp/b_\$1 $((2*$FRAMES)) 2>/tmp/e2 1>/dev/null
  i1=\$(grep -c 'Trace 0:' /tmp/e1); i2=\$(grep -c 'Trace 0:' /tmp/e2)
  ck=\$(cut -d' ' -f2 /tmp/o1)
  awk -v t="\$1" -v i="\$i1" -v j="\$i2" -v f="$FRAMES" -v c="\$ck" \
    'BEGIN{printf "%s %.1f %s\n", t, (j-i)/f, c}'
}
echo "kernel insn_frame checksum"
measure prod_linear $WX86_C/src/runtime
measure linear      $VARIANTS_C/linear/runtime
measure cubic       $VARIANTS_C/cubic/runtime
measure sinc8       $VARIANTS_C/sinc8/runtime
measure sinc16      $VARIANTS_C/sinc16/runtime
measure sinc16_hoisted /scratch/hoisted
SH
docker run --rm --user "$(id -u):$(id -g)" -v "$ROOT":/work -v "$SCRATCH":/scratch -w /work "$IMAGE" bash /scratch/measure.sh | tee "$SCRATCH/report.txt"

# 3) CA3: el prototipo hoisted debe ser bitexacto contra el sinc16 de producción.
p=$(awk '$1=="sinc16"{print $3}' "$SCRATCH/report.txt")
h=$(awk '$1=="sinc16_hoisted"{print $3}' "$SCRATCH/report.txt")
[ "$p" = "$h" ] || { echo "FALLA CA3: checksum sinc16=$p != hoisted=$h (semántica distinta)" >&2; exit 1; }
echo "CA3 OK: sinc16 y sinc16_hoisted bitexactos (checksum $p)"
