#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_present.cpp src/present/present_math.cpp -o build/test_present
build/test_present
