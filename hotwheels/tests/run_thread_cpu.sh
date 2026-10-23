#!/usr/bin/env bash
# HW-075: snapshots raw y publicación de identidades por rol.
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/out"
trial_dir="$(mktemp -d "$HW/out/thread-cpu-XXXXXX")"
trap 'rc=$?; if [ "$rc" -eq 0 ]; then rm -rf -- "$trial_dir"; else echo "FAIL: evidencia en $trial_dir" >&2; fi' EXIT
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -I"$HW/src" \
    "$HW/tests/test_thread_cpu.cpp" -o "$trial_dir/test"
"$trial_dir/test"
c++ -std=c++17 -g -Wall -Wextra -Werror -fsanitize=thread -fno-pie -no-pie -pthread -I"$HW/src" \
    "$HW/tests/test_thread_cpu.cpp" -o "$trial_dir/test_tsan"
setarch "$(uname -m)" -R "$trial_dir/test_tsan"
