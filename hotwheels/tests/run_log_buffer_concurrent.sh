#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -pthread -fno-pie -no-pie -Isrc \
    tests/test_log_buffer_concurrent.cpp src/io/log_buffer.cpp -o build/test_log_buffer_concurrent
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
fallback() {
    echo 'ThreadSanitizer unavailable; running unsanitized concurrent integrity check.' >&2
    echo 'RUN_MODE: TSan unavailable; unsanitized concurrent integrity fallback'
    c++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Isrc \
        tests/test_log_buffer_concurrent.cpp src/io/log_buffer.cpp -o build/test_log_buffer_concurrent_plain
    build/test_log_buffer_concurrent_plain
}
retries="${HW_TSAN_RETRIES:-10}"
if [ "$retries" -le 0 ]; then fallback; exit; fi
for attempt in $(seq 1 "$retries"); do
    set +e
    { TSAN_OPTIONS=halt_on_error=1 build/test_log_buffer_concurrent; } >"$LOG" 2>&1
    rc=$?
    set -e
    if [ "$rc" -eq 0 ]; then cat "$LOG"; exit 0; fi
    if grep -q '^FATAL: ThreadSanitizer: unexpected memory mapping ' "$LOG"; then continue; fi
    if [ "$rc" -eq 139 ] && ! grep -q '^THREAD_SANITIZER_TEST_STARTED$' "$LOG"; then continue; fi
    cat "$LOG" >&2
    exit "$rc"
done
cat "$LOG" >&2
echo "ThreadSanitizer could not reserve shadow memory after $retries attempts" >&2
fallback
