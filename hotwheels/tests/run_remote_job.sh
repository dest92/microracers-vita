#!/usr/bin/env bash
# tests/test_remote_job.sh — contrato del job remoto del worker (SPEC-051 CA7/CA9/CA10).
#
# remote_job.sh es la mitad del runner que corre EN el worker: guarda de disco,
# slot, clon de VITA3K_HOME, corrida, tar del delta y limpieza según veredicto.
# Se prueba acá contra un root falso, sin worker ni emulador.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOB="$ROOT/hotwheels/tools/remote_job.sh"
GUARD="$ROOT/hotwheels/tools/remote_disk_guard.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "test_remote_job: FAIL: $1" >&2; exit 1; }
b64() { printf '%s' "$1" | base64 -w0; }

# El root falso no es el worker: se inyectan las medidas de un worker sano.
export HW_GUARD_ROOT_USED_KB=$((16 * 1024 * 1024))
export HW_GUARD_ROOT_AVAIL_KB=$((940 * 1024 * 1024))
export HW_GUARD_WIN_FREE_KB=$((401 * 1024 * 1024))
# Sin margen de asentamiento, para que la limpieza de una corrida sea inmediata.
export HW_RUN_QUIET_SEC=0

# Root falso: el mismo layout que el worker.
mkroot() {
  local r="$TMP/$1"
  mkdir -p "$r/hotwheels/tools" "$r/hotwheels/out/locks" "$r/hotwheels/build-vita3k" "$r/shared/Vita3K/home"
  cp "$GUARD" "$r/hotwheels/tools/remote_disk_guard.sh"
  printf 'firmware\n' > "$r/shared/Vita3K/home/firmware.bin"
  printf 'fuente\n' > "$r/hotwheels/tools/fuente.c"
  echo "$r"
}
# El job imprime REMOTE_RUN_ARCHIVE= y REMOTE_RUN_RC=; se leen del stdout.
field() { sed -n "s/^$1=//p"; }

# --- T1: corrida PASS: el delta viaja, la corrida se borra en el worker ---
r=$(mkroot pass)
cmd='mkdir -p hotwheels/out/run-x && printf ok > hotwheels/out/run-x/marker'
out=$(bash "$JOB" "$r" qemu 2 t1 "$(b64 "$cmd")" 2>"$TMP/err1")
rc=$?
[[ $rc == 0 ]] || fail "T1: rc esperado 0, obtenido $rc ($(cat "$TMP/err1"))"
arch=$(field REMOTE_RUN_ARCHIVE <<<"$out")
[[ -n "$arch" && -f "$arch" ]] || fail "T1: no hay archivo de retorno"
tar -tf "$arch" | grep -q 'run-x/marker' || fail "T1: el tar no lleva el delta: $(tar -tf "$arch")"
[[ ! -d "$r/hotwheels/out/run-x" ]] || fail "T1: la corrida PASS quedó en el worker"
[[ ! -e "$r/hotwheels/out/.runner" ]] || fail "T1: quedó el estado por corrida"
grep -q 'wsl_usado_gb=' <<<"$out" || fail "T1: no reporta el uso de disco"

# --- T2: corrida FAIL: el delta viaja igual y la corrida se retiene para debug ---
r=$(mkroot fail)
cmd='mkdir -p hotwheels/out/run-y; printf mal > hotwheels/out/run-y/marker; exit 7'
out=$(bash "$JOB" "$r" qemu 2 t2 "$(b64 "$cmd")" 2>"$TMP/err2")
rc=$?
[[ $rc == 7 ]] || fail "T2: rc esperado 7, obtenido $rc ($(cat "$TMP/err2"))"
arch=$(field REMOTE_RUN_ARCHIVE <<<"$out")
[[ -n "$arch" && -f "$arch" ]] || fail "T2: la corrida fallida también debe dejar el tar"
tar -tf "$arch" | grep -q 'run-y/marker' || fail "T2: el tar no lleva el delta fallido"
[[ -d "$r/hotwheels/out/run-y" ]] || fail "T2: la corrida FAIL debe retenerse en el worker"
grep -q 'run-y' <<<"$out" || fail "T2: no reporta la ruta de la corrida retenida"
[[ ! -e "$r/hotwheels/out/.runner" ]] || fail "T2: quedó el estado por corrida"

# --- T3: guarda de disco: aborta antes de correr ---
r=$(mkroot guard)
cmd='mkdir -p hotwheels/out/run-z'
out=$(HW_GUARD_ROOT_USED_KB=$((61 * 1024 * 1024)) \
      HW_GUARD_ROOT_AVAIL_KB=$((940 * 1024 * 1024)) \
      HW_GUARD_WIN_FREE_KB=$((401 * 1024 * 1024)) \
      bash "$JOB" "$r" qemu 2 t3 "$(b64 "$cmd")" 2>"$TMP/err3")
rc=$?
[[ $rc == 76 ]] || fail "T3: rc esperado 76 (guarda de disco), obtenido $rc"
[[ ! -d "$r/hotwheels/out/run-z" ]] || fail "T3: el comando corrió pese a la guarda"

# --- T4: sin slot -> 75 y el comando no corre ---
r=$(mkroot slot)
cmd='mkdir -p hotwheels/out/run-s'
exec 9>"$r/hotwheels/out/locks/qemu-0.lock"
flock -n 9 || fail "T4: no se pudo tomar el lock del slot"
out=$(bash "$JOB" "$r" qemu 1 t4 "$(b64 "$cmd")" 2>"$TMP/err4")
rc=$?
[[ $rc == 75 ]] || fail "T4: rc esperado 75 (sin slot), obtenido $rc"
[[ ! -d "$r/hotwheels/out/run-s" ]] || fail "T4: el comando corrió sin slot"

# --- T5: Vita3K: VITA3K_HOME apunta a un clon propio y el clon se borra ---
r=$(mkroot vita)
cmd='mkdir -p hotwheels/out/run-v; printf "%s" "$VITA3K_HOME" > hotwheels/out/run-v/home.txt'
out=$(bash "$JOB" "$r" vita3k 1 t5 "$(b64 "$cmd")" 2>"$TMP/err5")
rc=$?
[[ $rc == 0 ]] || fail "T5: rc esperado 0, obtenido $rc ($(cat "$TMP/err5"))"
arch=$(field REMOTE_RUN_ARCHIVE <<<"$out")
home=$(tar -xOf "$arch" 2>/dev/null | grep -o '/[^ ]*vita3k-home' | head -1)
[[ -n "$home" ]] || fail "T5: el comando no vio un VITA3K_HOME clonado"
[[ "$home" != "$r/shared/Vita3K" ]] || fail "T5: el clon es el home base, no una copia"
[[ ! -e "$home" ]] || fail "T5: el clon de VITA3K_HOME no se borró"

# --- T6: el job no crea copias del repo en el worker ---
r=$(mkroot copies)
cmd='mkdir -p hotwheels/out/run-c'
bash "$JOB" "$r" qemu 2 t6 "$(b64 "$cmd")" >/dev/null 2>&1
copies=$(find "$r" -maxdepth 3 -type d \( -name 'worker-run-*' -o -name 'repo' \) | head -5)
[[ -z "$copies" ]] || fail "T6: el job creó copias del repo: $copies"

# --- T7: directorio de build del host: compartido si está libre, propio si no ---
r=$(mkroot build)
cmd='mkdir -p hotwheels/out/run-b; printf "%s" "$HW_HOST_BUILD_DIR" > hotwheels/out/run-b/build.txt'
out=$(bash "$JOB" "$r" qemu 2 t7a "$(b64 "$cmd")" 2>"$TMP/err7")
rc=$?
[[ $rc == 0 ]] || fail "T7a: rc esperado 0, obtenido $rc ($(cat "$TMP/err7"))"
arch=$(field REMOTE_RUN_ARCHIVE <<<"$out")
got=$(tar -xOf "$arch" 2>/dev/null | grep -o '/[^ ]*build-host' | head -1)
[[ "$got" == "$r/hotwheels/build-host" ]] || fail "T7a: con el build libre debe usarse el compartido, se usó $got"

# con el build compartido tomado por otro job, el nuestro va aparte
r=$(mkroot build-busy)
mkdir -p "$r/hotwheels/build-host"
exec 7>"$r/hotwheels/build-host/.build.lock"
flock -n 7 || fail "T7b: no se pudo tomar el lock del build compartido"
out=$(bash "$JOB" "$r" qemu 2 t7b "$(b64 "$cmd")" 2>"$TMP/err7b")
rc=$?
[[ $rc == 0 ]] || fail "T7b: rc esperado 0, obtenido $rc ($(cat "$TMP/err7b"))"
arch=$(field REMOTE_RUN_ARCHIVE <<<"$out")
got=$(tar -xOf "$arch" 2>/dev/null | grep -o '/[^ ]*build-host' | head -1)
[[ "$got" == "$r/hotwheels/build-host" ]] && fail "T7b: con el build tomado debe usarse uno propio, se usó $got"
[[ -n "$got" ]] || fail "T7b: el comando no vio HW_HOST_BUILD_DIR"
[[ ! -e "$got" ]] || fail "T7b: el build propio quedó en el worker"

# --- T8: un directorio que otro job está escribiendo no se borra ---
# Con el margen por defecto, la salida reciente de un job concurrente queda:
# borrarla le rompería la corrida en curso.
r=$(mkroot vivo)
cmd='mkdir -p hotwheels/out/run-vivo; printf ok > hotwheels/out/run-vivo/marca'
HW_RUN_QUIET_SEC=30 bash "$JOB" "$r" qemu 2 t8 "$(b64 "$cmd")" >/dev/null 2>&1
[[ -d "$r/hotwheels/out/run-vivo" ]] || fail "T8: se borró la salida de un job concurrente"

echo 'run_remote_job: PASS'