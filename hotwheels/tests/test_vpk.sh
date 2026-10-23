#!/usr/bin/env bash
# hotwheels/tests/test_vpk.sh — HW-017: el VPK contiene lo esperado.
#
# No compila el juego: empaqueta un eboot.bin de prueba con tools/package.sh y
# verifica el contenido con `unzip -l` (el VPK es un zip). Además comprueba que
# los assets de sce_sys commiteados son exactamente lo que genera el script
# (reproducible, sin material del juego) y que el param.sfo lleva nuestro TITLEID.
#
# Salida: 0 = verde, 77 = SKIP (faltan herramientas), !=0/!=77 = rojo.
set -uo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=../tools/app.conf
source "$HW/tools/app.conf"
VITASDK="${VITASDK:-$HW/../shared/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

skip() { echo "SKIP: $1"; exit 77; }
fail() { echo "FAIL: $1" >&2; exit 1; }

for t in python3 unzip vita-mksfoex vita-pack-vpk; do
  command -v "$t" >/dev/null 2>&1 || skip "falta $t"
done

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# 0) El generador es determinista y coincide con lo commiteado.
python3 "$HW/tools/make_sce_sys.py" --out "$TMP/regen" >/dev/null
for f in icon0.png livearea/contents/bg.png livearea/contents/startup.png \
         livearea/contents/template.xml; do
  cmp -s "$HW/sce_sys/$f" "$TMP/regen/$f" \
    || fail "sce_sys/$f difiere de lo que genera make_sce_sys.py (¿asset del juego?)"
done

# 1) Empaquetar un eboot.bin de prueba.
head -c 1024 /dev/zero > "$TMP/eboot.bin"
HW_ORIGINAL_ART_DIR='' bash "$HW/tools/package.sh" "$TMP" >/dev/null || fail "package.sh falló"
VPK="$TMP/hotwheels-vita.vpk"
[ -f "$VPK" ] || fail "no se generó el VPK"

LIST="$(unzip -l "$VPK" 2>/dev/null)"
echo "$LIST"

# 2) Debe contener eboot + param.sfo + assets de LiveArea.
for want in eboot.bin sce_sys/param.sfo sce_sys/icon0.png \
            sce_sys/livearea/contents/bg.png \
            sce_sys/livearea/contents/startup.png \
            sce_sys/livearea/contents/template.xml; do
  echo "$LIST" | grep -qE "[[:space:]]$want\$" || fail "falta $want en el VPK"
done

# 3) param.sfo con nuestro TITLEID (el SFO guarda strings; grep -a alcanza).
grep -aq "$VITA_TITLEID" "$TMP/param.sfo" \
  || fail "param.sfo no contiene TITLE_ID=$VITA_TITLEID"

# 4) Nada del juego empaquetado (sólo la app y sus assets propios).
if echo "$LIST" | grep -qiE 'HotWheels\.exe|/DATA/|EREG|HWICON'; then
  fail "el VPK contiene material del juego"
fi

echo "PASS test_vpk: TITLEID=$VITA_TITLEID, $(echo "$LIST" | grep -cE '^[[:space:]]*[0-9]+.*(sce_sys/|eboot\.bin)') entradas"
