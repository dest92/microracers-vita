#!/usr/bin/env bash
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT

mkdir -p "$TMP/source/DATA/MENU"
touch "$TMP/source/HotWheels.exe"
SRC="$TMP/source" "$HW/tools/stage_data.sh" "$TMP/dest-missing" >/dev/null
test -f "$TMP/dest-missing/DATA/MENU/settings.dat"
python3 - "$TMP/dest-missing/DATA/MENU/settings.dat" <<'PY'
import pathlib, sys
assert pathlib.Path(sys.argv[1]).read_text().splitlines()[:2] == ["0", "2"]
PY
printf '1\n1\n9\n' > "$TMP/dest-missing/DATA/MENU/settings.dat"
SRC="$TMP/source" "$HW/tools/stage_data.sh" "$TMP/dest-missing" >/dev/null
python3 - "$TMP/dest-missing/DATA/MENU/settings.dat" <<'PY'
import pathlib, sys
assert pathlib.Path(sys.argv[1]).read_text().splitlines() == ["1", "1", "9"]
PY

printf '1\r\n0\r\n7\r\n' > "$TMP/source/DATA/MENU/settings.dat"
SRC="$TMP/source" "$HW/tools/stage_data.sh" "$TMP/dest-existing" >/dev/null
python3 - "$TMP/dest-existing/DATA/MENU/settings.dat" <<'PY'
import pathlib, sys
raw = pathlib.Path(sys.argv[1]).read_bytes()
assert raw == b"1\r\n2\r\n7\r\n", raw
PY
# Re-stage must preserve settings already edited by the game, even when source has one.
printf '8\r\n0\r\n42\r\n' > "$TMP/dest-existing/DATA/MENU/settings.dat"
SRC="$TMP/source" "$HW/tools/stage_data.sh" "$TMP/dest-existing" >/dev/null
python3 - "$TMP/dest-existing/DATA/MENU/settings.dat" <<'PY'
import pathlib, sys
raw = pathlib.Path(sys.argv[1]).read_bytes()
assert raw == b"8\r\n0\r\n42\r\n", raw
PY
echo "stage_data default resolution OK"
