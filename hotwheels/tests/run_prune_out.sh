#!/usr/bin/env bash
# Test de tools/prune_out.sh: no matar corridas activas (bug de la tanda 36:
# prune cada 30 min borró el LOGDIR de un smoke en curso → FAIL de infra).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PRUNE="$ROOT/hotwheels/tools/prune_out.sh"
[ -f "$PRUNE" ] || { echo "prune_out.sh inexistente"; exit 1; }
bash -n "$PRUNE"
S="$(mktemp -d)"; trap 'rm -rf "$S"' EXIT
mkdir -p "$S/tools" "$S/hotwheels/out"
cp "$PRUNE" "$S/tools/prune_out.sh"

# 4 corridas viejas con mtime escalonado (ls -t determinista):
# sm4(3h30) sm3(3h40) run2(3h50) run1(4h) => orden ls -t desc: sm4 sm3 run2 run1
mk() { local d="$S/hotwheels/out/$1"; mkdir -p "$d"; : > "$d/run.log"; touch -d "$2" "$d" "$d/run.log"; }
mk run-20260101-000001 "4 hours ago"
mk run-20260102-000002 "3 hours ago 10 minutes"
mk smoke-20260103-000003.d "3 hours ago 20 minutes"
mk smoke-20260104-000004.d "3 hours ago 30 minutes"

# A) sin lock: fuera de top3 se borra (run1 es el i=4); smoke-*.d sin protección.
HW_PRUNE_ROOT="$S" bash "$S/tools/prune_out.sh" >/dev/null
[ ! -d "$S/hotwheels/out/run-20260101-000001" ] || { echo "FAIL A: run1 (i=4) no se borro"; exit 1; }
[ -d "$S/hotwheels/out/smoke-20260103-000003.d" ] || { echo "FAIL A: murio el top3 (sm3)"; exit 1; }

# B) smoke activo (lock fresco): el smoke fuera de top3 NO se borra.
mk smoke-20260100-000000.d "5 hours ago"    # i=4 => fuera de top3
date +%s > "$S/hotwheels/out/.smoke-active"
HW_PRUNE_ROOT="$S" bash "$S/tools/prune_out.sh" >/dev/null
[ -d "$S/hotwheels/out/smoke-20260100-000000.d" ] || { echo "FAIL B: prune mato smoke-*.d con lock activo"; exit 1; }
[ -d "$S/hotwheels/out/smoke-20260103-000003.d" ] || { echo "FAIL B: desaparecio smoke del top3"; exit 1; }

# C) lock viejo (>30 min): vuelve la política normal y el smoke i=4 muere.
touch -d "40 minutes ago" "$S/hotwheels/out/.smoke-active"
HW_PRUNE_ROOT="$S" bash "$S/tools/prune_out.sh" >/dev/null
[ ! -d "$S/hotwheels/out/smoke-20260100-000000.d" ] || { echo "FAIL C: lock viejo debia permitir el borrado"; exit 1; }

echo "prune_out: PASS"
