#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
W="${WINX86_DIR:-$HW/../d2vita/D2Vita/third_party/winx86}"
mkdir -p "$HW/build"
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$W/src" -I"$HW/src" \
    "$HW/tools/dsound_replay.cpp" -o "$HW/build/dsound_replay"
HW_DS_REPLAY="$HW/build/dsound_replay" python3 "$HW/tests/test_dsound_oracle.py"
c++ -std=c++17 -Wall -Wextra -Werror -pthread -I"$HW/src" \
    "$HW/tests/test_dsound_trace.cpp" -o "$HW/build/test_dsound_trace"
"$HW/build/test_dsound_trace"
if [ "${HW_DS_MUTANTS:-0}" = 1 ]; then
    TMP="$(mktemp -d "$HW/build/hw069-mutants.XXXXXX")"
    trap 'rm -rf -- "$TMP"' EXIT
    for mutant in nearest reset_phase db_power; do
        mkdir -p "$TMP/$mutant/runtime"
        python3 - "$W/src/runtime/ds_resample.h" "$HW/tools/dsound_replay.cpp" "$TMP/$mutant" "$mutant" <<'PY'
from pathlib import Path
import sys
h=Path(sys.argv[1]).read_text();cpp=Path(sys.argv[2]).read_text();dst=Path(sys.argv[3]);kind=sys.argv[4]
if kind=='nearest':
    assert 'if (f) {' in h;h=h.replace('if (f) {','if (false && f) {')
elif kind=='reset_phase':
    assert 'const uint64_t inv' in h;h=h.replace('const uint64_t inv','v.acc = 0;\n    const uint64_t inv')
else:
    assert '/2000.0' in cpp;cpp=cpp.replace('/2000.0','/1000.0')
(dst/'runtime/ds_resample.h').write_text(h);(dst/'replay.cpp').write_text(cpp)
PY
        c++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$TMP/$mutant" -I"$HW/src" \
            "$TMP/$mutant/replay.cpp" -o "$TMP/$mutant/replay"
        if HW_DS_REPLAY="$TMP/$mutant/replay" python3 "$HW/tests/test_dsound_oracle.py" Oracle.test_native_fidelity > "$TMP/$mutant/result.txt" 2>&1; then
            echo "MUTANT SURVIVED: $mutant"; exit 1
        fi
        grep -q 'AssertionError' "$TMP/$mutant/result.txt" || { cat "$TMP/$mutant/result.txt"; exit 1; }
        echo "MUTANT KILLED: $mutant"
    done
fi
