#!/usr/bin/env bash
# vita3k.sh - manejo del emulador Vita3K (release continuous) para validar
# homebrew del port Hot Wheels Micro Racers.
#
# Subcomandos:
#   setup      Descarga AppImage + firmware oficial Sony, extrae, construye la
#              imagen Docker e instala ambos firmware.
#   firmware   Instala PSVUPDAT.PUP (3.74) y PSP2UPDAT.PUP en la Vita virtual.
#   run [..]   Corre Vita3K. Cualquier argumento extra se pasa al emulador
#              (por ej: `run -r HWMR00001`, `run hotwheels/tools/.../hello.vpk`).
#   shell      Abre un bash dentro del contenedor.
#
# Por que contenedor: ver hotwheels/docs/vita3k.md y el Dockerfile de al lado.
#
# OJO con el content-path posicional: el parser de Vita3K usa
# allow_windows_style_options(), asi que un path ABSOLUTO (/...) se interpreta
# como opcion y se descarta. Pasar rutas RELATIVAS a /work (el repo).
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SELF_DIR/../../.." && pwd)"
VDIR="$ROOT/shared/Vita3K"
APPDIR="$VDIR/appdir"
FWDIR="$VDIR/firmware"
HOMEDIR="$VDIR/home"
IMAGE="vita3k-runtime:26.04"

APPIMAGE_URL="https://github.com/Vita3K/Vita3K/releases/download/continuous/Vita3K-x86_64.AppImage"
PSV_URL="http://dus01.psv.update.playstation.net/update/psv/image/2022_0209/rel_f2c7b12fe85496ec88a0391b514d6e3b/PSVUPDAT.PUP"
PSP2_URL="http://dus01.psp2.update.playstation.net/update/psp2/image/2019_0924/sd_8b5f60b56c3da8365b973dba570c53a5/PSP2UPDAT.PUP?dest=us"

log() { echo "[vita3k] $*"; }

ensure_image() {
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        log "construyendo imagen $IMAGE ..."
        docker build -t "$IMAGE" -f "$SELF_DIR/Dockerfile" "$SELF_DIR"
    fi
}

# Genera (una vez) el config.yml y lo deja listo para uso no interactivo.
seed_config() {
    ensure_image
    mkdir -p "$HOMEDIR"
    local cfg="$HOMEDIR/.config/Vita3K/config.yml"
    if [ ! -f "$cfg" ]; then
        # --version genera el config por defecto sin abrir GUI.
        docker run --rm --user "$(id -u):$(id -g)" -e HOME=/vita/home \
            -v "$VDIR":/vita "$IMAGE" \
            /vita/appdir/usr/bin/Vita3K --version >/dev/null 2>&1 || true
    fi
    [ -f "$cfg" ] || { log "ERROR: no se pudo generar $cfg"; return 1; }
    python3 - "$cfg" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p).read()
def setk(s, k, v):
    if re.search(r'(?m)^%s:.*$' % re.escape(k), s):
        return re.sub(r'(?m)^%s:.*$' % re.escape(k), '%s: %s' % (k, v), s)
    return s.rstrip() + '\n%s: %s\n' % (k, v)
# pref-path evita un crash de create_directories en el path CLI del release;
# show-welcome/check-for-updates apagan modales que bloquean sin interaccion.
for k, v in [('pref-path', '/vita/home/.local/share/Vita3K/Vita3K'),
             ('show-welcome', 'false'),
             ('check-for-updates', 'false'),
             ('warn-missing-firmware', 'false'),
             ('discord-rich-presence', 'false')]:
    s = setk(s, k, v)
open(p, 'w').write(s)
PY
}

# Ejecuta el binario dentro del contenedor con el DISPLAY del host.
run_bin() {
    ensure_image
    mkdir -p "$HOMEDIR"
    local xargs=()
    if [ -n "${DISPLAY:-}" ] && [ -d /tmp/.X11-unix ]; then
        xargs+=(-v /tmp/.X11-unix:/tmp/.X11-unix -e DISPLAY="$DISPLAY")
        if [ -n "${XAUTHORITY:-}" ] && [ -f "${XAUTHORITY}" ]; then
            xargs+=(-v "$XAUTHORITY":/tmp/.Xauth:ro -e XAUTHORITY=/tmp/.Xauth)
        fi
    fi
    docker run --rm --user "$(id -u):$(id -g)" \
        -e HOME=/vita/home -e LANG=C.UTF-8 -e APPDIR=/vita/appdir \
        -e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
        -e LD_LIBRARY_PATH=/vita/appdir/usr/lib \
        -e QT_PLUGIN_PATH=/vita/appdir/usr/plugins \
        "${xargs[@]}" \
        -v "$VDIR":/vita -v "$ROOT":/work:ro -w /work \
        "$IMAGE" /vita/appdir/usr/bin/Vita3K "$@"
}

cmd_setup() {
    mkdir -p "$VDIR" "$APPDIR" "$FWDIR" "$HOMEDIR"
    if [ ! -x "$VDIR/Vita3K-x86_64.AppImage" ]; then
        log "descargando AppImage ..."
        curl -fL --retry 3 -o "$VDIR/Vita3K-x86_64.AppImage" "$APPIMAGE_URL"
        chmod +x "$VDIR/Vita3K-x86_64.AppImage"
    fi
    if [ ! -f "$APPDIR/usr/bin/Vita3K" ]; then
        log "extrayendo AppImage a appdir/ ..."
        ( cd "$APPDIR" \
          && "$VDIR/Vita3K-x86_64.AppImage" --appimage-extract >/dev/null \
          && cp -a squashfs-root/. "$APPDIR"/ \
          && rm -rf squashfs-root )
    fi
    [ -f "$FWDIR/PSVUPDAT.PUP" ]  || { log "descargando PSVUPDAT.PUP ...";  curl -fL --retry 3 -o "$FWDIR/PSVUPDAT.PUP" "$PSV_URL"; }
    [ -f "$FWDIR/PSP2UPDAT.PUP" ] || { log "descargando PSP2UPDAT.PUP ..."; curl -fL --retry 3 -o "$FWDIR/PSP2UPDAT.PUP" "$PSP2_URL"; }
    ensure_image
    seed_config
    cmd_firmware
    log "setup completo."
}

cmd_firmware() {
    seed_config
    for f in PSVUPDAT.PUP PSP2UPDAT.PUP; do
        [ -f "$FWDIR/$f" ] || { log "ERROR: falta $FWDIR/$f (correr setup)"; exit 1; }
        log "instalando $f ..."
        run_bin --firmware "/vita/firmware/$f" --log-level 2
    done
}

cmd_shell() {
    ensure_image
    docker run --rm -it --user "$(id -u):$(id -g)" -e HOME=/vita/home \
        -e LD_LIBRARY_PATH=/vita/appdir/usr/lib \
        -e QT_PLUGIN_PATH=/vita/appdir/usr/plugins \
        -v "$VDIR":/vita -v "$ROOT":/work:ro -w /work \
        "$IMAGE" bash
}

case "${1:-run}" in
    setup)    cmd_setup ;;
    firmware) cmd_firmware ;;
    shell)    cmd_shell ;;
    run)      shift || true; seed_config; run_bin "$@" ;;
    -h|--help|help)
        sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        ;;
    *)        seed_config; run_bin "$@" ;;
esac
