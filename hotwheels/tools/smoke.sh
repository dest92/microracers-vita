#!/usr/bin/env bash
# smoke.sh — UN comando de humo para todo el equipo (HW-019).
#
#   hotwheels/tools/smoke.sh [VITA3K_SEGS] [QEMU_SEGS]
#
# Etapas:
#   1. tests host (tests/run_all.sh: ctest + run_*.sh + test_*.py + test_*.sh) [crítico]
#   2. build Vita (tools/build_vita.sh -> VPK)        [crítico]
#   3. run host qemu-arm (tools/run_host.sh)          [crítico si existe; si no, skip]
#   4. Vita3K con tools/vita3k_run.sh                 [crítico si la imagen existe; si no, skip]
#
# Resumen: hotwheels/out/smoke-<fecha>.md (tests, última trampa / MISSING, frames,
# duración por etapa, md5 del VPK). Devuelve != 0 si alguna etapa crítica falla.
# Niveles de validación: qemu-arm (tier 1) -> Vita3K; NO usa consola real.
set -uo pipefail
ulimit -c 0

HW="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HW/.." && pwd)"
VITA3K_SECS="${1:-20}"
QEMU_SECS="${2:-20}"
STAMP="$(date +%Y%m%d-%H%M%S%N)-$$"
OUT="$HW/out"
mkdir -p "$OUT"
REPORT="$OUT/smoke-$STAMP.md"
LOGDIR="$OUT/smoke-$STAMP.d"
mkdir -p "$LOGDIR"
# Lock de corrida activa: prune_out.sh no borra smoke-*.d mientras exista
# (<30 min). Sin esto, prune (cada 30 min) mataba el LOGDIR en curso y las
# etapas fallaban con ENOENT en la redirección (tanda 36).
echo $$ > "$OUT/.smoke-active"
trap 'rm -f "$OUT/.smoke-active"' EXIT

FAIL=0
declare -A RC DUR NOTES
run_stage() { # nombre comando...
    local name="$1"; shift
    local t0=$SECONDS
    if "$@" >"$LOGDIR/$name.log" 2>&1; then RC[$name]=0; else RC[$name]=$?; fi
    DUR[$name]=$((SECONDS - t0))
}
log() { echo "[smoke] $*"; }

# ---------------------------------------------------------------- 1) tests host
log "1/4 tests host (tests/run_all.sh)"
t0=$SECONDS
bash "$HW/tests/run_all.sh" >"$LOGDIR/tests.log" 2>&1
TESTS_RC=$?
DUR[tests]=$((SECONDS - t0))
[ $TESTS_RC -eq 0 ] || FAIL=1
CTEST_LINE="$(grep -aE '^run_all: PASS=' "$LOGDIR/tests.log" | tail -1)"
[ -n "$CTEST_LINE" ] || CTEST_LINE="(sin resumen run_all; rc=$TESTS_RC)"

# ---------------------------------------------------------------- 2) build Vita
log "2/4 build Vita"
VPK="$HW/build-vita/hotwheels-vita.vpk"
if run_stage build_vita bash "$HW/tools/build_vita.sh"; then :; fi
[ "${RC[build_vita]:-1}" -eq 0 ] && [ -f "$VPK" ] || FAIL=1
VPK_MD5="$(md5sum "$VPK" 2>/dev/null | awk '{print $1}')"
VPK_SIZE="$(stat -c %s "$VPK" 2>/dev/null || echo 0)"

# ---------------------------------------------------------------- 3) qemu host
QEMU_LAST_TAG="(n/a)"; QEMU_MISSING="(ninguno)"; QEMU_FRAMES=0; QEMU_FRAME_PATH="(n/a)"; LATEST=""; V3KDIR=""
QEMU_NOTE=""
if [ -x "$HW/tools/run_host.sh" ]; then
    log "3/4 run host qemu-arm (${QEMU_SECS}s)"
    run_stage qemu env HW_KEEP_OUT=1 bash "$HW/tools/run_host.sh" "$QEMU_SECS"
    QEMU_SRC_RC="${RC[qemu]}"
    # run_host.sh imprime "== <dir>" al terminar: usar ESE dir evita la carrera
    # sobre el symlink compartido out/latest cuando varios agentes corren a la vez.
    LATEST="$(grep -aE '^== /' "$LOGDIR/qemu.log" 2>/dev/null | tail -1 | sed 's/^== //')"
    [ -n "$LATEST" ] && [ -d "$LATEST" ] || LATEST="$HW/out/latest"
    # Veredicto por artefactos (BUG-003): exige base + frames>0 + sin cuelgue.
    mapfile -t QV < <(bash "$HW/tools/smoke_qemu_verdict.sh" "$LATEST" 2>/dev/null)
    QEMU_VERDICT="${QV[0]:-FAIL}"
    QEMU_REASON="${QV[1]:-sin veredicto}"
    QEMU_LAST_TAG="${QV[2]:-(sin tags de imports)}"
    if [ "$QEMU_VERDICT" = OK ]; then RC[qemu]=0; else RC[qemu]=1; FAIL=1; fi
    QEMU_NOTE="veredicto=$QEMU_VERDICT ($QEMU_REASON); run_host.sh rc=$QEMU_SRC_RC (no usado)"
    QEMU_MISSING="$(grep -ah 'MISSING' "$LATEST/run.log" 2>/dev/null | sort -u | tr '\n' '; ' )"
    [ -n "$QEMU_MISSING" ] || QEMU_MISSING="(ninguno)"
    QEMU_FRAMES="$(ls "$LATEST"/frame_*.png 2>/dev/null | wc -l)"
    QEMU_FRAME_PATH="$LATEST/frame_*.png"
else
    log "3/4 run_host.sh ausente: skip"
    RC[qemu]=99; DUR[qemu]=0
fi

# ---------------------------------------------------------------- 4) Vita3K
V3K_BOOT="(skip)"; V3K_TRAP="(n/a)"; V3K_SHOTS=0; V3K_PATH="(n/a)"
if docker image inspect vita3k-runtime:26.04 >/dev/null 2>&1 && [ -f "$VPK" ]; then
    log "4/4 Vita3K (${VITA3K_SECS}s)"
    STAGE="$LOGDIR/stage/hotwheels"
    mkdir -p "$STAGE"
    cp -f "$HW/extracted/original/Program_Files/HotWheels.exe" "$STAGE/" 2>/dev/null || true
    run_stage vita3k env STAGE="$LOGDIR/stage" HW_KEEP_OUT=1 bash "$HW/tools/vita3k_run.sh" "$VPK" "$VITA3K_SECS"
    [ "${RC[vita3k]:-1}" -eq 0 ] || FAIL=1
    V3KDIR="$(grep -a '^== salida:' "$LOGDIR/vita3k.log" 2>/dev/null | tail -1 | sed 's/^== salida: //')"
    if [ -n "$V3KDIR" ]; then
    V3K_PATH="$V3KDIR"
    V3K_BOOT="$(grep -ac 'boot_game_once' "$V3KDIR/run.stdout" 2>/dev/null || true)"
        V3K_SHOTS="$(ls "$V3KDIR"/shot_*.png 2>/dev/null | wc -l)"
    fi
    BOOTLOG="$ROOT/shared/Vita3K/home/.local/share/Vita3K/Vita3K/ux0/data/hotwheels/boot.log"
    V3K_TRAP="$(grep -a 'trap #' "$BOOTLOG" 2>/dev/null | tail -1)"
    [ -n "$V3K_TRAP" ] || V3K_TRAP="(ninguna)"
    [ "${V3K_BOOT:-0}" != "0" ] || { [ -z "$V3KDIR" ] || FAIL=1; }
else
    log "4/4 Vita3K no disponible (imagen o VPK): skip"
    RC[vita3k]=99; DUR[vita3k]=0
fi

# ---------------------------------------------------------------- resumen
status_of() { local n="$1"; case "${RC[$n]:-99}" in 0) echo "OK";; 99) echo "skip";; *) echo "FALLA(${RC[$n]})";; esac; }
OVERALL=PASS; [ $FAIL -eq 0 ] || OVERALL=FAIL
if [ "$OVERALL" = PASS ]; then
    QEMU_FRAME_PATH="(corrida eliminada tras PASS)"
    V3K_PATH="(corrida eliminada tras PASS)"
fi

cat > "$REPORT" <<EOF
# Smoke hotwheels — $STAMP

- **Resultado global: $OVERALL**
- VPK: \`hotwheels/build-vita/hotwheels-vita.vpk\` ($VPK_SIZE bytes)
- md5 VPK: \`$VPK_MD5\`
- Niveles: tests host → build Vita → qemu-arm (tier 1) → Vita3K. Sin consola real.
- Logs crudos: \`hotwheels/out/smoke-$STAMP.d/\` (se eliminan en PASS, quedan para debug en FAIL)

## Etapas

| # | etapa | resultado | duración |
|---|---|---|---|
| 1 | tests host (run_all) | $( [ $TESTS_RC -eq 0 ] && echo OK || echo "FALLA($TESTS_RC)" ) | ${DUR[tests]}s |
| 2 | build Vita (VPK) | $(status_of build_vita) | ${DUR[build_vita]}s |
| 3 | run host qemu-arm | $(status_of qemu) | ${DUR[qemu]}s |
| 4 | Vita3K | $(status_of vita3k) | ${DUR[vita3k]}s |

## 1) Tests host

\`\`\`
$CTEST_LINE
\`\`\`

## 2) Build Vita

- VPK md5: \`$VPK_MD5\`

## 3) qemu-arm host (tier 1)

- Última trampa/import: \`$QEMU_LAST_TAG\`
- MISSING únicos: $QEMU_MISSING
- PNG retenidos: **$QEMU_FRAMES** (\`$QEMU_FRAME_PATH\`)
- Nota: $QEMU_NOTE

## 4) Vita3K

- \`boot_game_once\` encontrados: **$V3K_BOOT** (0 = no booteó)
- Última trampa (del runtime): \`$V3K_TRAP\`
- Screenshots: **$V3K_SHOTS** (\`$V3K_PATH/shot_*.png\`)

## Errores / avisos

- rc por etapa: tests=$TESTS_RC build_vita=${RC[build_vita]:-?} qemu=${RC[qemu]:-?} vita3k=${RC[vita3k]:-?}
- etapas con 99 = salteadas por dependencia ausente.
$(if [ "$FAIL" -gt 0 ]; then
    wip_out="$(bash "$HW/tools/wip_src.sh" "$ROOT" 2>/dev/null || echo "wip=0")"
    wip_n="$(printf '%s\n' "$wip_out" | head -1)"
    wip_n="${wip_n#wip=}"
    case "$wip_n" in "" | *[!0-9]*) wip_n=0 ;; esac
    if [ "$wip_n" -gt 0 ]; then
        printf -- "- AVISO WIP: %s cambio(s) sin commitear en hotwheels/src — las fallas pudieran deberse al WIP local, no a HEAD:\n" "$wip_n"
        printf '%s\n' "$wip_out" | sed 1d | sed 's/^/  - /'
    fi
fi
)
EOF

echo "== smoke $OVERALL -> $REPORT"
grep -E 'Resultado global|md5 VPK' "$REPORT" || true
if [ $FAIL -eq 0 ]; then
    if [ -n "$LATEST" ] && [ -d "$LATEST" ]; then rm -rf -- "$LATEST"; fi
    [ -n "$LATEST" ] && [ "$(readlink "$HW/out/latest" 2>/dev/null || true)" = "$(basename "$LATEST")" ] && rm -f "$HW/out/latest"
    [ -n "$V3KDIR" ] && [ -d "$V3KDIR" ] && rm -rf -- "$V3KDIR"
    rm -rf -- "$LOGDIR"
    bash "$HW/tools/prune_out.sh" >/dev/null 2>&1 || true
    exit 0
fi
bash "$HW/tools/prune_out.sh" >/dev/null 2>&1 || true
exit 1
