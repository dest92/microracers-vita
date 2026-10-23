#!/usr/bin/env bash
# run_all.sh — corre TODOS los tests host del port y resume pass/fail por suite.
#
#   hotwheels/tests/run_all.sh [--list]
#
# Suites:
#   1) ctest: cmake configure + build + ctest (targets de CMakeLists.txt)
#   2) tests/run_*.sh (unidades standalone)
#   3) tests/test_*.py (python3, con el repo raíz en PYTHONPATH)
#   4) tests/test_*.sh (contratos de shell: smoke/veredicto, herramientas, worker)
#
# Sale != 0 si alguna suite falla. Logs por suite en hotwheels/out/run_all-<fecha>/.
# Lo usa tools/smoke.sh como etapa "tests host".
set -uo pipefail

HW="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HW/.." && pwd)"
cd "$HW"

# Etapa 4: directorio y timeout por suite (override para tests y rondas acotadas).
TESTS_DIR="${RUN_ALL_TESTS_DIR:-tests}"
TEST_SH_TIMEOUT="${RUN_ALL_TEST_SH_TIMEOUT:-120}"
# Suites de la etapa 4 que necesitan insumos NO versionados (exe del juego,
# toolchains, consola, red): se reportan SKIP y no cuentan como fallo.
ENV_SKIP_FILE="$HW/tests/test_sh_env_skip.txt"

ONLY_TEST_SH=""
case "${1:-}" in
    --list)
        echo "ctest"
        for s in tests/run_*.sh; do
            [ "$(basename "$s")" = "run_all.sh" ] && continue
            echo "$(basename "$s")"
        done
        for p in tests/test_*.py; do echo "$(basename "$p")"; done
        for s in "$TESTS_DIR"/test_*.sh; do
            [ -e "$s" ] || continue
            [ "$(basename "$s")" = "run_all.sh" ] && continue
            echo "$(basename "$s")"
        done
        exit 0
        ;;
    --only-test-sh) ONLY_TEST_SH=1 ;;
    "") ;;
    *) echo "run_all: opción desconocida '$1' (usá --list o --only-test-sh)" >&2; exit 2 ;;
esac

STAMP="$(date +%Y%m%d-%H%M%S%N)-$$"
OUT="$HW/out/run_all-$STAMP"
mkdir -p "$OUT"
ln -sfn "run_all-$STAMP" "$HW/out/run_all-latest"

PASS=0; FAIL=0; XFAILED=0; SKIPPED=0
declare -a RESULTS

# Suites atadas a bugs abiertos (rojas a propósito): se reportan XFAIL y no
# cuentan como fallo. Al cerrar el bug, quitarlas de esta lista.
XFAIL="${RUN_ALL_XFAIL:-}"
is_xfail() { for x in $XFAIL; do [ "$x" = "$1" ] && return 0; done; return 1; }
# Suites que necesitan qemu-arm/docker (lanzan tools/run_host.sh): por defecto
# se saltean (las cubre smoke.sh etapa 3 con veredicto). Forzar con RUN_ALL_INTEGRATION=1.
is_integration() { grep -q 'run_host\.sh' "$1" 2>/dev/null; }
# Etapa 4: la suite figura en tests/test_sh_env_skip.txt (insumos no versionados).
is_env_skip() {
    [ -f "$ENV_SKIP_FILE" ] || return 1
    sed 's/#.*//; s/[[:space:]]*$//' "$ENV_SKIP_FILE" | grep -qxF "$1"
}

record() { # nombre rc log
    local name="$1" rc="$2" logf="$3" mode
    # Convenio automake: 77 = la suite se auto-saltea por falta de prereqs
    # (`skip(){ echo "SKIP: falta $t"; exit 77; }` en test_vpk/test_console_package).
    if [ "$rc" -eq 77 ]; then
        RESULTS+=("SKIP $name (rc=77: la suite se auto-saltea)")
        SKIPPED=$((SKIPPED + 1))
        echo "[run_all] SKIP $name (rc=77: la suite se auto-saltea)"
        return
    fi
    if [ "$rc" -eq 0 ]; then
        mode="$(sed -n 's/^RUN_MODE: //p' "$logf" 2>/dev/null | tail -1)"
        if [ -n "$mode" ]; then
            RESULTS+=("PASS $name ($mode)")
            echo "[run_all] PASS $name ($mode)"
        else
            RESULTS+=("PASS $name")
            echo "[run_all] PASS $name"
        fi
        PASS=$((PASS + 1))
    else
        RESULTS+=("FAIL $name (rc=$rc) -> $logf"); FAIL=$((FAIL + 1))
        echo "[run_all] FAIL $name (rc=$rc) -> $logf"
    fi
}

# ---------------------------------------------------------------- 1) ctest
if [ -z "$ONLY_TEST_SH" ]; then
echo "[run_all] ctest (configure + build + test)"
# Subshell + `&&`: si configure o build fallan, no se llega a ctest (evita el
# falso verde de correr binarios de un build previo con rc de ctest = 0).
(
    cmake -S "$HW" -B "$HW/build" &&
    cmake --build "$HW/build" -j"$(nproc)" &&
    ctest --test-dir "$HW/build" --output-on-failure
) >"$OUT/ctest.log" 2>&1
record ctest $? "$OUT/ctest.log"

# ---------------------------------------------------------------- 2) run_*.sh
for s in tests/run_*.sh; do
    [ -e "$s" ] || continue
    name="$(basename "$s")"
    [ "$name" = "run_all.sh" ] && continue           # no recursión
    if is_xfail "$name"; then
        RESULTS+=("XFAIL $name (bug abierto; RUN_ALL_XFAIL)"); XFAILED=$((XFAILED + 1))
        echo "[run_all] XFAIL $name (bug abierto)"
        continue
    fi
    if [ -z "${RUN_ALL_INTEGRATION:-}" ] && is_integration "$s"; then
        RESULTS+=("SKIP $name (integración qemu; RUN_ALL_INTEGRATION=1 / smoke etapa 3)")
        SKIPPED=$((SKIPPED + 1))
        echo "[run_all] SKIP $name (integración qemu)"
        continue
    fi
    timeout 300 bash "$s" >"$OUT/$name.log" 2>&1
    record "$name" $? "$OUT/$name.log"
done

# ---------------------------------------------------------------- 3) test_*.py
for p in tests/test_*.py; do
    [ -e "$p" ] || continue
    name="$(basename "$p")"
    ( cd "$ROOT" && PYTHONPATH="$ROOT${PYTHONPATH:+:$PYTHONPATH}" timeout 300 python3 "$HW/$p" ) \
        >"$OUT/$name.log" 2>&1
    record "$name" $? "$OUT/$name.log"
done
fi

# ---------------------------------------------------------------- 4) test_*.sh
# Contratos de shell. timeout por suite: una suite colgada no cuelga el gate.
for s in "$TESTS_DIR"/test_*.sh; do
    [ -e "$s" ] || continue
    name="$(basename "$s")"
    [ "$name" = "run_all.sh" ] && continue           # no recursión
    if is_xfail "$name"; then
        RESULTS+=("XFAIL $name (bug abierto; RUN_ALL_XFAIL)"); XFAILED=$((XFAILED + 1))
        echo "[run_all] XFAIL $name (bug abierto)"
        continue
    fi
    if is_env_skip "$name"; then
        RESULTS+=("SKIP $name (entorno: insumos no versionados; tests/test_sh_env_skip.txt)")
        SKIPPED=$((SKIPPED + 1))
        echo "[run_all] SKIP $name (entorno: insumos no versionados)"
        continue
    fi
    if [ -z "${RUN_ALL_INTEGRATION:-}" ] && is_integration "$s"; then
        RESULTS+=("SKIP $name (integración qemu; RUN_ALL_INTEGRATION=1 / smoke etapa 3)")
        SKIPPED=$((SKIPPED + 1))
        echo "[run_all] SKIP $name (integración qemu)"
        continue
    fi
    timeout "$TEST_SH_TIMEOUT" bash "$s" >"$OUT/$name.log" 2>&1
    record "$name" $? "$OUT/$name.log"
done

# ---------------------------------------------------------------- resumen
echo
echo "===== run_all resumen ($OUT) ====="
printf '%s\n' "${RESULTS[@]}"
echo "run_all: PASS=$PASS FAIL=$FAIL XFAIL=$XFAILED SKIP=$SKIPPED"
if [ "$FAIL" -gt 0 ]; then
    wip_out="$(bash "$HW/tools/wip_src.sh" "$ROOT" 2>/dev/null || echo "wip=0")"
    wip_n="$(printf '%s\n' "$wip_out" | head -1)"
    wip_n="${wip_n#wip=}"
    case "$wip_n" in "" | *[!0-9]*) wip_n=0 ;; esac
    if [ "$wip_n" -gt 0 ]; then
        echo "AVISO WIP: $wip_n cambio(s) sin commitear en hotwheels/src — las fallas pudieran deberse al WIP local, no a HEAD:"
        printf '%s\n' "$wip_out" | sed 1d
    fi
fi
if [ "$FAIL" -eq 0 ]; then
    [ "$(readlink "$HW/out/run_all-latest" 2>/dev/null || true)" = "$(basename "$OUT")" ] && rm -f "$HW/out/run_all-latest"
    rm -rf -- "$OUT"
fi
[ "$FAIL" -eq 0 ]
