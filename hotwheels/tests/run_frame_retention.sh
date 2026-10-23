#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_frame_retention.cpp -o build/test_frame_retention
build/test_frame_retention emit
python3 tools/finalize_frames.py build/test-frame-retention/sampled
python3 tools/finalize_frames.py build/test-frame-retention/all
build/test_frame_retention verify
rm -rf -- build/test-frame-retention
echo "test_frame_retention: sample + últimos 20 / HW_FRAMES_ALL OK"
