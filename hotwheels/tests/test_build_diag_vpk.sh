#!/usr/bin/env bash
# SPEC-052 CA5: plan de build de las variantes de diagnóstico (sin compilar: --dry-run).
set -uo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
fails=0; fail() { echo "FAIL: $*"; fails=$((fails + 1)); }
S="$HW/tools/build_diag_vpk.sh"
# Flags del vitaGL de producción, leídos de build_vitagl.sh: V0 no debe derivar del VPK de usuario.
base="$(grep -o 'make install -j4 HAVE_[A-Z0-9_= ]*' "$HW/tools/build_vitagl.sh" | sed 's/make install -j4 //; s/ *$//')"
[ -n "$base" ] || fail "no se pudieron leer los flags de build_vitagl.sh"
plan="$(bash "$S" --dry-run V0 V1 V2)" || fail "dry-run falló"
line() { printf '%s\n' "$plan" | grep "^variant=$1 "; }
flags() { line "$1" | sed 's/.* make_flags="\([^"]*\)".*/\1/'; }
[ "$(flags V0)" = "$base VGL_DIAG_VARIANT=V0" ] || fail "V0: '$(flags V0)' != '$base VGL_DIAG_VARIANT=V0'"
[ "$(flags V1)" = "$base SAFE_DRAW=1 SAFE_UNIFORMS=1 VGL_DIAG_VARIANT=V1" ] || fail "V1: '$(flags V1)'"
[ "$(flags V2)" = "$base NO_CIRCULAR_POOL=1 VGL_DIAG_VARIANT=V2" ] || fail "V2: '$(flags V2)'"
for v in V0 V1 V2; do
  line "$v" | grep -q " vgl_sha=c707ff00d9a372f0ba7056b4a4b723f5a00ba880 " || fail "$v sin SHA de vitaGL fijado"
  line "$v" | grep -q " vpk=$HW/out/diag-vpk/hotwheels-diag-$v.vpk" || fail "$v ruta de VPK"
done
bash "$S" --dry-run V9 >/dev/null 2>&1 && fail "variante desconocida aceptada"
[ "$fails" = 0 ] && echo "test_build_diag_vpk: OK" || { echo "test_build_diag_vpk: $fails FALLOS"; exit 1; }
