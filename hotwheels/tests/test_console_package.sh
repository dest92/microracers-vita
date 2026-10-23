#!/usr/bin/env bash
# tests/test_console_package.sh — HW-028 (spec 015): el paquete listo-para-consola
# contiene lo esperado. Arma un VPK de prueba (no compila el juego) y verifica.
# 0 = verde, 77 = SKIP (faltan tools), otro = rojo.
set -uo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
source "$HW/tools/app.conf"
VITASDK="${VITASDK:-$HW/../shared/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

skip() { echo "SKIP: $1"; exit 77; }
fail() { echo "FAIL: $1" >&2; exit 1; }
for t in unzip sha256sum vita-mksfoex vita-pack-vpk; do
  command -v "$t" >/dev/null 2>&1 || skip "falta $t"
done

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/build"; head -c 1024 /dev/zero > "$TMP/build/eboot.bin"
bash "$HW/tools/package.sh" "$TMP/build" >/dev/null || fail "package.sh"
[ -f "$TMP/build/hotwheels-vita.vpk" ] || fail "no se generó el VPK de prueba"

bash "$HW/tools/package_console.sh" --vpk "$TMP/build/hotwheels-vita.vpk" --out "$TMP/dist" >/dev/null \
  || fail "package_console.sh"

# CA1: contenido del paquete.
for f in hotwheels-vita.vpk INSTALL.md CONSOLE-CHECKLIST.md stage_data.sh MANIFEST.txt; do
  [ -f "$TMP/dist/$f" ] || fail "falta $f en el paquete"
done

# CA2: MANIFEST con TITLEID + sha256 real.
grep -aq "TITLE_ID: $VITA_TITLEID" "$TMP/dist/MANIFEST.txt" || fail "MANIFEST sin TITLE_ID"
want=$(sha256sum "$TMP/dist/hotwheels-vita.vpk" | cut -d' ' -f1)
grep -aq "$want" "$TMP/dist/MANIFEST.txt" || fail "MANIFEST con sha256 distinto"

# CA3: VPK con eboot + param.sfo + assets de LiveArea.
LIST="$(unzip -l "$TMP/dist/hotwheels-vita.vpk" 2>/dev/null)"
for w in eboot.bin sce_sys/param.sfo sce_sys/icon0.png \
         sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/template.xml; do
  echo "$LIST" | grep -qE "[[:space:]]$w\$" || fail "VPK sin $w"
done

# CA4: nada del juego en el paquete.
if echo "$LIST" | grep -qiE 'HotWheels\.exe|/DATA/|EREG|HWICON'; then fail "VPK con material del juego"; fi
if ls "$TMP/dist" | grep -qiE '\.exe$|^DATA$|EREG'; then fail "paquete con material del juego"; fi

# CA5: checklist con secciones requeridas.
CK="$TMP/dist/CONSOLE-CHECKLIST.md"
for sec in "## 0. Requisitos" "## 1. Instalar el VPK" "## 2. Copiar los datos" \
           "## 3. Primer arranque" "## 6. Recolectar evidencia" "## 7. Triage"; do
  grep -qF "$sec" "$CK" || fail "checklist sin '$sec'"
done

# CA6: stage_data.sh funciona desde el paquete, con SRC externo (CAMBIOS HW-028).
# Simula el artefacto extraído en otra ruta + datos del usuario fuera del repo.
mkdir -p "$TMP/entregado"
cp -a "$TMP/dist/." "$TMP/entregado/"
FAKE="$TMP/usuario/Program_Files"
mkdir -p "$FAKE/DATA"
echo "exe" > "$FAKE/HotWheels.exe"
echo "man" > "$FAKE/DATA/MANUAL.TXT"
DEST="$TMP/ux0/data/hotwheels"
OUT_STAGE="$TMP/stage.out"
( cd "$TMP/entregado" && SRC="$FAKE" ./stage_data.sh "$DEST" --no-ereg ) >"$OUT_STAGE" 2>&1 \
  || { cat "$OUT_STAGE" >&2; fail "stage_data.sh desde el paquete"; }
[ -f "$DEST/HotWheels.exe" ] || fail "stage_data no copió HotWheels.exe"
[ -f "$DEST/DATA/MANUAL.TXT" ] || fail "stage_data no copió DATA/"
grep -q "OK stage_data" "$OUT_STAGE" || fail "stage_data sin OK"
# Sin SRC y sin repo al lado, tiene que avisar y fallar (no copiar a ciegas).
( cd "$TMP/entregado" && env -u SRC ./stage_data.sh "$TMP/dest-no" ) >/dev/null 2>&1 \
  && fail "stage_data sin SRC debería fallar en el paquete"
[ ! -e "$TMP/dest-no" ] || fail "stage_data creó destino sin validar SRC"

# CA7: las instrucciones del artefacto extraído dan el SRC externo, no el path del repo.
SRC_CMD='SRC=/path/to/Program_Files ./stage_data.sh /path/to/mount/ux0/data/hotwheels'
grep -qF "$SRC_CMD" "$TMP/dist/MANIFEST.txt" || fail "MANIFEST no enseña SRC para el paquete"
grep -qF "$SRC_CMD" "$TMP/dist/CONSOLE-CHECKLIST.md" || fail "checklist no enseña SRC para el paquete"
grep -qF "$SRC_CMD" "$TMP/dist/INSTALL.md" || fail "INSTALL no enseña comando portable del paquete"

echo "PASS test_console_package: TITLEID=$VITA_TITLEID, sha256 ${want:0:12}…, checklist OK, CA6 stage_data OK, CA7 portable docs OK"
