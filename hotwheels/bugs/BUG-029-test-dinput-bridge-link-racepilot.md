# BUG-029 — test_dinput_bridge no linkea: `vtable for hw::integration::RacePilotInput` indefinido

Nivel: **medido en host** (qa, 2026-10-03 ~08:18, HEAD 0268de3). Dueño: **luna** (autor de 7fefe75 "cubrir piloto DINPUT con telemetría guest"). Severidad: mayor (run_all FAIL=1). Estado: **cerrado** (`022f2aa`).

## Evidencia
- `out/run_all-20261003-081802386434633-1281442/ctest.log`: `dinput_bridge.cpp:(...RacePilotInputD2Ev...) undefined reference to 'vtable for hw::integration::RacePilotInput'`, ld rc=1.
- `RacePilotInput` se define en `src/integration/scripted_input.cpp`; el target CMake `test_dinput_bridge` (CMakeLists.txt:52-54) sólo compila `dinput.cpp` + `dinput_bridge.cpp` → falta `src/integration/scripted_input.cpp` en sus fuentes (o enlazarlo vía hw_boot).
- Se suma a BUG-027 (pmu_metrics.h ausente): ctest está rojo por 2 causas independientes; con make en paralelo sólo se ve la primera.

## Acción
Añadí `src/integration/scripted_input.cpp` al target `test_dinput_bridge` en `hotwheels/CMakeLists.txt` (`022f2aa`). Build aislado en `/tmp/hw029-luna`: `cmake -S hotwheels -B /tmp/hw029-luna`, `cmake --build /tmp/hw029-luna --target test_dinput_bridge -j2` y `ctest --test-dir /tmp/hw029-luna --output-on-failure -R '^dinput_bridge$'` PASS (1/1). Resuelve la vtable sin incorporar el script/piloto al runtime Vita ni a `hw_boot`.
