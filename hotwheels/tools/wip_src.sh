#!/usr/bin/env bash
# wip_src.sh — reporta cambios locales sin commitear en hotwheels/src.
# Uso: wip_src.sh [root]  →  imprime "wip=N" y, si N>0, el listado.
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
if ! git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    echo "wip=0"
    exit 0
fi
list="$(git -C "$ROOT" status --porcelain -- hotwheels/src 2>/dev/null || true)"
n=0
if [ -n "$list" ]; then
    n="$(printf '%s\n' "$list" | grep -c . || true)"
fi
echo "wip=$n"
if [ "$n" -gt 0 ]; then
    printf '%s\n' "$list"
fi
exit 0
