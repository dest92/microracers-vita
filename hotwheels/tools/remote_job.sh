#!/usr/bin/env bash
# remote_job.sh — la mitad del runner que corre EN el worker (SPEC-051).
#
# Uso: remote_job.sh <root> <kind> <slots> <id> <cmd_base64>
#
#   1. guarda de disco: aborta con 76 si la WSL o el drive de Windows no dan margen
#   2. slot flock por tipo: aborta con 75 si no hay ninguno libre
#   3. Vita3K: clona VITA3K_HOME bajo el lock del home base; el clon se borra SIEMPRE
#   4. corre el comando en <root> y mide el delta de directorios de corrida
#   5. empaqueta el delta para que el host lo baje
#   6. PASS: borra la corrida en el worker. FAIL: la retiene y reporta la ruta
#
# El estado por corrida vive en <out>/.runner-<id> (prefijo '.': el detector de
# delta ignora los directorios ocultos) porque el clon de VITA3K_HOME pesa 3.7 GB
# y no debe viajar en el tar de retorno.
set -uo pipefail

root="${1:?falta root}"
kind="${2:?falta kind}"
slots="${3:?falta slots}"
id="${4:?falta id}"
cmd_b64="${5:?falta cmd_base64}"

out="$root/hotwheels/out"
build="$root/hotwheels/build-vita3k"
state="$out/.runner-$id"
archive="$out/.archive-$id.tar"
mkdir -p "$out/locks" "$build" "$state"
trap 'rm -rf -- "$state"' EXIT

# --- 1) guarda de disco: antes de tocar nada del disco del worker ---
if ! bash "$root/hotwheels/tools/remote_disk_guard.sh"; then
  echo "guarda de disco: job abortado" >&2
  exit 76
fi

# --- 2) slot: un descriptor por tipo; el que se toma queda abierto hasta salir ---
slot=-1
for ((i = 0; i < slots; i++)); do
  exec 9>"$out/locks/$kind-$i.lock"
  if flock -n 9; then slot=$i; break; fi
done
if ((slot < 0)); then
  echo "sin slot $kind disponible" >&2
  exit 75
fi

# --- 3) Vita3K: home privado por corrida ---
if [[ "$kind" == vita3k ]]; then
  base="$root/shared/Vita3K"
  if [[ ! -d "$base" ]]; then
    echo "el worker no tiene el home de Vita3K en $base" >&2
    exit 78
  fi
  profile="$state/vita3k-home"
  exec 8>"$base/.hotwheels-vita3k-run.lock"
  flock -x 8
  mkdir -p "$profile"
  cp -a --reflink=auto "$base"/. "$profile"/
  flock -u 8
  export VITA3K_HOME="$profile"
fi

# --- 3b) directorio de build del host ---
# El árbol es único, así que dos jobs que compilan el host a la vez se pisarían
# los .o (mismo contenido, escritura concurrente). El primero en llegar usa el
# build compartido y su caché; si otro job lo tiene tomado, el nuestro compila
# aparte. HW_HOST_BUILD_DIR del llamador manda.
if [[ -z "${HW_HOST_BUILD_DIR:-}" ]]; then
  shared_build="$root/hotwheels/build-host"
  if mkdir -p "$shared_build" 2>/dev/null && exec 7>"$shared_build/.build.lock" && flock -n 7; then
    export HW_HOST_BUILD_DIR="$shared_build"
  else
    export HW_HOST_BUILD_DIR="$state/build-host"
    mkdir -p "$HW_HOST_BUILD_DIR"
  fi
fi

# --- 4) corrida + delta de directorios nuevos ---
snapshot() {
  find "$out" "$build" -mindepth 1 -maxdepth 1 -type d ! -name '.*' -printf '%p\n' 2>/dev/null \
    | sed "s|^$root/||" | sort
}
snapshot > "$state/before"
cd "$root" || exit 79
set +e
bash -lc "$(printf '%s' "$cmd_b64" | base64 -d)"
rc=$?
set -e
snapshot > "$state/after"
delta_dirs="$(comm -13 "$state/before" "$state/after")"

# --- 5) tar del delta ---
if [[ -n "$delta_dirs" ]]; then
  printf '%s\n' "$delta_dirs" | tar -C "$root" -cf "$archive" -T - 2>/dev/null
else
  tar -cf "$archive" --files-from=/dev/null
fi

# --- 6) limpieza según veredicto: PASS se borra en el worker, FAIL se retiene ---
# Un directorio que otro job está escribiendo no se toca: se exige que lleve
# QUIET segundos sin escrituras. Los harnesses ya borran su propia salida en
# PASS; lo que quede de un comando que deja salida lo podaría prune_out.sh.
QUIET="${HW_RUN_QUIET_SEC:-30}"
quiet_dir() {
  local d="$1"
  [[ -d "$d" ]] || return 0
  ((QUIET <= 0)) && return 0
  ! find "$d" -newermt "@$(( $(date +%s) - QUIET ))" -print -quit 2>/dev/null | grep -q .
}
if ((rc == 0)); then
  while IFS= read -r d; do
    [[ -n "$d" ]] || continue
    if quiet_dir "$root/$d"; then
      rm -rf -- "$root/$d"
    else
      echo "retenido_en_worker: $root/$d (escrito hace menos de ${QUIET}s)"
    fi
  done <<<"$delta_dirs"
else
  while IFS= read -r d; do
    [[ -n "$d" ]] && echo "retenido_en_worker: $root/$d"
  done <<<"$delta_dirs"
fi

# estado de disco al terminar (la guarda ya corrió; acá es sólo el reporte)
bash "$root/hotwheels/tools/remote_disk_guard.sh" || echo "guarda de disco: el job superó los límites al terminar" >&2

printf 'REMOTE_RUN_ARCHIVE=%s\n' "$archive"
printf 'REMOTE_RUN_RC=%s\n' "$rc"
exit "$rc"
