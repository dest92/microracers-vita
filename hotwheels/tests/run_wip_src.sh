#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WIP="$ROOT/hotwheels/tools/wip_src.sh"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# 1) sin repo → wip=0
out="$("$WIP" "$T")"
[ "$out" = "wip=0" ] || { echo "FAIL no-repo: $out"; exit 1; }

# 2) repo limpio → wip=0
git -C "$T" init -q
mkdir -p "$T/hotwheels/src"
echo ok > "$T/hotwheels/src/a.cpp"
git -C "$T" -c user.email=t@t -c user.name=t add -A
git -C "$T" -c user.email=t@t -c user.name=t commit -qm base
out="$("$WIP" "$T")"
[ "$out" = "wip=0" ] || { echo "FAIL limpio: $out"; exit 1; }

# 3) sucio en src → wip=2 con ambas líneas
echo dirty >> "$T/hotwheels/src/a.cpp"
echo new > "$T/hotwheels/src/b.cpp"
out="$("$WIP" "$T")"
n="$(printf '%s\n' "$out" | head -1)"
[ "$n" = "wip=2" ] || { echo "FAIL sucio: $out"; exit 1; }
printf '%s\n' "$out" | grep -q "src/a.cpp" || { echo "FAIL lista a.cpp"; exit 1; }
printf '%s\n' "$out" | grep -q "src/b.cpp" || { echo "FAIL lista b.cpp"; exit 1; }

# 4) sucio fuera de src → no cuenta
echo x >> "$T/hotwheels/README" 2>/dev/null || echo x > "$T/hotwheels/README"
out="$("$WIP" "$T")"
[ "$(printf '%s\n' "$out" | head -1)" = "wip=2" ] || { echo "FAIL fuera de src: $out"; exit 1; }

echo "PASS wip_src (0 repos / 0 limpio / 2 sucio / fuera de src no cuenta)"

# 5) parser de los callers (run_all/smoke): la PRIMERA línea es wip=N;
#    con listado multi-línea el caller no debe invalidar N.
echo dirty >> "$T/hotwheels/src/a.cpp"
out="$("$WIP" "$T")"
n1="$(printf '%s\n' "$out" | head -1)"; n1="${n1#wip=}"
case "$n1" in "" | *[!0-9]*) echo "FAIL parser: $n1"; exit 1 ;; esac
[ "$n1" -gt 0 ] || { echo "FAIL parser n=0 con sucio"; exit 1; }

echo "PASS wip_src (parser callers con listado multi-línea)"
