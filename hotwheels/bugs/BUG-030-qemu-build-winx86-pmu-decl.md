# BUG-030 — build host (qemu) cae: `dyn86_pmu_dispatch_read` no declarado en winx86 sched_native.cpp

Nivel: **medido en qemu/docker** (qa, 2026-10-03 ~09:04, HEAD f65bf14 + working tree de winx86). Dueño: **ports** (winx86/dynarec; PMU). Severidad: bloqueante para smoke etapa qemu. Estado: **cerrado** (`3035e1e`, winx86; build host ARM/qemu PASS).

## Evidencia
- `out/run-20261003-090413640489019-1367300/stdout.txt`: `sched_native.cpp:1008:48: error: 'dyn86_pmu_dispatch_read' was not declared in this scope` (y `dyn86_pmu_dispatch_samples_read` :1009); `FATAL: 1 unite(s) en echec`; smoke qemu rc=1, 0 frames. run_all 23/0/1 (tests host no compilan winx86 en esa ruta).
- Sólo `sched_native.cpp` usa los símbolos; el árbol de `third_party/winx86` tiene WIP sin commitear (`dyn86.c`, `shim/debug.h`, `bridge.h`, `platform/vita_host.cpp`): la declaración/definición no llegó al header que incluye sched_native.cpp.
- Último smoke verde: 4f94565/c3bd4f8 (qemu OK, frames=203).

## Acción
El header compartido PMU ahora publica los tres símbolos para Vita y ofrece no-ops inline para host, donde no existe PMU ARM. Así `sched_native.cpp` compila y linkea sin condicionar el scheduler entero a Vita. Validación: Docker host ARM ejecutó `build_host.sh` y produjo `hotwheels/build-host/hotwheels-host`. El juego no se ejecutó bajo qemu en esta verificación.
