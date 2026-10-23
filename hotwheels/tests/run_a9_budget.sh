#!/usr/bin/env bash
# HW-073 (perf-host) SPEC-054 CA1: modelo de presupuesto A9 del resampleo.
# Sólo el unit test (rápido). La medición con qemu vive en tools/a9/run_a9_budget.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/test_a9_budget.py
