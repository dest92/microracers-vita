#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${TMPDIR:-/tmp}/hw-mmio-transfer-$$"
trap 'rm -f "$BIN"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -I"$HW/src" \
  "$HW/tests/test_mmio_transfer.cpp" -o "$BIN"
"$BIN"
echo 'MMIO transfer: PASS'
