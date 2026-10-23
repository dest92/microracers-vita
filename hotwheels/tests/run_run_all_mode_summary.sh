#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
RUNNER="${RUN_ALL_SCRIPT:-$HW/tests/run_all.sh}"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
echo 'RUN_MODE: TSan unavailable; unsanitized concurrent integrity fallback' > "$TMP/test.log"
OUT="$(RUN_ALL_SCRIPT_PATH="$RUNNER" RUN_LOG="$TMP/test.log" bash -c '
  source <(sed -n "/^record() {/,/^}/p" "$RUN_ALL_SCRIPT_PATH")
  RESULTS=(); PASS=0; FAIL=0
  record mode-test 0 "$RUN_LOG"
  printf "%s\n" "${RESULTS[@]}"
')"
grep -Fq 'PASS mode-test (TSan unavailable; unsanitized concurrent integrity fallback)' <<< "$OUT"
echo 'run_all fallback mode summary: PASS'
