#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d /tmp/hw-div32-test.XXXXXX)"
cleanup() { local rc=$?; if [ "$rc" -eq 0 ]; then rm -rf -- "$OUT"; else echo "OUT=$OUT" >&2; fi; }
trap cleanup EXIT
gcc -std=c11 -O2 -Wall -Wextra -Werror "$HW/tests/test_div32_fast.c" -o "$OUT/test_div32_fast"
"$OUT/test_div32_fast"
