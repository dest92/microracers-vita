#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HW/build/tests"
${CXX:-g++} -std=c++17 -Wall -Wextra -I"$HW/src" "$HW/tests/test_scripted_input.cpp" "$HW/src/integration/scripted_input.cpp" -o "$HW/build/tests/scripted-input"
"$HW/build/tests/scripted-input"
echo 'scripted-input: PASS'
