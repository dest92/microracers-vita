#!/usr/bin/env bash
# tests/test_remote_run.sh — contrato del wrapper local del runner remoto (SPEC-051).
#
# Mocks de rsync/ssh: no se toca el worker. Se verifica el cableado (sincronización,
# exclusiones, propagación del RC, recuperación del delta) y el contrato de disco
# del incidente 2026-10-21: un árbol único, sin copias del repo por corrida.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPT="$ROOT/hotwheels/tools/remote_run.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/mock-delta/hotwheels/out/run-delta"
printf 'delta\n' > "$TMP/mock-delta/hotwheels/out/run-delta/marker"
cat > "$TMP/bin/rsync" <<'MOCK'
#!/usr/bin/env bash
printf 'rsync %s\n' "$*" >> "$REMOTE_RUN_TRACE"
if [[ ${MOCK_RSYNC_RC:-0} != 0 ]]; then exit "$MOCK_RSYNC_RC"; fi
for arg in "$@"; do case "$arg" in */run.tar) [[ ${MOCK_ARCHIVE_RC:-0} == 0 ]] || exit "$MOCK_ARCHIVE_RC"; tar -cf "$arg" -C "$TMP/mock-delta" . ;; esac; done
MOCK
cat > "$TMP/bin/ssh" <<'MOCK'
#!/usr/bin/env bash
n=$(cat "$TMP/ssh-count" 2>/dev/null || printf 0)
n=$((n + 1))
printf '%s' "$n" > "$TMP/ssh-count"
printf 'ssh %s\n' "$*" >> "$REMOTE_RUN_TRACE"
encoded=$(printf '%s' "${2:-}" | sed -n 's/.*printf %s \([A-Za-z0-9+\/=]*\) | base64 -d.*/\1/p')
if [[ -n "$encoded" ]]; then printf '%s' "$encoded" | base64 -d >>"$REMOTE_RUN_TRACE"; fi
if ((n == 1)); then
  id=$(sed -n "s/.*id='\([^']*\)'.*/\1/p" "$REMOTE_RUN_TRACE" | tail -1)
  printf 'disco: wsl_usado_gb=16 wsl_libre_gb=940 drive=D: drive_libre_gb=401\n'
  printf 'REMOTE_RUN_ARCHIVE=/tmp/hwremote-%s.tar\n' "$id"
  printf 'REMOTE_RUN_RC=%s\n' "${MOCK_SSH_RC:-0}"
  exit "${MOCK_SSH_RC:-0}"
fi
exit "${MOCK_CLEANUP_SSH_RC:-0}"
MOCK
chmod +x "$TMP/bin/rsync" "$TMP/bin/ssh"
export TMP
export REMOTE_RUN_TRACE="$TMP/trace"
export REMOTE_RUN_OUTPUT_ROOT="$TMP/results"
export PATH="$TMP/bin:$PATH"
export REMOTE_RUN_HOST=mock-worker
export REMOTE_RUN_ROOT="$TMP/remote-root"
export REMOTE_RUN_RSYNC_PATH='rsync'
export REMOTE_RUN_RSYNC_BIN=rsync
export REMOTE_RUN_SSH_PATH='ssh'
: >"$REMOTE_RUN_TRACE"

reset() { rm -rf "$TMP/results"; mkdir -p "$TMP/results"; rm -f "$TMP/ssh-count"; : >"$REMOTE_RUN_TRACE"; }
fail() { echo "$1" >&2; exit 1; }

# --- Ca3: el RC remoto no se enmascara y el cableado es el del contrato ---
set +e
MOCK_SSH_RC=17 "$SCRIPT" hotwheels/tools/fake_cmd.sh 10
rc=$?
set -e
[[ $rc == 17 ]] || fail "exit remoto esperado 17, obtenido $rc"
grep -q 'mock-worker:' "$REMOTE_RUN_TRACE"
# --- CA7: un árbol único: el rsync NO crea una copia del repo por corrida ---
grep -q -- "$TMP/remote-root/" "$REMOTE_RUN_TRACE" || fail "el rsync no sincroniza contra el árbol del worker"
if grep -q 'worker-run-' "$REMOTE_RUN_TRACE"; then fail "el runner sigue creando copias del repo por corrida"; fi
# --- CA2: exclusiones del orquestador ---
for ex in '--exclude=/hotwheels/out/' '--exclude=/hotwheels/build-vita3k/' '--exclude=/hotwheels/wineprefix/' \
          '--exclude=/shared/Vita3K/' '--exclude=/shared/Vita3K-v0.2.1-4115/' '--exclude=/shared/opt/' \
          '--exclude=/shared/vitasdk/' '--exclude=/shared/ghidra-proyectos/' '--exclude=/d2vita/' \
          '--exclude=/d2vita/d2-juego/' '--exclude=/core'; do
    grep -q -- "$ex" "$REMOTE_RUN_TRACE" || fail "falta la exclusión $ex"
done
grep -Fq -- '--exclude=*.core' "$REMOTE_RUN_TRACE" || fail "falta la exclusión *.core"
grep -q -- '--exclude=/.wt-\*/' "$REMOTE_RUN_TRACE" || fail "falta la exclusión .wt-*"
grep -q -- '--include=/d2vita/D2Vita/third_party/winx86/' "$REMOTE_RUN_TRACE" || fail "falta el include de winx86"
grep -q 'base64 -d | bash' "$REMOTE_RUN_TRACE"
grep -q 'remote_job.sh' "$REMOTE_RUN_TRACE" || fail "la corrida remota no usa remote_job.sh"
grep -q "slots='10'" "$REMOTE_RUN_TRACE"

# --- Camino feliz: el delta se extrae local ---
reset
set +e
out=$("$SCRIPT" hotwheels/tools/fake_cmd.sh 10)
rc=$?
set -e
[[ $rc == 0 ]] || fail "camino feliz: rc esperado 0, obtenido $rc"
delta=$(ls -d "$TMP/results"/remote-*/hotwheels/out/run-delta/marker 2>/dev/null | head -1 || true)
[[ -n "$delta" ]] || fail "camino feliz: no se recuperó el delta local"
[[ ! -e "$TMP/results"/remote-*/run.tar ]] || fail "camino feliz: quedó run.tar sin desempacar"
grep -q 'wsl_usado_gb=' <<<"$out" || fail "no se reportó el uso de disco"

# --- Un fallo de limpieza remota NO cambia el RC del comando ---
reset
set +e
MOCK_SSH_RC=0 MOCK_CLEANUP_SSH_RC=44 "$SCRIPT" hotwheels/tools/fake_cmd.sh 10 2>"$TMP/err"
rc=$?
set -e
[[ $rc == 0 ]] || fail "limpieza rc=44 no debe enmascarar el RC remoto 0, obtenido $rc"
grep -q 'aviso limpieza remota rc=44' "$TMP/err" || fail "no se avisó del fallo de limpieza"

# --- Comando que falla conserva el delta recuperado (retención para debug) ---
reset
set +e
MOCK_SSH_RC=1 "$SCRIPT" hotwheels/tools/fake_cmd.sh 10
rc=$?
set -e
[[ $rc == 1 ]] || fail "fallo remoto: rc esperado 1, obtenido $rc"
delta=$(ls -d "$TMP/results"/remote-*/hotwheels/out/run-delta/marker 2>/dev/null | head -1 || true)
[[ -n "$delta" ]] || fail "fallo remoto: no se retuvo el delta recuperado"

# --- Ca4: Vita3K recibe 3 slots y la clase de job ---
reset
set +e
MOCK_SSH_RC=17 REMOTE_RUN_KIND=vita3k "$SCRIPT" hotwheels/tools/vita3k_run.sh hotwheels/out/input.vpk 20
rc=$?
set -e
[[ $rc == 17 ]] || fail "exit remoto Vita3K esperado 17, obtenido $rc"
grep -q -- '--include=/hotwheels/out/input.vpk' "$REMOTE_RUN_TRACE"
grep -q "slots='3'" "$REMOTE_RUN_TRACE"
grep -q "kind='vita3k'" "$REMOTE_RUN_TRACE"

# --- Propagación del RC de rsync ---
reset
set +e
MOCK_RSYNC_RC=23 "$SCRIPT" hotwheels/tools/fake_cmd.sh 10
rc=$?
set -e
[[ $rc == 23 ]] || fail "exit rsync esperado 23, obtenido $rc"

# --- Sin slot y sin tar: no queda directorio local vacío ---
reset
dirs_before=$(find "$TMP/results" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | wc -l)
set +e
MOCK_SSH_RC=75 MOCK_ARCHIVE_RC=1 "$SCRIPT" hotwheels/tools/fake_cmd.sh 10
rc=$?
set -e
[[ $rc == 75 ]] || fail "exit sin slot esperado 75, obtenido $rc"
dirs_after=$(find "$TMP/results" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | wc -l)
[[ $dirs_after == "$dirs_before" ]] || fail "quedó directorio local vacío"
echo 'run_remote_run: PASS'
