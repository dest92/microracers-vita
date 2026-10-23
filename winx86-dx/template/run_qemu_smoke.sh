#!/usr/bin/env bash
# Build a tiny PE32 guest with llvm-mingw, then load/run it under qemu-arm.
set -euo pipefail
ulimit -c 0

TEMPLATE="$(cd "$(dirname "$0")" && pwd)"
DX="$(cd "$TEMPLATE/.." && pwd)"
ROOT="$(cd "$DX/.." && pwd)"
HW_OUT="$ROOT/hotwheels/out"
WX86="${WINX86_DIR:-$ROOT/d2vita/D2Vita/third_party/winx86}"
WX86="$(cd "$WX86" 2>/dev/null && pwd)" || {
  echo "FAIL: winx86 no encontrado: ${WINX86_DIR:-$ROOT/d2vita/D2Vita/third_party/winx86}" >&2
  exit 2
}
LLVM_ROOT="${LLVM_MINGW_ROOT:-}"
if [ -z "$LLVM_ROOT" ]; then
  LLVM_OPT="$(readlink -f "$ROOT/shared/opt")"
  LLVM_ROOT="$(find "$LLVM_OPT" -maxdepth 1 -type d -name 'llvm-mingw-*' -print -quit)"
fi
MINGW_BIN="${LLVM_MINGW_BIN:-$LLVM_ROOT/bin}"
IMAGE="${HW_HOST_ARM_IMAGE:-hw-host-arm:24.04}"
LOCK="$HW_OUT/BUILD_HOST_BUSY"
mkdir -p "$HW_OUT"

OUT="$(mktemp -d "$HW_OUT/winx86-dx-template.XXXXXX")"
LOCKED=0
PASSED=0
SKIPPED=0
cleanup() {
  local rc=$?
  if [ "$LOCKED" = 1 ]; then
    python3 - "$LOCK" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).unlink(missing_ok=True)
PY
  fi
  if [ "$PASSED" = 1 ] || [ "$SKIPPED" = 1 ]; then
    python3 - "$OUT" <<'PY'
from pathlib import Path
import shutil, sys
shutil.rmtree(sys.argv[1], ignore_errors=True)
PY
  else
    echo "FAIL rc=$rc; OUT=$OUT" >&2
  fi
}
trap cleanup EXIT

if [ ! -x "$MINGW_BIN/i686-w64-mingw32-gcc" ]; then
  echo "SKIP: llvm-mingw i686 no encontrado; definir LLVM_MINGW_ROOT" >&2
  SKIPPED=1
  exit 77
fi
if ! command -v docker >/dev/null 2>&1 || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "SKIP: falta la imagen Docker $IMAGE (ver hotwheels/tools/host/Dockerfile)" >&2
  SKIPPED=1
  exit 77
fi
if [ ! -x "$WX86/build.sh" ]; then
  echo "FAIL: winx86 no encontrado en $WX86" >&2
  exit 2
fi
if ! python3 - "$LOCK" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
try:
    with p.open('x') as f: f.write('dxlib-template\n')
except FileExistsError:
    print('SKIP: BUILD_HOST_BUSY ya está tomado', file=sys.stderr)
    raise SystemExit(1)
PY
then
  SKIPPED=1
  exit 77
fi
LOCKED=1

"$MINGW_BIN/i686-w64-mingw32-gcc" -O1 -ffreestanding -fno-stack-protector \
  -nostdlib -Wl,--entry,_hello_entry@0 -Wl,--subsystem,console -Wl,--image-base,0x10000000 \
  -o "$OUT/hello.exe" "$TEMPLATE/hello_guest.c" -lddraw -ldsound -ldinput

export OUT WX86
docker run --rm --user "$(id -u):$(id -g)" -e OUT="/work/${OUT#"$ROOT"/}" \
  -e WX86_DIR=/winx86 -v "$ROOT:/work" -v "$WX86:/winx86" -w /work "$IMAGE" \
  bash -c '
    set -euo pipefail
    ulimit -c 0
    DX=/work/winx86-dx
    WX86="$WX86_DIR"
    bash "$WX86/build.sh" >/dev/null
    arm-linux-gnueabihf-g++ -std=gnu++17 -O2 -static -marm \
      -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard -DD2RT_CPU_BOX86 \
      -I"$DX/include" -I"$WX86/src" -I"$WX86/src/dynarec86" \
      -I"$WX86/src/dynarec86/shim" -I"$WX86/third_party/box86-dynarec/include" \
      -I"$WX86/third_party/box86-dynarec" \
      "$DX/template/hello_runner.cpp" \
      "$DX/src/ddraw/software.cpp" "$DX/src/dinput/dinput.cpp" \
      "$DX/src/audio/mixer.cpp" "$DX/src/audio/dsound_bus.cpp" \
      "$WX86/build-arm/libwinx86.a" -Wl,--start-group -lpthread -ldl -lm \
      -Wl,--end-group -o "$OUT/hello_runner"
    timeout 30 qemu-arm "$OUT/hello_runner" "$OUT/hello.exe"
  ' | tee "$OUT/qemu.log"
grep -Fx 'HELLO_PORT|loaded=hello.exe|ddraw=ok|dsound=ok|dinput=ok|unresolved=0' "$OUT/qemu.log" >/dev/null
echo "PASS: PE32 cargado y ejecutado bajo qemu-arm; DDraw, DSound y DInput genéricos ejercitados"
PASSED=1
