# BUG-019 — `fps_mode=interp60` no llega al runtime

- Estado: **cerrado** (`f06ec04`/`6cc787f`, review APRUEBA). HW-025 sigue con luna.
- Severidad: mayor: la opción persistida/seleccionable no cambia el pacing del juego.
- Evidencia: estática, HEAD `ff4881a` más WIP compartido al 2026-10-04.

`runtime_vita.cpp` carga `HwVitaConfig io_cfg` desde `hwvita.ini`; antes del fix no consultaba `io_cfg.fps_mode`, no instanciaba `Interp60Presenter` ni consumía ticks de 60 Hz.

`host::Config` expone ahora `d3d_present_frame` y `integrate.cpp` lo pasa a `ComHostOps::present_frame`. En Vita, `fps_mode=interp60` inicia un consumer que espera vblank, ejecuta `Interp60Presenter::tick` sobre vitaGL y comparte mutex con el presenter P8. `original` conserva replay síncrono; `native60` degrada a `interp60`; la ausencia del backend o un fallo al crear el hilo conserva el replay síncrono.

Verificación: `test_fps_runtime` PASS; `tools/build_vita.sh` PASS. Vita3K con `hwvita.ini` en `interp60`, corrida `build-vita3k/hotwheels-vita-20261003-053825`: log `fps_mode guardado=interp60 efectivo=interp60`, consumer activo, 493 rings aceptados y 69 renders de consumer al llegar a escena 500; `shot_024.png` muestra carrera. La corrida prueba que la opción cambia la ruta y consume rings, pero no demuestra 60 presentaciones/s.

HW-025 sigue incompleta: el caller no clasifica ordinales de geometría de mundo, así que `world_draws` vacío repite draws y no interpola movimiento. Vita3K completó sólo 69 renders para 493 rings aceptados; el objetivo de 60 Hz bajo carga queda abierto en HW-025, junto con interpolación geométrica y review de Luna.

## Historial — 2026-10-04

La primera comprobación de `f9382cb` sólo cubrió la política aislada y correctamente dejó el bug abierto. El cableado de runtime descrito arriba se completó después y tiene evidencia host/build/Vita3K; queda pendiente review independiente y el objetivo completo de pacing/interpolación de HW-025.

BUGS.md indexa BUG-019 desde `0c49a24`.
