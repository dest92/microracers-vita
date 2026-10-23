#!/usr/bin/env bash
# tests/run_ds_cursor_contract.sh — SPEC-048 (frente 2): contrato de cursores.
#
# La definición vive en el PARCHE versionado (winx86 no está en este repo: es un
# repo aparte compartido con D2Vita), así que el header se materializa desde el
# patch. El test no depende de out/ ni de ningún artefacto fuera de git
# (BUG-048).
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
PATCH="${1:-$HW/patches/winx86-0014-dsound-cursor-contract.patch}"
[ -f "$PATCH" ] || { echo "falta el patch $PATCH" >&2; exit 1; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/runtime"
( cd "$TMP" && git apply --include='src/runtime/ds_cursor.h' "$PATCH" )
mkdir -p "$HW/build"
c++ -std=c++17 -Wall -Wextra -Werror -I"$TMP/src/runtime" \
    "$HW/tests/test_ds_cursor_contract.cpp" -o "$HW/build/test_ds_cursor_contract"
"$HW/build/test_ds_cursor_contract" "$PATCH"
