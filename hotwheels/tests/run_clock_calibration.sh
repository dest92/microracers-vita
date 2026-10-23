#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/out"
trial_dir="$(mktemp -d "$HW/out/clock-calibration-XXXXXX")"
trap 'rc=$?; if [ "$rc" -eq 0 ]; then rm -rf -- "$trial_dir"; else echo "FAIL: evidencia en $trial_dir" >&2; fi' EXIT
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$HW/src" -I"$HW/samples/thread-clock" \
    "$HW/tests/test_clock_calibration.cpp" -o "$trial_dir/test"
"$trial_dir/test"
c++ -std=c++17 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$HW/src" -I"$HW/samples/thread-clock" \
    "$HW/tests/test_clock_calibration.cpp" -o "$trial_dir/test_san"
"$trial_dir/test_san"
