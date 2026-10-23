#!/usr/bin/env bash
# vita3k_run.sh — harness AUTOMÁTICO de Vita3K para homebrew (HW-014).
#
#   vita3k_run.sh <vpk> [segundos]
#
# Hace, sin intervención humana y sin tocar el escritorio del usuario:
#   1. Se apoya en la imagen de deepseek `vita3k-runtime:26.04` y en una capa
#      propia `vita3k-headless:26.04` (Xvfb + ImageMagick) — ver
#      Dockerfile.vita3k-headless.
#   2. Instala el .vpk (content-path posicional; ver CLI real más abajo).
#   3. Lo corre con `-r <TITLEID>` bajo un Xvfb :98 DENTRO del contenedor.
#   4. Captura stdout del emulador (incluye `sceClibPrintf`/log de Vita3K,
#      line-buffered con stdbuf) y saca screenshots periódicos con `import`.
#   5. Opcionalmente inyecta teclado con HW_VITA3K_KEY_EVENTS
#      (segundo:keysym:duración,...), registrando send_keys.log.
#   6. Mata todo (contenedor + Xvfb) al terminar o al vencer el timeout.
#
# CLI real de Vita3K (v0.2.1 4115, verificada con --help):
#   [content-path]              .vpk/.zip o carpeta -> instalar & correr
#   -r, --installed-path TEXT   correr app instalada por TitleID
#   --firmware PATH             instalar firmware
#   -B, --backend-renderer {OpenGL,Vulkan}
#   -l, --log-level 0..6        (0=TRACE .. 6=OFF)
#   -z, --console [0]           modo consola
#   -f/-c, --load-config/--config-location PATH
#   No hay --installer/-i ni --no-gui: la instalación es el content-path posicional.
#   Quirk: un content-path ABSOLUTO se descarta (CLI11 allow_windows_style_options),
#   por eso se pasa siempre RELATIVO a /work (= repo montado).
set -euo pipefail
ulimit -c 0

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SELF_DIR/../.." && pwd)"
VDIR="${VITA3K_HOME:-$ROOT/shared/Vita3K}"
BASE_IMAGE="vita3k-runtime:26.04"
IMAGE="vita3k-headless:26.10-xtest2"
DISPLAY_NUM=":98"
SCREEN="1280x720x24"

usage() { sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 1 ] || usage
VPK="$1"
SECS="${2:-20}"
case "$SECS" in ''|*[!0-9]*) echo "segundos inválidos: $SECS" >&2; exit 2;; esac
if [ -n "${HW_VITA3K_KEY_EVENTS:-}" ] && [ -z "${HW_VITA3K_INPUT_EXPECT:-}" ]; then
    echo "ERROR: HW_VITA3K_KEY_EVENTS requiere HW_VITA3K_INPUT_EXPECT (waypoint-keymask:1..15)" >&2
    exit 2
fi
if [ -n "${HW_VITA3K_KEY_EVENTS:-}" ] && ! [[ "$HW_VITA3K_INPUT_EXPECT" =~ ^waypoint-keymask:([1-9]|1[0-5])$ ]]; then
    echo "ERROR: HW_VITA3K_INPUT_EXPECT debe ser waypoint-keymask:1..15" >&2
    exit 2
fi

# VPK -> absoluto y relativo a ROOT (el content-path debe ser relativo a /work).
VPK_ABS="$(realpath "$VPK")"
case "$VPK_ABS" in
    "$ROOT"/*) VPK_REL="${VPK_ABS#"$ROOT"/}" ;;
    *) echo "ERROR: el vpk debe estar dentro del repo ($ROOT) para montarlo en /work" >&2; exit 2 ;;
esac
[ -f "$VPK_ABS" ] || { echo "ERROR: no existe $VPK_ABS" >&2; exit 2; }

# TITLEID: env TITLEID, o parseado del sce_sys/param.sfo dentro del vpk.
TITLEID="${TITLEID:-}"
if [ -z "$TITLEID" ]; then
    TITLEID="$(python3 - "$VPK_ABS" <<'PY'
import sys, zipfile, struct
def parse_sfo(d):
    magic, ver, koff, doff, n = struct.unpack_from('<IIIII', d, 0)
    for i in range(n):
        ko, fmt, dlen, dmax, dof = struct.unpack_from('<HHIII', d, 20 + i*16)
        k = d[koff+ko:d.index(b'\0', koff+ko)].decode('latin1')
        if k == 'TITLE_ID':
            raw = d[doff+dof:doff+dof+dlen]
            return raw.split(b'\0')[0].decode('latin1')
    return ''
try:
    with zipfile.ZipFile(sys.argv[1]) as z:
        name = next((x for x in z.namelist() if x.endswith('param.sfo')), None)
        print(parse_sfo(z.read(name)) if name else '')
except Exception:
    print('')
PY
)"
fi
[ -n "$TITLEID" ] || { echo "ERROR: no pude determinar TITLEID (pasá TITLEID=... en el entorno)" >&2; exit 2; }

# Imágenes.
docker image inspect "$BASE_IMAGE" >/dev/null 2>&1 || {
    echo "ERROR: falta $BASE_IMAGE. Corré primero: hotwheels/tools/vita3k/vita3k.sh setup" >&2; exit 1; }
docker image inspect "$IMAGE" >/dev/null 2>&1 || {
    echo "== construyendo $IMAGE"; docker build -t "$IMAGE" -f "$SELF_DIR/Dockerfile.vita3k-headless" "$SELF_DIR" >/dev/null; }

# El home contiene settings, datos y logs append-only compartidos. Serializar
# stage + snapshot + Vita3K + extracción evita mezclar corridas concurrentes.
mkdir -p "$VDIR"
exec {VITA3K_LOCK_FD}>"$VDIR/.hotwheels-vita3k-run.lock"
flock "$VITA3K_LOCK_FD"

# Vita3K enumera UPDATE_STARTUP_OFF como 0; fuerce ese modo en el config
# persistente del contenedor para que el diálogo de actualización no tape shots.
VITA3K_CONFIG="$VDIR/home/.config/Vita3K/config.yml"
mkdir -p "$(dirname "$VITA3K_CONFIG")"
python3 - "$VITA3K_CONFIG" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text() if path.exists() else ""
key = re.compile(r"(?m)^check-for-updates-mode\s*:")
if key.search(text):
    text = re.sub(r"(?m)^check-for-updates-mode\s*:[^\n]*", "check-for-updates-mode: 0", text)
else:
    text = re.sub(r"(?m)^check-for-updates\s*:[^\n]*", "", text)
    text = text.rstrip() + "\ncheck-for-updates-mode: 0\n"
path.write_text(text)
assert re.search(r"(?m)^check-for-updates-mode:\s*0\s*$", text)
PY
echo "== Vita3K: check-for-updates-mode=0 (OFF)"

# Datos del homebrew: si STAGE=<dir>, su contenido se copia a ux0:data/ antes de
# correr (p. ej. la app espera ux0:data/hotwheels/HotWheels.exe).
if [ -n "${STAGE:-}" ]; then
    [ -d "$STAGE" ] || { echo "ERROR: STAGE=$STAGE no es un directorio" >&2; exit 2; }
    DST="$VDIR/home/.local/share/Vita3K/Vita3K/ux0/data"
    mkdir -p "$DST"
    cp -a "$STAGE"/. "$DST"/
    echo "== STAGE: $STAGE -> ux0:data/"
fi

# libshacccg.suprx opcional, provisto por el usuario (SHACCCG=/ruta/al/modulo).
# No se puede distribuir; si existe, se copia a ur0:/data/ de la Vita virtual.
if [ -n "${SHACCCG:-}" ]; then
    [ -f "$SHACCCG" ] || { echo "ERROR: SHACCCG=$SHACCCG no existe" >&2; exit 2; }
    mkdir -p "$VDIR/home/.local/share/Vita3K/Vita3K/ur0/data"
    cp -f "$SHACCCG" "$VDIR/home/.local/share/Vita3K/Vita3K/ur0/data/libshacccg.suprx"
    echo "== libshacccg.suprx provisto (SHACCCG) -> ur0:/data/libshacccg.suprx"
fi

# Salida (el prefijo 'build' la deja gitignoreada por el patrón build*/).
STAMP="$(date +%Y%m%d-%H%M%S%N)-$$"
OUT="$ROOT/hotwheels/build-vita3k/$(basename "$VPK_ABS" .vpk)-$STAMP"
mkdir -p "$OUT"
VITA_DATA="$VDIR/home/.local/share/Vita3K/Vita3K/ux0/data/hotwheels"
AUDIO_LOG="$VITA_DATA/audio.log"
PROGRESS_LOG="$VITA_DATA/progress.txt"
VITA3K_LOG="$VDIR/home/.cache/Vita3K/vita3k.log"
AUDIO_LOG_STATE="$OUT/audio.log.state"
PROGRESS_LOG_STATE="$OUT/progress.txt.state"
VITA3K_LOG_STATE="$OUT/vita3k.log.state"
python3 "$ROOT/hotwheels/tools/extract_log_delta.py" snapshot "$AUDIO_LOG" "$AUDIO_LOG_STATE"
python3 "$ROOT/hotwheels/tools/extract_log_delta.py" snapshot "$PROGRESS_LOG" "$PROGRESS_LOG_STATE"
python3 "$ROOT/hotwheels/tools/extract_log_delta.py" snapshot "$VITA3K_LOG" "$VITA3K_LOG_STATE"

# inner.sh: corre DENTRO del contenedor. Recibe: VPK_REL TITLEID SECS.
cat > "$OUT/inner.sh" <<'INNER'
#!/bin/bash
set -u
ulimit -c 0
VPK_REL="$1"; TITLEID="$2"; SECS="$3"; FRAMES_ALL="${HW_FRAMES_ALL:-0}"
BIN=/vita/appdir/usr/bin/Vita3K
export LD_LIBRARY_PATH=/vita/appdir/usr/lib
export QT_PLUGIN_PATH=/vita/appdir/usr/plugins
export LC_ALL=C.UTF-8

# Fix del layout de la AppImage: Vita3K busca shaders-builtin junto al binario,
# pero el AppImage los trae en usr/share/Vita3K/. Symlink idempotente.
ln -sfn ../share/Vita3K/shaders-builtin /vita/appdir/usr/bin/shaders-builtin 2>/dev/null || true

Xvfb :98 -screen 0 1280x720x24 >/out/xvfb.log 2>&1 & XV=$!
sleep 2
export DISPLAY=:98

cd /work
# --- pasada 1: instalar (content-path posicional) ---
stdbuf -oL -eL "$BIN" "$VPK_REL" -l 0 >/out/install.stdout 2>&1 & IP=$!
for _ in $(seq 1 20); do
    grep -q 'installed successfully' /out/install.stdout 2>/dev/null && break
    sleep 1
done
kill -TERM "$IP" 2>/dev/null; sleep 2; kill -9 "$IP" 2>/dev/null

# --- pasada 2: correr instalada por TitleID, screenshots periódicos ---
stdbuf -oL -eL "$BIN" -r "$TITLEID" -l 0 >/out/run.stdout 2>&1 & RP=$!
KP=""
INPUT_RC=0
if [ -n "${HW_VITA3K_KEY_EVENTS:-}" ]; then
    timeout "$SECS" /usr/local/bin/hw-send-keys "$HW_VITA3K_KEY_EVENTS" \
        >/out/send_keys.log 2>&1 & KP=$!
fi
i=0
while [ "$i" -le "$SECS" ]; do
    if [ "$FRAMES_ALL" = 1 ] || [ "$((i % 50))" -eq 0 ] || [ "$i" -ge "$((SECS > 20 ? SECS - 20 : 0))" ]; then
        import -window root "$(printf '/out/shot_%03d.png' "$i")" 2>/dev/null || true
    fi
    i=$((i + 1))
    sleep 1
done
if [ -n "$KP" ]; then
    if wait "$KP"; then
        echo "XTest terminó; recepción por el guest aún no verificada" >>/out/send_keys.log
    else
        INPUT_RC=$?
        echo "XTest falló rc=$INPUT_RC" >>/out/send_keys.log
    fi
fi
kill -TERM "$RP" 2>/dev/null; sleep 2; kill -9 "$RP" 2>/dev/null
kill "$XV" 2>/dev/null

exit "$INPUT_RC"
INNER
chmod +x "$OUT/inner.sh"

# Contenedor detached + cleanup garantizado.
NAME="hw014-$(basename "$VPK_ABS" .vpk)-$$"
cleanup() { docker rm -f "$NAME" >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM

RUN_RC=0
if docker run -d --name "$NAME" \
    --user "$(id -u):$(id -g)" \
    -e HOME=/vita/home -e LC_ALL=C.UTF-8 -e SDL_AUDIODRIVER="${V3K_AUDIODRIVER:-dummy}" \
    -e HW_FRAMES_ALL="${HW_FRAMES_ALL:-0}" -e HW_WAV="${HW_WAV:-0}" \
    -e "HW_VITA3K_KEY_EVENTS=${HW_VITA3K_KEY_EVENTS:-}" \
    -e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
    -v "$VDIR":/vita -v "$ROOT":/work:ro -v "$OUT":/out \
    -w /work "$IMAGE" bash /out/inner.sh "$VPK_REL" "$TITLEID" "$SECS" >/dev/null; then
    :
else
    RUN_RC=$?
    echo "== docker run falló (rc=$RUN_RC)"
fi

TIMED_OUT=0
if [ "$RUN_RC" -eq 0 ]; then
    echo "== corriendo $TITLEID ($VPK_REL) por ${SECS}s en $IMAGE"
    DEADLINE=$(( $(date +%s) + SECS + 90 ))
    while :; do
        RUNNING="$(docker inspect -f '{{.State.Running}}' "$NAME" 2>/dev/null || echo false)"
        [ "$RUNNING" = "true" ] || break
        [ "$(date +%s)" -lt "$DEADLINE" ] || { echo "== timeout: matando $NAME"; TIMED_OUT=1; docker rm -f "$NAME" >/dev/null 2>&1 || true; break; }
        sleep 2
    done
    CONTAINER_RC="$(docker wait "$NAME" 2>/dev/null || echo 1)"
else
    CONTAINER_RC="$RUN_RC"
fi
python3 "$ROOT/hotwheels/tools/extract_log_delta.py" extract "$AUDIO_LOG" "$AUDIO_LOG_STATE" "$OUT/audio.log"
python3 "$ROOT/hotwheels/tools/extract_log_delta.py" extract "$PROGRESS_LOG" "$PROGRESS_LOG_STATE" "$OUT/progress.txt"
python3 "$ROOT/hotwheels/tools/extract_log_delta.py" extract "$VITA3K_LOG" "$VITA3K_LOG_STATE" "$OUT/vita3k.log"

BOOT="$(grep -c 'boot_game_once\|Game started' "$OUT/run.stdout" 2>/dev/null || true)"
[ -n "$BOOT" ] || BOOT=0
SHOTS="$(find "$OUT" -maxdepth 1 -name 'shot_*.png' -type f | wc -l)"
echo "== print: $(grep -c 'sceClibPrintf' "$OUT/run.stdout" 2>/dev/null || echo 0) líneas"
ERRS="$(grep -c '|E|\||C|' "$OUT/run.stdout" 2>/dev/null || true)"
echo "== errores del emulador: ${ERRS:-0} (ver run.stdout)"
echo "== boot: $BOOT líneas; screenshots=$SHOTS"
grep -aE 'boot_game_once|sceClibPrintf' "$OUT/run.stdout" 2>/dev/null | tail -5 || true
INPUT_VERDICT=1
if [ -n "${HW_VITA3K_KEY_EVENTS:-}" ]; then
    if python3 "$ROOT/hotwheels/tools/vita3k/check_guest_input.py" "$HW_VITA3K_INPUT_EXPECT" "$OUT/run.stdout"; then
        echo "== guest input observado ($HW_VITA3K_INPUT_EXPECT)"
    else
        echo "== guest input ausente ($HW_VITA3K_INPUT_EXPECT)"
        INPUT_VERDICT=0
    fi
fi
if [ "$BOOT" -gt 0 ] && [ "$CONTAINER_RC" = 0 ] && [ "$TIMED_OUT" -eq 0 ] && [ "$INPUT_VERDICT" -eq 1 ]; then VERDICT=PASS; else VERDICT=FAIL; fi
echo "== resultado: $VERDICT (boot=$BOOT screenshots=$SHOTS container_rc=$CONTAINER_RC timeout=$TIMED_OUT)"
echo "== salida: $OUT"
if [ "$VERDICT" = PASS ] && [ "${HW_KEEP_OUT:-0}" != 1 ]; then rm -rf -- "$OUT"; fi
if [ "${HW_KEEP_OUT:-0}" = 1 ]; then KEEP=999 bash "$ROOT/hotwheels/tools/prune_out.sh" >/dev/null 2>&1 || true
else bash "$ROOT/hotwheels/tools/prune_out.sh" >/dev/null 2>&1 || true
fi
[ "$VERDICT" = PASS ]
