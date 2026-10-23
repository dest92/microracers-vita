# BUG-043 — qemu-arm: pipeline D3D segfault tras shutdown cooperativo

- **Severidad:** mayor
- **Owner:** bugs (fix); reviewer (revisión)
- **Estado:** fix implementado; review de código pendiente.
- **Detectó:** render, 2026-10-03

## Síntoma y evidencia

Con pipeline D3D default, `tools/run_race.sh 35 d3d` alcanzó `shutdown requested exit=0`, `frames=1274`, `missing=0`, y después el proceso terminó con SIGSEGV/rc 139. Repetición de 25 s: `frames=879`, `missing=0`, mismo SIGSEGV/rc 139. En ambas, fault host-PC `0x30ef0`; guest EIP al reportar el fallo varió (`0042250b`, `00446080`).

Control con `HW_D3D_SYNC_BASELINE=1`, mismo modo `d3d`: `shutdown requested exit=0`, `frames=265`, `missing=0`, proceso rc 0. Evidencia:

- Pipeline 35 s: `out/run-20261003-163211371484535-2431689/stdout.txt`
- Pipeline 25 s: `out/run-20261003-163557050058978-2452947/stdout.txt`
- Sync 35 s: `out/run-20261003-163320959213128-2439803/stdout.txt`
- Pipeline 25 s con telemetría HW-063: `out/run-20261003-164949219728846-2483396/stdout.txt`; shutdown `exit=0`, `frames=890`, `missing=0`, luego SIGSEGV/139, host-PC `0x30fc8`.

Los runs pipeline saturaron la cola (879/915 drops en una ventana estable; guest 39.998 Hz, consumer 1.466 Hz, backend uploads 0.066/frame). `frame_00200.png` muestra la escena texturada durante la carga pipeline. La corrida con telemetría confirmó 360.868 ops resource en rings descartados y 20 estados finales reinyectados; el test golden host valida el pixel resultante. Este SIGSEGV posterior al shutdown no se atribuye al carry de HW-063.

Reproducción integ (2026-10-03): `run_race.sh 8 d3d` terminó rc 0 (frames=67, missing=0; no reprodujo). A 25 s, el pipeline llegó a `fin: shutdown requested exit=0x00000000 frames=852 missing=0` y luego SIGSEGV/139: host-PC `0x31010`, guest EIP snapshot `0x00422890`, fault `0x4ed350dc`; artefacto `out/run-20261003-170812098423090-2572554/stdout.txt`. El control `HW_D3D_SYNC_BASELINE=1`, también 25 s, esta vez no reprodujo SIGSEGV, pero el watchdog no obtuvo GIL y terminó por `_Exit(3)` (`out/run-20261003-170935355288324-2579360`); no es control limpio comparable. Esto afianza la falla pipeline, pero no atribuye causa.

Evidencia estática más concreta: `host_main.cpp` mantiene `RefRaster raster` en la pila y pasa su dirección a `Config::d3d_backend`; `install_all` guarda un `ComBridge` en `g_d3d`, cuyo worker usa `Backend&` y cuyos callbacks capturan `raster` por referencia. `shutdown_d3d()` existe pero el retorno normal de `host_main` no lo llama. El destructor global de `g_d3d` puede correr después de destruir los locales; hace falta apagarlo en vida del backend y unir el worker antes de salir. No se aplicó el fix porque `host_main.cpp` conserva WIP ajeno sin coordinar.

## Hipótesis y próximo paso

La causa probable era lifetime/join incorrecto durante teardown del worker. El consumidor conserva `Backend&`, mientras el backend host es local a `host_main.cpp`; sin reset explícito, `g_d3d` global se destruye después de los locales. Coordiné el cambio acotado con integ/render y conservé sus demás hunks WIP fuera del commit.

## Fix BUG-043 (2026-10-04)

`D3DShutdownGuard` se construye tras retornar `ns->run()` y antes del retorno de `main`. Al salir de scope llama `shutdown_d3d()`: `ComBridge::Impl` cierra/drena `FrameQueue` y hace `join()` del consumer antes de que se destruyan los locales anteriores (`RefRaster` y callbacks capturados). La queue protege `closed`, estados de slot y writer con mutex/CV; esos locks publican cambios al despertar al consumer. `join()` sincroniza la finalización del hilo y garantiza que no retenga referencias al backend después del retorno. No hay stop-flag lock-free que necesite atomic acquire/release; agregarlo duplicaría la sincronización existente.

Regresión: `run_d3d_shutdown_guard.sh` verifica que el cleanup bloquee la destrucción del backend hasta que termina el consumer (rojo antes del guard). `run_bug043_pipeline.sh` ejecuta el caso D3D QEMU de 25 s; resultado directo PASS: `fin=limpio`, `exit=0`, `missing=0`. Tres corridas de 25 s con el guard terminaron sin SIGSEGV (894, 891 y 877 frames guest). Commits `dcb5fec`, `b42d8b6`, `1ebee88`. Solicité revisión de código; BUG-043 no se cierra hasta el APRUEBA.
