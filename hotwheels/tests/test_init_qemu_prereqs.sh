#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BASH_BIN="$(command -v bash)"
LS_BIN="$(command -v ls)"
GIT_BIN="$(command -v git)"
DIRNAME_BIN="$(command -v dirname)"
TMP="$(mktemp -d /tmp/hw-init-prereqs.XXXXXX)"
RC=1
cleanup() {
  if [ "$RC" -eq 0 ]; then rm -rf "$TMP"; else echo "DEBUG: $TMP" >&2; fi
}
trap cleanup EXIT

assert_status() {
  local out="$1" name="$2" expected="$3" actual
  actual="$(awk -v name="$name" '
    $1 == "OK" || $1 == "FALTA" {
      state=$1; sub(/^(OK|FALTA)[[:space:]]+/, "")
      if ($0 == name) { print state; exit }
    }' <<<"$out")"
  if [ "$actual" != "$expected" ]; then
    echo "FAIL: $name esperado=$expected obtenido=${actual:-ausente}" >&2
    return 1
  fi
}

mkdir "$TMP/absent" "$TMP/present"
for path in "$TMP/absent" "$TMP/present"; do
  ln -s "$LS_BIN" "$path/ls"
  ln -s "$GIT_BIN" "$path/git"
  ln -s "$DIRNAME_BIN" "$path/dirname"
done
for tool in arm-linux-gnueabihf-gcc arm-linux-gnueabihf-g++ qemu-arm; do
  printf '#!/bin/sh\nexit 0\n' >"$TMP/present/$tool"
  chmod +x "$TMP/present/$tool"
done

OUT_ABSENT="$(PATH="$TMP/absent" "$BASH_BIN" "$ROOT/init.sh")"
OUT_PRESENT="$(PATH="$TMP/present" "$BASH_BIN" "$ROOT/init.sh")"
assert_status "$OUT_ABSENT" 'ARM Linux GCC' FALTA
assert_status "$OUT_ABSENT" 'ARM Linux G++' FALTA
assert_status "$OUT_ABSENT" 'qemu-arm' FALTA
assert_status "$OUT_PRESENT" 'ARM Linux GCC' OK
assert_status "$OUT_PRESENT" 'ARM Linux G++' OK
assert_status "$OUT_PRESENT" 'qemu-arm' OK
RC=0
echo "PASS: init.sh distingue prerequisitos ausentes y presentes sin volverlos críticos"
