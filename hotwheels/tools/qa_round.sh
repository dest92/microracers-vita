#!/usr/bin/env bash
# Ronda de QA en 1 comando (mejora de qa): compara HEAD con la última verificada
# (out/.qa_last), corre smoke+run_all sólo si cambió src/tests/tools/CMake, e
# imprime un resumen compacto. Uso: tools/qa_round.sh [--force] [--clean]
# --clean: verifica HEAD en un git worktree aislado (copia d2vita/D2Vita y extracted/),
# sin el WIP sin commitear del árbol compartido (BUG-039). Imprime run_all y fin del host.
set -u
HW="$(cd "$(dirname "$0")/.." && pwd)"; REPO="$(git -C "$HW" rev-parse --show-toplevel)"
LAST_F="$HW/out/.qa_last"; HEAD_C="$(git -C "$REPO" rev-parse --short HEAD)"
if [ "${1:-}" = "--clean" ] || [ "${2:-}" = "--clean" ]; then
  W="${TMPDIR:-/tmp}/qa_wt_$$"; git -C "$REPO" worktree add -q --detach "$W" HEAD || exit 1
  rm -rf "$W/d2vita/D2Vita"; cp -a "$REPO/d2vita/D2Vita" "$W/d2vita/D2Vita"; cp -a "$HW/extracted" "$W/hotwheels/extracted"
  # Infra no versionada (vitasdk, winx86-dx, Vita3K): symlinks al árbol real. Sin esto el
  # worktree da rojos de entorno (build_vita sin toolchain, includes de winx86-dx) que no son de HEAD (BUG-048).
  mkdir -p "$W/shared"; for s in "$REPO"/shared/*; do ln -sfn "$s" "$W/shared/$(basename "$s")"; done
  ( cd "$W/hotwheels" && timeout 500 bash tests/run_all.sh 2>&1 | grep -E "^FAIL|run_all:"
    timeout 450 bash tools/run_host.sh >/dev/null 2>&1; D=$(ls -dt out/run-*|head -n1)
    grep -hE "no paró|fin:" "$D/run.log" 2>/dev/null | tail -n 1 | cut -c1-120; grep -m2 -E "undefined|error" "$D/stdout.txt" | cut -c1-150 )
  mkdir -p "$HW/out/qa_clean_logs"; for L in "$W"/hotwheels/out/run_all-*/; do for f in "$L"*.log; do case "$f" in *ctest.log|*run_*.log|*test_*.log) grep -qE "error|FAIL|Assert|\*\*\*" "$f" && { echo "--- $(basename "$f")"; grep -E "error|FAIL|Assert|\*\*\*" "$f" | head -n 3 | cut -c1-200; cp "$f" "$HW/out/qa_clean_logs/"; };; esac; done; done
  git -C "$REPO" worktree remove --force "$W"; exit 0
fi
LAST="$(cat "$LAST_F" 2>/dev/null || true)"
echo "HEAD=$HEAD_C last=${LAST:-ninguna} loadavg=$(cut -d' ' -f1-3 /proc/loadavg) nproc=$(nproc)"
if [ -n "$LAST" ] && [ "${1:-}" != "--force" ]; then
  CH="$(git -C "$REPO" diff --stat "$LAST"..HEAD -- hotwheels/src hotwheels/tests hotwheels/tools hotwheels/CMakeLists.txt | tail -n 1)"
  [ -z "$CH" ] && { echo "sin cambios de código desde $LAST: nada que reverificar"; echo "$HEAD_C" > "$LAST_F"; exit 0; }
  echo "cambios: $CH"
fi
timeout 400 bash "$HW/tools/smoke.sh" >/dev/null 2>&1
R="$(ls -t "$HW"/out/smoke-*.md | head -n 1)"
grep -E "Resultado global|MISSING únicos|PNG retenidos|veredicto|rc por etapa" "$R" | cut -c1-150
timeout 500 bash "$HW/tests/run_all.sh" 2>&1 | grep -E "^FAIL|run_all:"
echo "$HEAD_C" > "$LAST_F"
