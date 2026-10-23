#!/usr/bin/env bash
# tests/test_remote_disk_guard.sh — contrato de la guarda de disco del worker (SPEC-051 CA8).
#
# La guarda se ejecuta en el worker, pero su decisión es pura aritmética sobre
# medidas de disco: se prueban con valores inyectados (sin red, sin worker).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GUARD="$ROOT/hotwheels/tools/remote_disk_guard.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "test_remote_disk_guard: FAIL: $1" >&2; exit 1; }

# --- T1: dentro de los límites pasa y reporta las tres medidas ---
out=$(HW_GUARD_ROOT_USED_KB=$((16 * 1024 * 1024)) \
      HW_GUARD_ROOT_AVAIL_KB=$((940 * 1024 * 1024)) \
      HW_GUARD_WIN_FREE_KB=$((401 * 1024 * 1024)) \
      bash "$GUARD" 2>"$TMP/err")
rc=$?
[[ $rc == 0 ]] || fail "T1: rc esperado 0, obtenido $rc ($(cat "$TMP/err"))"
grep -q 'wsl_usado_gb=16' <<<"$out" || fail "T1: no reporta wsl_usado_gb=16: $out"
grep -q 'drive_libre_gb=401' <<<"$out" || fail "T1: no reporta drive_libre_gb=401: $out"

# --- T2: WSL por encima del tope -> guarda violada ---
out=$(HW_GUARD_ROOT_USED_KB=$((61 * 1024 * 1024)) \
      HW_GUARD_ROOT_AVAIL_KB=$((940 * 1024 * 1024)) \
      HW_GUARD_WIN_FREE_KB=$((401 * 1024 * 1024)) \
      bash "$GUARD" 2>"$TMP/err")
rc=$?
[[ $rc == 1 ]] || fail "T2: rc esperado 1, obtenido $rc"
grep -q '60' "$TMP/err" || fail "T2: el error no menciona el tope de 60 GB: $(cat "$TMP/err")"

# --- T3: drive Windows por debajo del mínimo -> guarda violada ---
out=$(HW_GUARD_ROOT_USED_KB=$((16 * 1024 * 1024)) \
      HW_GUARD_ROOT_AVAIL_KB=$((940 * 1024 * 1024)) \
      HW_GUARD_WIN_FREE_KB=$((49 * 1024 * 1024)) \
      bash "$GUARD" 2>"$TMP/err")
rc=$?
[[ $rc == 1 ]] || fail "T3: rc esperado 1, obtenido $rc"
grep -q '50' "$TMP/err" || fail "T3: el error no menciona el mínimo de 50 GB: $(cat "$TMP/err")"

# --- T4: exactamente en el tope no viola (el tope es "más de", no "desde") ---
out=$(HW_GUARD_ROOT_USED_KB=$((60 * 1024 * 1024)) \
      HW_GUARD_ROOT_AVAIL_KB=$((940 * 1024 * 1024)) \
      HW_GUARD_WIN_FREE_KB=$((50 * 1024 * 1024)) \
      bash "$GUARD" 2>"$TMP/err")
rc=$?
[[ $rc == 0 ]] || fail "T4: en el tope exacto debe pasar, obtenido $rc ($(cat "$TMP/err"))"

# --- T5: sin drive mounted y sin valor inyectado no se puede medir -> no se sigue ---
out=$(HW_DISK_WIN_DRIVE=/mnt/no-existe \
      HW_GUARD_ROOT_USED_KB=$((16 * 1024 * 1024)) \
      HW_GUARD_ROOT_AVAIL_KB=$((940 * 1024 * 1024)) \
      bash "$GUARD" 2>"$TMP/err")
rc=$?
[[ $rc == 2 ]] || fail "T5: rc esperado 2 (no medible), obtenido $rc"

# --- T6: sin inyección las medidas salen de df (parsing del formato real) ---
mkdir -p "$TMP/bin"
cat > "$TMP/bin/df" <<'MOCK'
#!/usr/bin/env bash
case "${*: -1}" in
  /) printf 'Filesystem 1K-blocks Used Available Use%% Mounted on\n/dev/sdd 1030800000 16777216 1014022784 2%% /\n' ;;
  *) printf 'Filesystem 1K-blocks Used Available Use%% Mounted on\nD:\\ 1677721600 1258291200 419430400 75%% %s\n' "${*: -1}" ;;
esac
MOCK
chmod +x "$TMP/bin/df"
out=$(PATH="$TMP/bin:$PATH" HW_DISK_WIN_DRIVE=/mnt/d bash "$GUARD" 2>"$TMP/err")
rc=$?
[[ $rc == 0 ]] || fail "T6: rc esperado 0 con df real, obtenido $rc ($(cat "$TMP/err"))"
grep -q 'wsl_usado_gb=16' <<<"$out" || fail "T6: no parsea la fila de /: $out"
grep -q 'drive_libre_gb=400' <<<"$out" || fail "T6: no parsea la fila del drive: $out"

echo 'run_remote_disk_guard: PASS'
