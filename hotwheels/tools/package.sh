#!/usr/bin/env bash
# hotwheels/tools/package.sh — param.sfo + VPK con LiveArea (HW-017).
#
# Empaqueta un eboot.bin YA construido (no compila). Es invocado por
# tools/build_vita.sh al final del build y por tests/test_vpk.sh con un eboot
# de prueba. Fuente única de verdad de TITLEID/versión: tools/app.conf.
#
# Uso: package.sh [OUT_DIR]      (default: hotwheels/build-vita)
# Requiere un eboot.bin en OUT_DIR y vitasdk (vita-mksfoex, vita-pack-vpk).
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=app.conf
source "$HW/tools/app.conf"
OUT="${1:-$HW/build-vita}"
VITASDK="${VITASDK:-$HW/../shared/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
mkdir -p "$OUT"

EBOOT="$OUT/eboot.bin"
[ -f "$EBOOT" ] || { echo "package: falta $EBOOT (compilá primero)" >&2; exit 1; }

# 1) Staging propio: nunca regenerar material original en el árbol versionado.
ART="$OUT/sce_sys"
art_args=()
if [ "${HW_ORIGINAL_ART_DIR+x}" = x ]; then
  original_dir="$HW_ORIGINAL_ART_DIR"  # vacío pide genéricos explícitamente
else
  original_dir=""
  if [ -e "$HW/extracted/iso/HWICON.BMP" ] || [ -e "$HW/extracted/iso/autostart/background.bmp" ]; then
    original_dir="$HW/extracted/iso"  # si está incompleto, el generador falla
  fi
fi
if [ -n "$original_dir" ]; then
  art_args=(--original-dir "$original_dir")
  echo "package: LiveArea original desde $original_dir"
else
  echo "package: LiveArea genérico (sin originales seleccionados)"
fi
python3 "$HW/tools/make_sce_sys.py" --out "$ART" "${art_args[@]}" >/dev/null
for f in icon0.png livearea/contents/bg.png livearea/contents/startup.png \
         livearea/contents/template.xml; do
  [ -f "$ART/$f" ] || { echo "package: falta sce_sys/$f" >&2; exit 1; }
done

# 2) param.sfo (TITLEID propio; APP_VER y ATTRIBUTE2=12 = memoria extra).
vita-mksfoex \
  -s TITLE_ID="$VITA_TITLEID" \
  -s APP_VER="$VITA_VERSION" \
  -d ATTRIBUTE2=12 \
  "$VITA_APP_NAME" "$OUT/param.sfo" >/dev/null

# 3) VPK: eboot + param.sfo + assets de LiveArea.
vita-pack-vpk -s "$OUT/param.sfo" -b "$EBOOT" \
  -a "$ART/icon0.png=sce_sys/icon0.png" \
  -a "$ART/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png" \
  -a "$ART/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png" \
  -a "$ART/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml" \
  "$OUT/hotwheels-vita.vpk" >/dev/null
