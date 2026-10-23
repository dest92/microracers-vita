#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
WX86="${WINX86_DIR:-$HW/../d2vita/D2Vita/third_party/winx86}"
OUT="$(mktemp -d /tmp/hw-idiv32-box86.XXXXXX)"
cleanup() { local rc=$?; if [ "$rc" -eq 0 ]; then rm -rf -- "$OUT"; else echo "OUT=$OUT" >&2; fi; }
trap cleanup EXIT
SOURCE=third_party/box86-dynarec/emu/x86primop.c
mkdir -p "$OUT/winx86/$(dirname "$SOURCE")"
git -C "$WX86" show "HEAD:$SOURCE" > "$OUT/winx86/$SOURCE"

build() {
  local source="$1" binary="$2"
  gcc -std=gnu17 -O2 -DDYNAREC -DARM -DUSE_MMAP -DTRACE_MEMSTAT -ffunction-sections \
    -I"$WX86/third_party/box86-dynarec/emu" -I"$WX86/src/dynarec86/shim" \
    -I"$WX86/src/dynarec86" -I"$WX86/third_party/box86-dynarec/include" \
    -I"$WX86/third_party/box86-dynarec" -I"$WX86/third_party/box86-dynarec/dynarec" \
    -c "$source" -o "$OUT/x86primop.o"
  gcc -std=gnu17 -DDYNAREC -I"$WX86/third_party/box86-dynarec/emu" \
    -I"$WX86/third_party/box86-dynarec/include" -I"$WX86/third_party/box86-dynarec" \
    -ffunction-sections -Wl,--gc-sections "$HW/tests/test_idiv32_box86.c" \
    "$OUT/x86primop.o" -o "$binary"
}

patch -s -p1 -d "$OUT/winx86" < "$HW/patches/winx86-0007-div32-fast.patch"
build "$OUT/winx86/$SOURCE" "$OUT/baseline"
set +e
"$OUT/baseline"
baseline_rc=$?
set -e
if [ "$baseline_rc" -ne 1 ]; then
  echo "FAIL: se esperaba reproducir el #DE espurio de winx86-0007; rc=$baseline_rc" >&2
  exit 1
fi
echo "PASS: reprodujo #DE espurio de winx86-0007 para cociente INT32_MIN"

patch --batch --forward -p1 -d "$OUT/winx86" < "$HW/patches/winx86-0008-idiv32-min-quotient.patch"
build "$OUT/winx86/$SOURCE" "$OUT/fixed"
"$OUT/fixed"
