#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d /tmp/hw-eip-profile-test.XXXXXX)"
keep=0
cleanup() { rc=$?; if [ "$rc" -ne 0 ]; then keep=1; fi; if [ "$keep" -eq 0 ]; then rm -rf -- "$OUT"; else echo "OUT=$OUT" >&2; fi; }
trap cleanup EXIT
cat > "$OUT/functions.csv" <<'MAP'
0x00401000,0x00401010,0x00401000,func_A
0x00402000,0x00402010,0x00402000,func_B
0x00403000,0x00403020,0x00403000,outer
0x00403008,0x00403010,0x00403008,inner
MAP
g++ -std=c++17 -Wall -Wextra -Werror -pthread -I"$HW/src" \
  "$HW/src/runtime/guest_eip_profile.cpp" "$HW/tests/test_guest_eip_profile.cpp" \
  -o "$OUT/test_guest_eip_profile"
"$OUT/test_guest_eip_profile" "$OUT/functions.csv"
echo "PASS: EIP-to-function grouping, overlap precedence, guest/shim/idle accounting"
g++ -std=c++17 -Wall -Wextra -Werror -pthread -DHW_TEST_NO_DYN86_PROFILE -I"$HW/src" \
  "$HW/src/runtime/guest_eip_profile.cpp" "$HW/tests/test_guest_eip_profile.cpp" \
  -o "$OUT/test_guest_eip_profile_without_winx86_patch"
"$OUT/test_guest_eip_profile_without_winx86_patch"
echo "PASS: opt-in profiler stays inactive and links without the optional winx86 patch"
