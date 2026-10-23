# BUG-024 — teardown del host: assert `g_owned` en gil.cpp:242 → rc134 tras "apagando"

Nivel: **medido en host (qemu)**, mimo, 2026-10-03. Severidad: mayor (el apagado
limpio exige rc0; el soak HW-027 falla en el cierre aunque la carrera va bien).
Estado: **cerrado por causa raíz** (`8ef72b2`, bounded GIL; `2d61f65`, reintento acotado). Feature: HW-027.

## Evidencia
`hotwheels-host: /work/d2vita/D2Vita/third_party/winx86/src/runtime/gil.cpp:242:
void d2rt::gil::assert_held(): Assertion 'g_owned' failed.`
tras `[hw] apagando` → `timeout: the monitored command dumped core` → `== salida no cero: 134`.
- Run: `out/run-20261003-062031` (286 frames, sin línea `fin:` en run.log) y repro en
  `out/run-20261003-062322` (307 frames, verdict soak `wrapper salida no cero:134`).
- Intermitente: `out/run-20261003-061129` (660 frames) cerró con
  `shutdown requested exit=0x00000000` sin assert.

## Causa raíz e invariant del GIL
El watchdog host corre en un hilo detached. Tras el límite, imprimía `apagando`
y llamaba `NativeScheduler::request_shutdown()` sin adquirir el GIL. Ese método
modifica `shutdown_`, solicita stop del CPU y recorre `threads_` para broadcast;
su contrato exige el GIL. `gil::assert_held()` no era un falso positivo del
teardown: `g_owned` representa el ownership del mutex global, se escribe bajo
ese mutex y sólo puede consultarse como invariant mientras el caller lo posee.
El watchdog violaba el contrato; además, leer ese `bool` concurrentemente sin
el lock no sería una observación C++ sincronizada.

El arreglo usa `gil::try_lock()` hasta 100 veces con pausas de 10 ms. Si lo toma,
llama `request_shutdown()` y libera el mutex; si no, conserva el `_Exit(3)` sin
esperar indefinidamente detrás de un guest o shim trabado. El grace de 3 s
empieza tras el intento (caso extremo ~4 s desde el corte). Toda mutación
estructural ocurre con exclusión mutua; el watchdog no bloquea ni finge
ownership. Test-first: `bd8cb9e` rojo, `0458d7a` cubre el reintento acotado; fix
`8ef72b2` + `2d61f65`; re-review `coordination/review-2d61f65.md` APRUEBA.

## Validación
- `bash hotwheels/tests/run_all.sh`: 24 PASS, 0 FAIL, 1 integración qemu omitida.
- `bash hotwheels/tools/run_host.sh 3`: gatilló el límite/watchdog; FIN limpio,
  `frames=4 missing=0`, verdict `OK`, salida 0. La corrida usó 3 s y quedó
  eliminada por política PASS; el resumen quedó en stdout.
- Gate solicitado de build: `build_vita.sh` generó VPK de 8.005.284 bytes;
  Docker ARM `build_host.sh` compiló/linkeó `hotwheels-host`.
