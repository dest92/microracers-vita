#!/usr/bin/env bash
# hotwheels/tools/stage_data.sh — prepara los datos del juego para el usuario (HW-017).
#
# Copia SOLO los archivos que el juego lee en runtime desde
# hotwheels/extracted/original/Program_Files/ (salida de HW-001) hacia el
# destino que indique el usuario (normalmente la memoria de la Vita, en
# ux0:data/hotwheels/). NO empaqueta datos del juego en el VPK ni los commitea:
# el usuario debe poseer su copia legal.
#
# Uso:
#   stage_data.sh <DESTINO> [--no-ereg]
#     <DESTINO>  ruta de la carpeta ux0:data/hotwheels en la Vita montada
#                (ej: /run/media/$USER/PSVITA/ux0/data/hotwheels)
#     --no-ereg  omite EREG/ (registro electrónico; el juego no lo usa, FINDINGS #11)
#
# Variables: SRC=<dir>  (default: primer candidato existente — ver abajo)
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$(cd "$(dirname "$0")" && pwd)"
# Resolución de SRC (el paquete extraído no tiene el repo):
#   1. $SRC del entorno (obligatorio fuera del repo: ver docs/INSTALL.md §2)
#   2. repo:  <HW>/extracted/original/Program_Files
#   3. al lado del script: ./Program_Files o ./extracted/original/Program_Files
if [ -z "${SRC:-}" ]; then
  for c in "$HW/extracted/original/Program_Files" \
           "$DIR/Program_Files" \
           "$DIR/extracted/original/Program_Files"; do
    if [ -d "$c" ]; then SRC="$c"; break; fi
  done
fi
SRC="${SRC:-$HW/extracted/original/Program_Files}"

DEST=""
NO_EREG=0
for arg in "$@"; do
  case "$arg" in
    --no-ereg) NO_EREG=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) DEST="$arg" ;;
  esac
done

[ -n "$DEST" ] || { echo "uso: stage_data.sh <DESTINO> [--no-ereg]" >&2; exit 2; }
[ -d "$SRC" ] || {
  echo "stage_data: no existe $SRC" >&2
  echo "             desde el paquete (sin repo) pasá la ruta de tus datos:" >&2
  echo "               SRC=/ruta/a/Program_Files ./stage_data.sh <DESTINO> [--no-ereg]" >&2
  echo "             en el repo: corré primero la extracción de HW-001 (docs/triage.md)" >&2
  exit 1
}
[ -f "$SRC/HotWheels.exe" ] || { echo "stage_data: falta HotWheels.exe en $SRC" >&2; exit 1; }

mkdir -p "$DEST"
find_settings() {
  find "$1" -type f -ipath '*/data/menu/settings.dat' -print -quit 2>/dev/null || true
}
# Preserve the user's file across the recursive source copy. A stage refresh
# must not replace preferences already written by the game.
DEST_SETTINGS_BEFORE="$(find_settings "$DEST")"
SETTINGS_BACKUP=""
cleanup_settings_backup() {
  [ -z "$SETTINGS_BACKUP" ] || rm -f -- "$SETTINGS_BACKUP"
}
trap cleanup_settings_backup EXIT
if [ -n "$DEST_SETTINGS_BEFORE" ]; then
  SETTINGS_BACKUP="$(mktemp)"
  cp -p -- "$DEST_SETTINGS_BEFORE" "$SETTINGS_BACKUP"
fi
if command -v rsync >/dev/null 2>&1; then
  EXCL=()
  [ "$NO_EREG" = 1 ] && EXCL+=(--exclude EREG/)
  rsync -a "${EXCL[@]}" "$SRC"/ "$DEST"/
else
  cp -a "$SRC"/. "$DEST"/
  [ "$NO_EREG" = 1 ] && rm -rf "$DEST/EREG"
fi
if [ -n "$SETTINGS_BACKUP" ]; then
  cp -p -- "$SETTINGS_BACKUP" "$DEST_SETTINGS_BEFORE"
  cleanup_settings_backup
  SETTINGS_BACKUP=""
fi

# The game's second settings.dat integer selects race resolution (0=320, 1=640,
# 2=800). Seed max resolution for a fresh install and raise source defaults,
# while preserving an already installed user's file when the source has none.
SOURCE_SETTINGS="$(find_settings "$SRC")"
DEST_SETTINGS="$(find_settings "$DEST")"
if [ -n "$SOURCE_SETTINGS" ] && [ -z "$DEST_SETTINGS_BEFORE" ]; then
  python3 - "$DEST_SETTINGS" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
data = p.read_bytes()
newline = b"\r\n" if b"\r\n" in data else b"\n"
lines = data.decode("ascii").splitlines()
if not lines:
    lines = ["0"]
while len(lines) < 2:
    lines.append("0")
lines[1] = "2"
trailing = data.endswith((b"\r", b"\n"))
p.write_bytes(newline.join(line.encode("ascii") for line in lines) + (newline if trailing else b""))
PY
elif [ -z "$DEST_SETTINGS" ]; then
  mkdir -p "$DEST/DATA/MENU"
  printf '0\r\n2\r\n' > "$DEST/DATA/MENU/settings.dat"
fi

echo "OK stage_data: $(find "$DEST" -type f | wc -l) archivos ($(du -sh "$DEST" | cut -f1)) en $DEST"
echo "   En la Vita debe quedar en ux0:data/hotwheels/ (el runtime usa esa ruta)."
