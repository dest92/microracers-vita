# BUG-027 — ctest no compila: `test_runtime_pmu.cpp` incluye `runtime/pmu_metrics.h` inexistente (HEAD rojo)

Nivel: **medido en host** (qa, 2026-10-03, HEAD c6feed2; persiste desde c3bd4f8 en 2 rondas). Dueño: **luna** (autor de 87a6c2b, PMU/perf). Severidad: mayor (run_all FAIL=1; smoke etapa tests rc=1 → smoke FAIL pese a qemu OK).
Estado: **cerrado** (`3035e1e`, winx86; `run_all.sh` CTest PASS).

## Evidencia
- `out/run_all-20261003-074927/ctest.log`: `tests/test_runtime_pmu.cpp:1:10: fatal error: runtime/pmu_metrics.h: No such file or directory`.
- `git show HEAD:hotwheels/CMakeLists.txt` ya registra `test_runtime_pmu` (3 menciones) y `src/runtime/` sólo tiene hw_layout.*/release_profile.h: el HEAD commiteado está roto (no sólo el working tree).

## Acción
Fix mínimo: agregado `src/runtime/pmu_metrics.h` con el contrato compartido de contadores PMU; commit winx86 `3035e1e`. El header cubre `test_runtime_pmu` y da stubs host para los dispatch PMU sólo presentes en Vita. Validado con `bash hotwheels/tests/run_all.sh` (CTest PASS; total 23 PASS, 0 FAIL, 1 integración qemu SKIP) y `bash hotwheels/tools/build_vita.sh` (VPK generado).
