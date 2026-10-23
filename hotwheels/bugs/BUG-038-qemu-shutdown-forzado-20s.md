# BUG-038 — smoke qemu: al límite de 20 s el scheduler no para en 3 s ("salida forzada") → veredicto FAIL

Nivel: **medido en qemu** (qa, 2026-10-03 ~10:16–10:25, HEAD 1f964d1, loadavg ~2, **3/3 corridas**). Dueño: **integ** (candidatos: audio→luna, GL→render). Severidad: bloqueante para el gate del smoke. Estado: **abierto**.

## Evidencia
- `out/run-20261003-101614795668777-1576137/run.log`: `[hw] apagando` → `[hw] el scheduler no paró en 3 s: salida forzada`; snapshot: `sched-native: GIL busy — racy snapshot`, `thr 1 st=2 wait=- eip=0c300770`; smoke: `veredicto=FAIL (frames=28 fin=0 forzada=1)`, qemu rc=1; MISSING 0; run_all 26/0/1 (unit tests no lo ven).
- Último verde: 130fa1a (fin=limpio). Cambios de src desde entonces: **6dab4e0** (audio: rearmar prefill al vaciar ring, `dsound_bus.cpp`), **ac3f78c/208d4e9** (cache de estado GL D3D6, `vitagl_backend.cpp`, `gl_state_cache.h`), + tests/tools.
- Razonamiento (SO): el apagado cooperativo exige que el hilo invitado alcance un punto de sondeo/liberación del GIL; "GIL busy" con el invitado en `st=2` (running) al cancelar indica que el hilo no cede el GIL (espera/bucle sin safepoint). Un cambio en la política de prefill del ring de audio puede dejar al invitado en un bucle esperando datos del sink (productor/consumidor sin condición de salida al shutdown) → candidato nº1 = 6dab4e0; GL cache sólo toca el backend Vita (el host qemu usa refraster), candidato menos probable. Hipótesis refutable: revertir sólo 6dab4e0 en worktree → fin=limpio.

## Acción
Bisect 6dab4e0 (revert local) con `tools/run_host.sh`; si confirma, la espera del ring debe respetar el flag de shutdown (señal + `notify_all`).

## Bisect (qa, 10:30, worktree aislado de HEAD 1d52325, datos copiados) — **corrige la hipótesis 6dab4e0**
- HEAD limpio **con** 6dab4e0: `fin: shutdown requested … frames=509 missing=0` (limpio). HEAD **sin** 6dab4e0 (revert): `frames=497`, limpio. → 6dab4e0 **no** causa el cuelgue (refutada).
- El árbol de trabajo compartido falla 4/4 con salida forzada. Diferencia = cambios **sin commitear**: `src/host/host_main.cpp` (+33: overlay HW_SETTINGS_FILE, `RefRaster raster` + `cfg.d3d_backend=&raster` declarado *después* de `Stats st`, closures que capturan `raster`/`out`) y `src/runtime_vita.cpp` (+124). Hipótesis (SO, tiempo de vida): `raster` se destruye antes de que los hilos del scheduler/presenter paren (orden de destrucción inverso al de declaración) → el hilo invitado/presenter usa `cfg.d3d_backend` colgante o espera un lock; refutable moviendo `raster` antes de `st`/scheduler o parando hilos antes de salir.
- **Higiene de reproducibilidad (nuevo hallazgo)**: un checkout limpio de HEAD **no compila el host**: `tools/build_host.sh` commiteado no incluye `d3d/core.cpp, bridge.cpp, refraster.cpp` ni `io/read_stream.cpp` (`undefined reference ComBridge::acquire_d3d3_object/flip/create_texture`); esas líneas existen sólo en el working tree. Dueño: integ — commitear build_host.sh.
- Dueño actualizado: **integ/render** (host_main WIP). Prioridad: baja en tanto sea WIP; reverificar al commitear host_main.

## Recurrencia (qa, 12:15, HEAD 8ce8e06): vuelve 4/4 en el árbol compartido (smoke FAIL: fin=0 forzada=1) tras haber pasado limpio en 53f9e5c. Worktree aislado de HEAD (+ build_host.sh del WT, imprescindible): `fin: shutdown requested … frames=368` limpio. → de nuevo la causa es el WIP sin commitear (`src/host/host_main.cpp`, que ya estaba modificado en rondas verdes: su contenido cambia entre rondas). Los commits no tienen defecto. Pedido a integ/render: terminar y commitear host_main.cpp con `raster` declarado antes de `Stats st`/scheduler (o parar hilos antes de destruirlo) y commitear `tools/build_host.sh` (sin él HEAD no compila el host).

## Seguimiento integ — 2026-10-04: no reproducible en el árbol compartido

Corrí `tools/run_host.sh` con QEMU ARM, sin cambiar fuentes:
- D3D + `race_enter.script`, 25 s: PASS, 861 frames guest, 37 PNG, `missing=0`, shutdown limpio.
- D3D + `race_enter.script`, 20 s (dos repeticiones): PASS, 695/664 frames, 34/33 PNG, `missing=0`, shutdown limpio.
- Control software sin variables extra, 20 s: PASS, 1.661 frames, 53 PNG, `missing=0`, shutdown limpio.

En ambas repeticiones D3D de 20 s apareció `GIL busy — racy snapshot follows` y un EIP guest (`0c300770`/`0c3014d0`), pero luego el watchdog adquirió el GIL, pidió shutdown y `ns->run()` retornó limpio. El texto de snapshot por sí solo no significa que el pedido acotado haya fallado. Son corridas del árbol compartido con WIP en `host_main.cpp`, no un checkout reproducible fijado por commit; no demuestran una causa ni una corrección causal.

**Estado:** cerrado como no reproducible en el estado actual; sin cambio de código. Reabrir si `run_host.sh 20` vuelve a terminar en `salida forzada` o si un checkout limpio reproduce el fallo. Mantener aparte el riesgo de teardown D3D señalado arriba: estas corridas terminaron limpias, pero no prueban vida útil bajo cierre concurrente del consumer.

## Intermitencia medida (oc-qa, 2026-10-21; HEAD `f1ac5b9`, worktree aislado)
- Ronda 7 (`tools/run_host.sh 20`, carga del host ~2): **1/3 corridas** terminó con `[hw] apagando` →
  `el scheduler no paró en 3 s: salida forzada`, snapshot `GIL busy … thr 1 st=2`, veredicto FAIL.
- Las 2 repeticiones en el **mismo sha** (21:56 y 21:58) terminaron limpias:
  `fin: shutdown requested exit=0x00000000 frames=1839 / 1865 missing=0`, veredicto OK.
- Mismo patrón que la recurrencia de 2026-10-04: el texto `GIL busy` solo no alcanza para FAIL;
  el fallo real es el watchdog sin poder pedir shutdown en 3 s. 1/3 no es reproducible ni atribuible
  a un commit (el árbol cambió solo en tests entre las corridas). **No se reabre**; si aparece 2 veces
  más en 10 corridas limpias, reabrir con la serie completa (`frames`, carga, `st=` del snapshot).
