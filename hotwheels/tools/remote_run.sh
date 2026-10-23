#!/usr/bin/env bash
# remote_run.sh — corre un comando del repo en el worker remoto (SPEC-051).
#
#   remote_run.sh <cmd...>
#
# Reglas de disco del incidente 2026-10-21 (el runner llenó el C: del usuario):
#   * UN árbol sincronizado: el rsync actualiza $REMOTE_ROOT en el worker; no se
#     copia el repo por corrida (cada copia ocupaba ~1.3 GB y las N copias
#     simultáneas fueron lo que dejó al usuario sin espacio).
#   * Cada corrida crea sólo su directorio de salida (y el clon de VITA3K_HOME
#     en Vita3K, que se borra siempre).
#   * Guardas de disco antes de ejecutar (60 GB usados en la WSL, 50 GB libres en
#     el drive de Windows) y reporte de uso en cada corrida.
#   * Corrida PASS: el directorio de corrida se borra en el worker tras traer el
#     delta. FAIL: se retiene y se reporta la ruta.
#   * El exit code del comando remoto es el único veredicto.
set -euo pipefail
ulimit -c 0
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${REMOTE_RUN_HOST:?set REMOTE_RUN_HOST to your SSH alias}"
REMOTE_ROOT="${REMOTE_RUN_ROOT:?set REMOTE_RUN_ROOT to the worker checkout}"
RSYNC_PATH="${REMOTE_RUN_RSYNC_PATH:-wsl -d Ubuntu -- rsync}"
RSYNC_BIN="${REMOTE_RUN_RSYNC_BIN:-rsync}"
OUT_ROOT="${REMOTE_RUN_OUTPUT_ROOT:-$ROOT/hotwheels/out}"
SSH_PATH="${REMOTE_RUN_SSH_PATH:-ssh}"
[[ $# -gt 0 ]] || { echo "uso: remote_run.sh <cmd...>" >&2; exit 2; }

joined="$*"
kind="${REMOTE_RUN_KIND:-}"
if [[ -z "$kind" ]]; then
  case "$joined" in *vita3k*|*Vita3K*) kind=vita3k ;; *) kind=qemu ;; esac
fi
case "$kind" in
  vita3k) slots=3 ;;
  qemu) slots=10 ;;
  *) echo "REMOTE_RUN_KIND debe ser vita3k o qemu" >&2; exit 2 ;;
esac

run_id="$(date +%Y%m%d-%H%M%S)-$$"

# --- 1) sincronización incremental contra el árbol único del worker ---
# Sólo viajan los archivos que el comando necesita de hotwheels/out/; el resto
# de out/ y los directorios pesados quedan excluidos (y por eso protegidos de
# --delete: son datos del worker, no fuentes).
rsync_args=(-a --delete --mkpath --rsync-path="$RSYNC_PATH")
for arg in "$@"; do
  case "$arg" in hotwheels/out/*|hotwheels/build-vita/*|hotwheels/build-vita3k/*)
    p="$arg"
    while [[ "$p" == */* ]]; do p="${p%/*}"; rsync_args+=(--include="/$p/"); done
    rsync_args+=(--include="/$arg")
    ;;
  esac
done
rsync_args+=(--include=/d2vita/ --include=/d2vita/D2Vita/ --include=/d2vita/D2Vita/third_party/ --include=/d2vita/D2Vita/third_party/winx86/***)
# Los directorios de build son artefactos (gitignored), no fuentes: no se
# transfieren y --delete no los borra del worker, que conserva su propia
# compilación. El artefacto que un comando nombra entra por los --include de
# arriba (VPK de build-vita/, out/ o build-vita3k/).
rsync_args+=(--exclude=/hotwheels/out/*** --exclude=/hotwheels/build-vita3k/*** --exclude=/hotwheels/build*/*** \
  --exclude=/hotwheels/wineprefix/*** \
  --exclude=/shared/Vita3K/*** --exclude=/shared/Vita3K-v0.2.1-4115/*** --exclude=/shared/opt/*** \
  --exclude=/shared/vitasdk/*** --exclude=/shared/ghidra-proyectos/*** --exclude=/wineprefix/*** \
  --exclude=/d2-juego/*** --exclude=/d2vita/*** --exclude=/d2vita/d2-juego/*** \
  --exclude=/.wt-*/*** --exclude=/.commandcode/*** --exclude=/core '--exclude=*.core')
set +e
"$RSYNC_BIN" "${rsync_args[@]}" "$ROOT/" "$HOST:$REMOTE_ROOT/"
sync_rc=$?
set -e
if ((sync_rc != 0)); then
  echo "remote_run: la sincronización falló (rc=$sync_rc); no se corre el comando" >&2
  exit "$sync_rc"
fi

# --- 2) job remoto (guarda, slot, corrida, delta, limpieza) ---
cmd_q=""
for arg in "$@"; do printf -v q '%q' "$arg"; cmd_q+=" $q"; done
cmd_b64="$(printf '%s' "$cmd_q" | base64 -w0)"
head_sha="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo desconocido)"
remote_script=$(cat <<EOF_REMOTE
set -u
root='$REMOTE_ROOT'
id='$run_id'
kind='$kind'
slots='$slots'
head='$head_sha'
printf 'remote_run: kind=%s id=%s head=%s\\n' "\$kind" "\$id" "\$head"
bash "\$root/hotwheels/tools/remote_job.sh" "\$root" "\$kind" "\$slots" "\$id" '$cmd_b64'
EOF_REMOTE
)
encoded="$(printf '%s' "$remote_script" | base64 -w0)"
remote_cmd="wsl -d Ubuntu -- bash -lc \"printf %s $encoded | base64 -d | bash\""
remote_out="$(mktemp)"
trap 'rm -f "$remote_out"' EXIT
set +e
"$SSH_PATH" "$HOST" "$remote_cmd" > "$remote_out" 2>&1
remote_rc=$?
set -e
cat "$remote_out"
archive="$(sed -n 's/^REMOTE_RUN_ARCHIVE=//p' "$remote_out" | tail -1)"

# --- 3) traer el delta y limpiar el tar remoto ---
local_dir="$OUT_ROOT/remote-$run_id"
mkdir -p "$local_dir"
rec_rc=0
if [[ -n "$archive" ]]; then
  "$RSYNC_BIN" -a --rsync-path="$RSYNC_PATH" "$HOST:$archive" "$local_dir/run.tar" 2>/dev/null || rec_rc=$?
else
  rec_rc=1
fi
if ((rec_rc == 0)); then
  tar -xf "$local_dir/run.tar" -C "$local_dir" || rec_rc=$?
fi
if ((rec_rc == 0)); then
  unlink "$local_dir/run.tar"
  cleanup="rm -f -- '$archive'"
  clean64="$(printf '%s' "$cleanup" | base64 -w0)"
  clean_rc=0
  "$SSH_PATH" "$HOST" "wsl -d Ubuntu -- bash -lc \"printf %s $clean64 | base64 -d | bash\"" >/dev/null 2>&1 || clean_rc=$?
  ((clean_rc == 0)) || echo "remote_run: aviso limpieza remota rc=$clean_rc" >&2
else
  echo "remote_run: aviso recuperación de salidas rc=$rec_rc; tar remoto ${archive:-<ninguno>}" >&2
  rmdir "$local_dir" 2>/dev/null || true
fi
echo "remote_run: kind=$kind rc=$remote_rc salida=$local_dir"
exit "$remote_rc"
