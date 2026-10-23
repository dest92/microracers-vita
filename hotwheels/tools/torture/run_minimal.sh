#!/usr/bin/env bash
# Minimal winx86 torture target, independent of D2Vita/rt_boot_arm.
# It builds libwinx86 directly, links a tiny ARM runner, and compares the
# observable result with the chain funnel and direct-link A/B knobs.
set -euo pipefail
ulimit -c 0

HERE="$(cd "$(dirname "$0")" && pwd)"
HW="$(cd "$HERE/../.." && pwd)"
SOURCE_WX86="${WINX86_DIR:-$HW/../d2vita/D2Vita/third_party/winx86}"
PATCH_BASE="${WINX86_BASE:-bd5d299}"
OWN_OUT=0
if [ -n "${OUT:-}" ]; then
    OUT="$OUT"
else
    OUT="$(mktemp -d /tmp/hw-torture-minimal.XXXXXX)"
    OWN_OUT=1
fi
KEEP_OUT=0
BUILD_WX86=""
cleanup() {
    local rc=$?
    if [ -n "$BUILD_WX86" ] && [ "$BUILD_WX86" != "$SOURCE_WX86" ]; then
        git -C "$SOURCE_WX86" worktree remove --force "$BUILD_WX86" >/dev/null 2>&1 || true
    fi
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 77 ]; then KEEP_OUT=1; fi
    if [ "$OWN_OUT" -eq 1 ] && [ "$KEEP_OUT" -eq 0 ]; then rm -rf -- "$OUT"; else echo "OUT=$OUT" >&2; fi
}
trap cleanup EXIT
mkdir -p "$OUT"

need() { command -v "$1" >/dev/null 2>&1 || {
    echo "SKIP: falta $1 (oracle ARM/qemu no disponible)" >&2
    exit 77
}; }
[ -d "$SOURCE_WX86" ] || { echo "FAIL: WINX86_DIR no existe: $SOURCE_WX86" >&2; KEEP_OUT=1; exit 2; }

# Build from the known base of the runtime patch series, not the shared
# submodule HEAD: later D2-only commits may already contain one of these files
# (e.g. pmu_metrics.h), so applying the port patch series there is invalid.
# The isolated worktree also keeps D2Vita's dirty WIP untouched.
if git -C "$SOURCE_WX86" rev-parse --git-dir >/dev/null 2>&1; then
    git -C "$SOURCE_WX86" cat-file -e "$PATCH_BASE^{commit}" || {
        echo "FAIL: WINX86_BASE no existe: $PATCH_BASE" >&2
        KEEP_OUT=1
        exit 2
    }
    BUILD_WX86="$OUT/winx86"
    git -C "$SOURCE_WX86" worktree add --detach "$BUILD_WX86" "$PATCH_BASE" >/dev/null
    for patch in "$HW"/patches/winx86-*.patch; do
        git -C "$BUILD_WX86" apply --check "$patch"
        git -C "$BUILD_WX86" apply "$patch"
    done
else
    BUILD_WX86="$SOURCE_WX86"
fi

# Apply/validate the patch stack even on hosts that lack the ARM/qemu oracle.
# The subsequent rc=77 is still a skip, never a correctness PASS.
need arm-linux-gnueabihf-gcc
need arm-linux-gnueabihf-g++
need qemu-arm

bash "$BUILD_WX86/build.sh" >/dev/null
LIB="$BUILD_WX86/build-arm/libwinx86.a"
[ -f "$LIB" ] || { echo "FAIL: falta $LIB" >&2; KEEP_OUT=1; exit 2; }

arm-linux-gnueabihf-g++ -std=gnu++17 -O2 -static -marm \
  -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard \
  -I"$BUILD_WX86/src" -I"$BUILD_WX86/src/dynarec86" \
  -I"$BUILD_WX86/src/dynarec86/shim" -I"$BUILD_WX86/third_party/box86-dynarec/include" \
  "$HERE/minimal_runner.cpp" "$LIB" -lpthread -ldl -lm -o "$OUT/minimal_runner"

for chain in 0 1; do
  WX86_CHAIN="$chain" timeout 30 qemu-arm "$OUT/minimal_runner" \
    >"$OUT/chain-$chain.txt" 2>"$OUT/chain-$chain.err"
done
if ! diff -u "$OUT/chain-0.txt" "$OUT/chain-1.txt"; then
    echo "FAIL: WX86_CHAIN A/B cambió el resultado observable" >&2
    KEEP_OUT=1
    exit 1
fi
for expected in \
  'TORTURE\|case=indirect-a\|slot=0x10003000\|eax=10\|ecx=0\|ebx=17\|mem=17\|fault=none' \
  'TORTURE\|case=indirect-b\|slot=0x10003000\|eax=10\|ecx=0\|ebx=34\|mem=34\|fault=none' \
  'TORTURE\|case=smc-retranslated\|slot=0x10003000\|eax=10\|ecx=0\|ebx=51\|mem=51\|fault=none'; do
    if ! grep -Eq "^$expected$" "$OUT/chain-0.txt"; then
        echo "FAIL: salida mínima no confirma caso esperado: $expected" >&2
        KEEP_OUT=1
        exit 1
    fi
done
echo "PASS: libwinx86 directo; WX86_CHAIN=0/1 equivalentes (loop, destinos indirectos, memoria y SMC)"
