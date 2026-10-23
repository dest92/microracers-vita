#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PATCH="$ROOT/hotwheels/patches/winx86-0003-pmu-profile.patch"

python3 - "$PATCH" <<'PY'
from pathlib import Path
import sys

patch = Path(sys.argv[1]).read_text()
start = patch.index('"pmu: init begin tid=%u"')
guard = patch.rfind('pmu_requested() && !pn->pmu_init_reported', 0, start)
init = patch.index('pmu_on = pmu_thread_init();', start)
end = patch.index('"pmu: init end tid=%u enabled=%u"', init)
reported = patch.index('pn->pmu_init_reported = true;', end)
gil_release = patch.index('gil::Release r;', reported)

assert '#ifdef __vita__' in patch[patch.rfind('@@', 0, start):start]
assert guard >= 0 and guard < start < init < end < reported < gil_release
assert 'bool            pmu_init_reported = false;' in patch
print('PASS: init PMU queda opt-in y deja marcadores antes de cpu_->run()')
PY
