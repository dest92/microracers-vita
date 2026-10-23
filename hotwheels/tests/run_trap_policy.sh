#!/usr/bin/env bash
# tests/run_trap_policy.sh — SPEC-048: default de la política de primera trampa.
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/build"
c++ -std=c++17 -Wall -Wextra -Werror -I"$HW/src" \
    "$HW/tests/test_trap_policy.cpp" -o "$HW/build/test_trap_policy"
"$HW/build/test_trap_policy"
