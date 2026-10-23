#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HW="$ROOT/hotwheels"
TOOLBIN="${MINGW_BIN:?set MINGW_BIN to your llvm-mingw bin directory}"
WINE="${WINE_BIN:?set WINE_BIN to your Wine executable}"
IMAGE="${HW_HOST_IMAGE:-hw-host-arm:24.04}"
OUT="$(mktemp -d /tmp/hw-x87-oracle.XXXXXX)"
KEEP=0
SOURCE_WX86="$ROOT/d2vita/D2Vita/third_party/winx86"
BUILD_WX86="$OUT/winx86"
cleanup() {
    local rc=$?
    if [ -d "$BUILD_WX86" ]; then git -C "$SOURCE_WX86" worktree remove --force "$BUILD_WX86" >/dev/null 2>&1 || true; fi
    if [ "$rc" -ne 0 ]; then KEEP=1; fi
    if [ "$KEEP" -eq 0 ]; then rm -rf -- "$OUT"; else echo "OUT=$OUT" >&2; fi
}
trap cleanup EXIT

for tool in "$TOOLBIN/i686-w64-mingw32-gcc" "$TOOLBIN/llvm-nm" "$WINE"; do
    [ -x "$tool" ] || { echo "FAIL: falta herramienta $tool" >&2; exit 2; }
done
command -v objcopy >/dev/null || { echo "FAIL: falta objcopy host" >&2; exit 2; }
command -v docker >/dev/null || { echo "FAIL: falta docker" >&2; exit 2; }

"$TOOLBIN/i686-w64-mingw32-gcc" -O2 -c "$HW/tests/x87_oracle.S" -o "$OUT/x87_oracle.o"
"$TOOLBIN/i686-w64-mingw32-gcc" -O2 "$HW/tests/x87_oracle.c" "$OUT/x87_oracle.o" -o "$OUT/x87_oracle.exe"
SYMBOL="$("$TOOLBIN/llvm-nm" -n "$OUT/x87_oracle.o" | awk '$3 == "_x87_fistp_case" { print $1 }')"
[ "$SYMBOL" = 00000000 ] || { echo "FAIL: x87 fixture no comienza en .text (offset=$SYMBOL)" >&2; exit 3; }
objcopy --only-section=.text -O binary "$OUT/x87_oracle.o" "$OUT/x87_code.bin"
WINEDEBUG=-all timeout 30s "$WINE" "$OUT/x87_oracle.exe" >"$OUT/oracle.txt" 2>"$OUT/wine.err"

git -C "$SOURCE_WX86" worktree add --detach "$BUILD_WX86" bd5d299 >/dev/null
for patch in "$HW"/patches/winx86-*.patch; do git -C "$BUILD_WX86" apply --check "$patch"; git -C "$BUILD_WX86" apply "$patch"; done

docker run --rm --user "$(id -u):$(id -g)" \
    -v "$ROOT:/work" -v "$OUT:/tmp/x87" -w /work \
    "$IMAGE" bash -lc '
set -euo pipefail
ulimit -c 0
BUILD=/tmp/x87/winx86
bash "$BUILD/build.sh" >/dev/null
arm-linux-gnueabihf-g++ -std=gnu++17 -O2 -static -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard \
  -I"$BUILD/src" -I"$BUILD/src/dynarec86" -I"$BUILD/src/dynarec86/shim" -I"$BUILD/third_party/box86-dynarec/include" \
  /work/hotwheels/tools/torture/x87_runner.cpp "$BUILD/build-arm/libwinx86.a" -lpthread -ldl -lm -o /tmp/x87/x87_runner
ID="$(cat /tmp/x87/x87_runner /tmp/x87/x87_code.bin | sha256sum | { read -r hash rest; echo "$hash"; })"
mkdir -p /tmp/x87/cache
export WX86_TCACHE=1 WX86_TCACHE_DIR=/tmp/x87/cache WX86_TCACHE_ID="$ID"
export WX86_TCACHE_FIXED_IMAGE=1 WX86_TCACHE_TARGET=qemu-arm-static
export WX86_CHAIN=0 WX86_TCACHE_CHAIN=0
export WX86_TCACHE_GUEST_BASE=0x10000000 WX86_TCACHE_GUEST_SIZE=0x1000
timeout 30 qemu-arm /tmp/x87/x87_runner /tmp/x87/x87_code.bin > /tmp/x87/runtime.cold.txt 2>/tmp/x87/cold.err
timeout 30 qemu-arm /tmp/x87/x87_runner /tmp/x87/x87_code.bin > /tmp/x87/runtime.txt 2>/tmp/x87/warm.err
diff -u /tmp/x87/runtime.cold.txt /tmp/x87/runtime.txt
grep -Eq "\\[tcache\\] stats hits=0 misses=[1-9][0-9]* saved=[1-9][0-9]* rejected=[0-9]+" /tmp/x87/cold.err
grep -Eq "\\[tcache\\] stats hits=[1-9][0-9]* misses=[0-9]+ saved=0 rejected=[0-9]+" /tmp/x87/warm.err
python3 -c "import sys; from pathlib import Path; p=Path(sys.argv[1]); b=bytearray(p.read_bytes()); assert b[:2]==bytes([235,0]); b[:2]=bytes([144,144]); Path(sys.argv[2]).write_bytes(b)" /tmp/x87/x87_code.bin /tmp/x87/x87_code-mutated.bin
timeout 30 qemu-arm /tmp/x87/x87_runner /tmp/x87/x87_code-mutated.bin > /tmp/x87/runtime.source-miss.txt 2>/tmp/x87/source-miss.err
diff -u /tmp/x87/runtime.txt /tmp/x87/runtime.source-miss.txt
grep -Eq "\\[tcache\\] stats hits=[1-9][0-9]* misses=[1-9][0-9]* saved=[0-9]+ rejected=[0-9]+" /tmp/x87/source-miss.err
BEFORE="$(sha256sum /tmp/x87/cache/winx86-armv7-v3.bin)"
WX86_TCACHE_CHAIN=1 timeout 30 qemu-arm /tmp/x87/x87_runner /tmp/x87/x87_code.bin > /tmp/x87/runtime.chain-mismatch.txt 2>/tmp/x87/chain-mismatch.err
diff -u /tmp/x87/runtime.txt /tmp/x87/runtime.chain-mismatch.txt
! grep -q "\\[tcache\\] stats" /tmp/x87/chain-mismatch.err
AFTER="$(sha256sum /tmp/x87/cache/winx86-armv7-v3.bin)"
[ "$BEFORE" = "$AFTER" ]
ID2="$(printf "%s" "$ID-incompatible" | sha256sum | { read -r hash rest; echo "$hash"; })"
WX86_TCACHE_ID="$ID2" timeout 30 qemu-arm /tmp/x87/x87_runner /tmp/x87/x87_code.bin > /tmp/x87/runtime.id-miss.txt 2>/tmp/x87/id-miss.err
diff -u /tmp/x87/runtime.txt /tmp/x87/runtime.id-miss.txt
grep -Eq "\\[tcache\\] stats hits=0 misses=[1-9][0-9]* saved=[1-9][0-9]* rejected=[0-9]+" /tmp/x87/id-miss.err
python3 -c "import sys; from pathlib import Path; p=Path(sys.argv[1]); b=bytearray(p.read_bytes()); b[-1]^=1; p.write_bytes(b)" /tmp/x87/cache/winx86-armv7-v3.bin
WX86_TCACHE_ID="$ID2" timeout 30 qemu-arm /tmp/x87/x87_runner /tmp/x87/x87_code.bin > /tmp/x87/runtime.corrupt.txt 2>/tmp/x87/corrupt.err
diff -u /tmp/x87/runtime.txt /tmp/x87/runtime.corrupt.txt
grep -Eq "\\[tcache\\] stats hits=0 misses=[1-9][0-9]* saved=[1-9][0-9]* rejected=[0-9]+" /tmp/x87/corrupt.err
python3 -c "import sys; from pathlib import Path; p=Path(sys.argv[1]); b=p.read_bytes(); p.write_bytes(b[:-8])" /tmp/x87/cache/winx86-armv7-v3.bin
WX86_TCACHE_ID="$ID2" timeout 30 qemu-arm /tmp/x87/x87_runner /tmp/x87/x87_code.bin > /tmp/x87/runtime.truncated.txt 2>/tmp/x87/truncated.err
diff -u /tmp/x87/runtime.txt /tmp/x87/runtime.truncated.txt
grep -Eq "\\[tcache\\] stats hits=0 misses=[1-9][0-9]* saved=[1-9][0-9]* rejected=[0-9]+" /tmp/x87/truncated.err
grep "\\[tcache\\] stats" /tmp/x87/cold.err /tmp/x87/warm.err /tmp/x87/source-miss.err /tmp/x87/id-miss.err /tmp/x87/corrupt.err /tmp/x87/truncated.err
'

tr -d '\r' <"$OUT/oracle.txt" >"$OUT/oracle.normalized.txt"
tr -d '\r' <"$OUT/runtime.txt" >"$OUT/runtime.normalized.txt"
if ! diff -u "$OUT/oracle.normalized.txt" "$OUT/runtime.normalized.txt"; then
    echo "FAIL: x87 FISTP PE oracle diverge" >&2
    exit 1
fi
echo "PASS: Wine x86 PE == libwinx86 ARM/QEMU; 13 x87 cases incl. RC, masked exceptions and FILD/FISTP exact path"
