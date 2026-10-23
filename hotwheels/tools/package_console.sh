#!/usr/bin/env bash
# tools/package_console.sh — arma el paquete "listo para consola" (HW-028).
#
# NO ejecuta nada en la consola. Toma el VPK del runtime (ya construido o lo
# construye con --build), y lo empaqueta con la guía de instalación, el
# checklist de consola, el script de datos y un MANIFEST con hash.
#
# Uso:
#   package_console.sh [--build] [--vpk PATH] [--out DIR]
#     --build      corre tools/build_vita.sh antes de empaquetar
#     --vpk PATH   VPK a empaquetar (default: build-vita/hotwheels-vita.vpk)
#     --out DIR    destino (default: dist/console-<fecha>)
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=app.conf
source "$HW/tools/app.conf"
BUILD=0; VPK=""; OUT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --build) BUILD=1 ;;
    --vpk) VPK="${2:?falta PATH}"; shift ;;
    --out) OUT="${2:?falta DIR}"; shift ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "package_console: arg desconocido: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ "$BUILD" = 1 ]; then
  bash "$HW/tools/build_vita.sh"
fi
[ -n "$VPK" ] || VPK="$HW/build-vita/hotwheels-vita.vpk"
[ -f "$VPK" ] || { echo "package_console: falta $VPK (compilá con tools/build_vita.sh)" >&2; exit 1; }
[ -n "$OUT" ] || OUT="$HW/dist/console-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

for f in "$HW/docs/INSTALL.md" "$HW/docs/CONSOLE-CHECKLIST.md" "$HW/tools/stage_data.sh"; do
  [ -f "$f" ] || { echo "package_console: falta $f" >&2; exit 1; }
done

cp "$VPK" "$OUT/hotwheels-vita.vpk"
cp "$HW/docs/INSTALL.md" "$OUT/INSTALL.md"
cp "$HW/docs/CONSOLE-CHECKLIST.md" "$OUT/CONSOLE-CHECKLIST.md"
cp "$HW/tools/stage_data.sh" "$OUT/stage_data.sh"

size=$(stat -c %s "$OUT/hotwheels-vita.vpk")
sha=$(sha256sum "$OUT/hotwheels-vita.vpk" | cut -d' ' -f1)
{
  echo "Paquete listo-para-consola — HotWheels Vita (HW-028)"
  echo "Generado: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "TITLE_ID: $VITA_TITLEID"
  echo "APP_NAME: $VITA_APP_NAME"
  echo "APP_VER:  $VITA_VERSION"
  echo "VPK:      hotwheels-vita.vpk ($size bytes)"
  echo "SHA256:   $sha"
  echo
  echo "Contenido del paquete:"
  (cd "$OUT" && ls -1 | sed 's/^/  /')
  echo
  echo "Datos del usuario (NO incluidos; copia legal):"
  echo "  Destino en la Vita: $VITA_DATA_PATH/"
  echo "  Requerido: $VITA_EXE_NAME, DATA/, HWICON.BMP  (EREG/ opcional)"
  echo "  Preparar desde este paquete (sin el repo): SRC=/path/to/Program_Files ./stage_data.sh /path/to/mount/ux0/data/hotwheels"
  echo
  echo "Antes de probar en consola: pedir autorización al usuario (AGENTS.md)."
  echo "Checklist detallado: CONSOLE-CHECKLIST.md"
} > "$OUT/MANIFEST.txt"

echo "OK $OUT ($(ls "$OUT" | wc -l) archivos, VPK $size B, sha256 ${sha:0:12}…)"
