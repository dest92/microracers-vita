# Hot Wheels Micro Racers → PS Vita — PROGRESS
Leer FINDINGS.md y feature_list.json. Estado al 2026-10-02: HW-001 tiene extracción y triage estático completados por sol, test verde y entregable `docs/triage.md`; sigue `in_progress` hasta review de luna/orquestador. Payload para wine e impl: `extracted/original/Program_Files/` (ignorado por Git). HW-002 debe medir las rutas alcanzadas y la memoria residente.

## HW-033/034 — runtime — 2026-10-03
- HW-033 ya estaba `in_progress` para runtime. Creé SPEC-019 en `specs/019-runtime-perf.md`, con protocolo opt-in de PMU/per-hilo y diseño de HW-034. Commit `9f0de5b`.
- Revisión de diseño pedida a Luna en `coordination/review-HW-033-design.md`; integ recibió pedido de ventanas de menú/carrera en `coordination/integ.md`. No hubo medición de juego ni acceso paralelo a consola.
- Detecté diffs D2 concurrentes sin commitear en el checkout propio de winx86, incluyendo dynarec, GIL y scheduler. No los modifiqué. Próximo: acordar el punto de integración, validar una sonda PMU conocida y recién entonces medir Hot Wheels.
- Implementé el parche aditivo `winx86-0003-pmu-profile.patch`: PMU por runner, eventos A9, ventanas válidas <=500 ms, muestreo 1/64 de lookup `DBGetBlock`, contención GIL y top de shims. `test_runtime_pmu`, compile Vita de `runtime_vita.cpp`, build Vita winx86 y build/link/VPK D2Vita pasan en worktrees aislados. El checkout limpio requirió poblar los gitlinks StormLib y vpmu-stub y el artefacto previo glide3x.dll; los logs de los primeros intentos quedan en `out/runtime-d2vita-failures/`. Piloto de consola coordinado: deploy verificado y VitaCompanion aceptó launch, pero ambos logs quedaron con tamaño/hash/MDTM previos; env.txt restaurado y consola liberada, sin evidencia de runtime/PMU. Diagnóstico de BUG-026, sonda multi-hilo, review de diseño y medición separada menú/carrera siguen pendientes de integ/Luna. HW-033 permanece `in_progress`; no se inició HW-034.

## HW-013 — sol (2026-10-02)
Spec `specs/004-ddraw-software.md`; commits `0456e6c` (test rojo), `995f5d8` (núcleo y PNG), `ed41c8d` (adaptador COM), `994cc61` (clipping/validación). `ctest --test-dir hotwheels/build`: 4/4 verde, incluida prueba de vtables y trampolines. Estado: implementación host lista, revisión externa pendiente. Integración Vita: impl debe registrar `ddraw::install` entre `Bridge::commit` y `Bridge::link` y proveer VA guest + `present`; comunicación en `coordination/impl.md`.
Hook de integración aditivo en boot: `BootOptions::install_shims` (test `7f352ce`, implementación `13c9436`), llamado entre commit/link; `ctest` sigue 4/4 verde. El runtime Vita aún debe conectar allocator y backend `present`.
Review HW-013 (`coordination/review-HW-013.md`) señaló formato 16 bpp inconsistente en la sonda; test rojo `79e1510`, fix RGB565 (pitch 1280 y máscaras correctas) verde 5/5. Re-review puntual pendiente para marcar done.
Re-review Luna `coordination/review-HW-013.md` (commit `a5a1494`) favorable: HW-013 `done`. Queda fuera de HW-013 la conexión Vita de `present` y la validación en qemu-arm/Vita3K; el hook para impl está listo y comunicado.

## HW-021 — sol (2026-10-03)
Callback opcional `HostOps::present_sized(idx,pitch,pal,w,h)` (`6b8943e`, aprobado) conserva fallback host; CTest incorpora ruta sized (`7c5cbd8`). Backend P8 con GXP precompilados (`9cae474`, headers licenciados en `1d84a07`) y pool circular2MiB/legacy0 (`514a783`). BUG-005 resuelto visualmente: fixtures640×480 y800×600 completan600 frames según log, capturas con franjas/barras verificadas por `4585eff`. Menú real inspeccionado por sol en `hotwheels-vita-20261003-003318/shot_020.png` (HW_AUDIO=0); objetivo gráfico alcanzado. Rebuild `-j4` y CTest8/8 verdes. Sigue `in_progress` sólo por review final del fix de pools/spec; audio BUG-008 queda fuera del presenter.

## HW-007 — integ (2026-10-03): M3 gráfico Vita3K observado
`build-vita3k/hotwheels-vita-20261003-003318/shot_020.png` muestra menú real (selección jugadores), inspeccionado visualmente. Log 640x480 status=ok, >1400 frames durante harness45s. **HW_AUDIO=0 diagnóstico**: audio ON cae en Engine::start (BUG-008, fixer); no se afirma audio Vita funcional. Runtime/build/deploy revisados inicialmente; review final commits6cfa541/b20cd4b y spec pendiente. Console deploy verificado, launch sin señal de vida: VPK en ux0:data/hotwheels para instalación/abrir VitaShell a la mañana. No ejecución consola demostrada.
- HW-007 validación posterior **HW_AUDIO=1**: harness `004225`, SDL_AUDIODRIVER=dummy y sink pthread de fixer; menú real en shot015, >1300frames640x480/25s sin SIGSEGV, audio.log confirma engine/start/PCM (no audio audible demostrado, música ausente stage). BUG008 resuelto observado, review pendiente.
- HW-026 d06367e guion input porframes verde. Fix MMIO de fixer28f07aa permite carrera software: qemu `004443`,461frames30s/missing0; frame460 carrera640x480 inspeccionada. BridgeD3D6 cableado; carreraD3D pendiente de settings overlay, sin tocar datos fuente.

## HW-025 — luna (2026-10-03)
- Commits `245c452` (regresión: ordinal/orden y cambio de viewport) y `160e4eb` (viewport en firma de draw + test en CTest).
- La metadata por draw ahora captura el viewport efectivo (completo al comenzar, actualizado por `Viewport`) y exige igualdad entre snapshots. El resto de la firma conserva ordinal, textura/generación, tipo, vertex count, total de draws y dimensiones de frame; si difiere, deja el draw del frame actual intacto. HUD/UI sólo se mueve si el caller entrega ordinal clasificado como mundo.
- Presenter síncrono usa un `Replayer` persistente y el scheduler 60 Hz existente; serialización del backend sigue a cargo del loop integrado. Host: build de `test_interp60`, CTest `interp60` 1/1 PASS y runner standalone PASS. No hay aún prueba de runtime/Vita: integ reporta su carrera D3D bajo qemu en curso.
- Estado: implementación host lista, pendiente revisión externa y conexión/prueba al completar el seam de carrera D3D.
- Seguimiento de integ: [qemu] `out/run-20261003-010836/stdout.txt` registra CreateDevice D3D target=1 640×480 y `frame_00600.png` muestra carrera. Todavía no ejercita HW-025 sobre esos comandos: callback host presenta PNG directamente y no expone snapshots al adapter. Falta handoff/cableado del ring en consumer D3D.

## QA 01:19 (2026-10-03)
- HEAD 183d037: smoke PASS (out/smoke-20261003-011757.md; 224 frames qemu, MISSING ninguno, VPK md5 2ab1056f…). run_all PASS=17 FAIL=0 SKIP=1 (run_bug001, integración). Sin regresiones ni bugs nuevos.

## HW-010 — render (2026-10-03)
- Extendido `samples/d3d-ring`: escena común Vita/host con TRIANGLESTRIP FVF `0x104`, colorkey y PAL8; golden host via `RefRaster`. Build Vita y Vita3K harness pasan; `shot_006.png` coincide con golden (MAE 2.98/255 tras normalizar viewport). `tests/run_d3d.sh` PASS, hash existente intacto. Validación aislada; pendiente recorrido de carrera real Vita3K e integración de integ.

## QA 01:35 (2026-10-03)
- HEAD 5ff753f: smoke FAIL sólo por etapa tests (ctest ddraw_bridge/_sized, rc=8); qemu 219 frames MISSING 0, build Vita OK, Vita3K OK. run_all PASS=16 FAIL=1. Abierto BUG-011 → render (bridge.cpp sin commitear vs test_ddraw_bridge.cpp:90).

## HW-026 — integ (2026-10-03)
- Hallazgos y fixes: la sonda D3D inicial ya devolvía S_OK con superficie válida. `EnumDisplayModes` omitía 320/800, y DDraw rechazaba offscreens RGB565 `SYSTEMMEMORY`; al corregirlo el juego llega a 800×600 y crea Device3 sobre el backbuffer. `guest_call` evita deadlock de GIL durante callbacks D3D.
- Qemu `run_race.sh 40 d3d`, corrida `014435`: dos CreateDevice S_OK (640×480 probe, 800×600 carrera), carrera visible en `frame_00264.png`, 265 frames, missing=0, shutdown solicitado.
- Qemu `run_race.sh 60 d3d`, `013652`: 9 presents D3D/17 uploads; `frame_00200.png` carrera 800×600 inspeccionada. Evidencia explícita de ring→RefRaster más allá de inicialización.
- Build Vita + Vita3K `014222`: backend vitaGL listo, sonda D3D S_OK y >1300 presents P8; captura en intro. No se llegó a carrera D3D VitaGL: stage no encontró `settings.dat` (ruta case-sensitive) y no hay input script activado en runtime Vita. Revisión focalizada HW-026: aprobada. HW-026 sigue `in_progress` hasta validar la carrera VitaGL con input y cerrar review integral.

- HW-010 continuación render: conectado backend vitaGL a `runtime_vita.cpp`/`build_vita.sh`; VPK y Vita3K menú PASS, D3D probe S_OK. Sin ring Draw en carrera todavía; falta input/overlay D3D para completar la validación integrada.

## QA 01:50 (2026-10-03)
- HEAD b53fb51: smoke FAIL sólo etapa tests (BUG-011 sigue: ddraw_bridge test:90); qemu 266 frames MISSING 0, build Vita/Vita3K OK. run_all 16/1/1. BUG-011 repuesto en BUGS.md (otro agente lo había pisado).

- HW-010 (render) integrado Vita3K: `HW_INPUT_SCRIPT` opcional y stage aislado con DATA normalizada llegan a carrera; evidencia `build-vita3k/hotwheels-vita-20261003-015009/shot_039.png`, ring escena100=735 Draw, frame300 vitaGL. Actualización: qemu RefRaster normal `out/run-20261003-023119/frame_00269.png` también muestra carrera real, 270 frames/missing0; commits revisados y HW-010 queda done. BUG-010 de input/IO normal sigue abierto.

## QA 02:05 (2026-10-03)
- HEAD c7be53a: smoke PASS (out/smoke-20261003-020215.md; 295 frames, MISSING 0, VPK md5 5a6f0ec5…). run_all 17/0/1. BUG-011 cerrado.

## QA 02:22 (2026-10-03)
- HEAD 7db43ab: smoke FAIL: qemu 0 frames, EIP=0x1 tras DirectSoundCreate (out/run-20261003-021946). run_all 17/0/1. Abierto BUG-014 → integ (sospecha di_args en 7db43ab; sin verificar).

## QA 02:36 (2026-10-03)
- HEAD 884aa4d: smoke PASS (296 frames, MISSING 0, VPK md5 d117c168…), run_all 17/0/1. BUG-014 cerrado (verificado). Abiertos: BUG-012 (x2), BUG-010 según BUGS.md.

## 2026-10-03 — HW-010 / BUG-012 cierre render
- HW-010: `tests/run_d3d.sh` PASS (golden `0x6d85c1e26d7a5b3c`), `run_present.sh` PASS, build Vita PASS (VPK 7,960,126 bytes), Vita3K carrera real 686 Draw por vitaGL y qemu RefRaster carrera real. Review P2 GL→P8 corregida en `bcdbe33`; init parcial corregido en `10fc8bc` con re-review APRUEBA. Estado `done`.
- BUG-012 cerrado: run qemu normal `out/run-20261003-023119` produce frame D3D 800×600 `frame_00269.png`, carrera inspeccionada, 270 frames y `missing=0`; las primeras tres escenas vacías eran sólo la etapa de carga.

## 2026-10-03 — HW-026 cierre integ
- Spec 014 completa: el guion host navega a carrera; qemu/RefRaster crea D3D en 640×480 y 800×600, emite draws y presenta carrera en `out/run-20261003-023119/frame_00269.png` (270 frames, missing=0, shutdown controlado). La traza `022752` confirma 39 Begin/EndScene y DrawPrimitive S_OK; Vita3K/vitaGL ya mostraba carrera real y draws en `015009`.
- HW-007 y HW-010 están `done`; review focalizada HW-026 aprobada. HW-026 pasa a `done`. BUG-012 cerrado: los runs vacíos contaban sólo tres escenas de carga. BUG-010 también resuelto en runtime/build Vita (commit `9af52ed`); pad físico queda sin validar en consola.
- No quedan features `todo` elegibles de integ ni bugs abiertos de su ownership; HW-027 sigue asignada a deepseek.

## QA 03:35 (2026-10-03)
- HEAD 0356601: run_all 17/1/1: run_dinput.sh no compila (test_dinput.cpp:169 redeclaración). BUG-018 → luna. Producción sin cambios.

## HW-029 — render — 2026-10-04
Vita3K 40 s con VPK GXP y stage `HW_INPUT_SCRIPT`: 41 capturas, carrera visible en `shot_039.png` (~00:32), CreateDevice 640×480/800×600 S_OK, frame 300 en vitaGL y escena 100 con 972 draws, sin crash/ShaccCg. Spec `017-gxp-d3d6-vita3k-race.md` actualizada tras re-review de diseño APRUEBA; HW-029 done. HW-021 ya estaba done y ahora registra coexistencia con D3D GXP. HW-011 actualizado como mitigado para el juego; sample FFP standalone sigue sin resolver. BUG-015: mitigado para el juego. Corrección del QEMU `run-20261003-042307`: genera frames D3D 230–267; `frame_00267.png` inspeccionada muestra carrera y HUD. BUG-012 sigue cerrado; mi nota inicial confundió el límite de log de escenas con ausencia de draws.

## HW-011 — render, alcance revisado — 2026-10-04
La decisión del orquestador separa el renderer integrado GXP del sample FFP standalone bloqueado por ShaccCg. Actualicé el contrato en `specs/018-vitagl-gxp-scope.md`, title/spec/resolution de feature_list y coordinación. HW-011 permanece `review` hasta aprobación de diseño de luna; BUG-015 mitigado para el juego. No se declara funcional el sample FFP. Corrección relacionada: `run-20261003-042307/frame_00267.png` muestra carrera QEMU pese al log limitado a 3 escenas.
Review de diseño Luna sobre HW-011/018: APRUEBA. Marca HW-011 done bajo el alcance revisado; evidencia P8 corresponde a HW-021 y carrera GXP D3D6 a HW-029; el sample FFP continúa como limitación independiente BUG-015.

HW-011 cierre: review de alcance Luna APRUEBA spec 018 (`97a75d4`); review posterior aprueba la mitigación integrada (`01d2266`). Feature `done` con evidencia P8 de HW-021 y carrera D3D6 GXP de HW-029. BUG-015 sigue mitigado para el juego; FFP standalone queda como limitación.

HW-011 estado corregido a `done` tras aprobación formal de diseño Luna (`re-review-HW-011-luna.md`, commits `97a75d4`/`01d2266`); P8/D3D6 quedan sustentados por HW-021/HW-029. El sample FFP standalone sigue limitado por BUG-015, mitigado para el juego.

## BUG-020 — integ cierre — 2026-10-03
- Causa: `SetDisplayMode(800×600)` dejaba `Device::primary_` apuntando a la primaria 640×480; el shim rechazaba la nueva primaria y el guest seguía hacia `BltFast` con surface nula. Fix `fbda786`, regresión `6730556`; review estática APRUEBA. QEMU SW run `060001`: carrera visible, 469 frames, shutdown limpio, missing=0. BUG-020 cerrado (`fc6be5d`); HW-027 queda libre de este bloqueo y sigue en QA.

BUG-020 cierre consolidado: el P2 del surface COM obsoleto se corrigió en `47fe6d4`; re-review Luna APRUEBA (`coordination/re-review-47fe6d4-luna.md`), test bridge PASS y run_all PASS=19/FAIL=0/SKIP=1 según review. QEMU final post-P2 mostró carrera/no fault a 451 frames, sin línea FIN por límite de PNG/disk; `060001` conserva el FIN limpio del fix principal. HW-027 sigue en QA.

- BUG-025: diagnóstico estático encontró que `race_full.script` suelta todas las teclas en frame 12.600, mientras Vita3K alcanzó frame 14.200 en 300 s. Es una posible causa, no confirmada; próxima corrida debe extender controles y observar contador de vueltas. HW-030 continúa bloqueada tras los dos intentos autorizados por el procedimiento autónomo.

## 2026-10-03 — integ re-review LogBuffer
`90a4227` CAMBIOS estática: `present_thread` emite ventanas FPS y los callbacks D3D del guest también alcanzan el `LOG()` global; `LogBuffer` no serializa escritores. BUG-028 abierto a runtime; no ejecuté tests ni toqué su código activo de HW-033.

## HW-030 bloqueada — BUG-025 (2026-10-03)
Piloto guest DINPUT implementado y opt-in; test/build Vita PASS. Soak de 300 s confirma telemetría activa pero contador guest fijo en cero, auto contra obstáculo en `shot_300.png`; sin crash. Tres patrones de steering no resuelven orientación. HW-030 `blocked` por BUG-025; integ debe clasificar mapping/recuperación antes de reintentar resultados.

## 2026-10-03 — Bugs build verde + invariant GIL
- BUG-027/029/030 cerrados en `BUGS.md`: header PMU host/Vita (`3035e1e` en winx86), link `scripted_input.cpp` revisado; `run_all.sh` final PASS=24/FAIL=0/SKIP=1, build Vita VPK=8.005.284 B, Docker ARM `build_host.sh` PASS.
- BUG-024: causa raíz era el watchdog de host invocando `request_shutdown()` sin poseer el GIL. Fix test-first `bd8cb9e`/`0458d7a`, implementación `8ef72b2`/`2d61f65`, re-review APRUEBA. `run_host.sh 3`: límite activado, shutdown limpio, 4 frames/missing=0, rc0. Invariant: mutaciones `shutdown_`/lista/CV dentro del mutex; no bloquear para siempre si guest retiene el GIL. Borde acotado ~4 s máximo desde corte (100×10 ms de adquisición + 3 s de gracia).

## HW-039 — unicidad de BUGS.md (2026-10-03)
`test_bug_index.py` detectó duplicados BUG-023/024 antes del arreglo. Renumeré los dos registros de integ como BUG-036/037; el test valida 36 IDs, enlaces y encabezados. `run_all.sh`: PASS=26/FAIL=0/SKIP=1. Review de código y diseño APRUEBAN.

## HW-034 — encadenamiento winx86 (2026-10-03)
- HW-034 `in_progress` para runtime por orden del orquestador mientras HW-033 espera BUG-026. `WX86_CHAIN=0/1` compara funnel `LinkNext` contra direct-link preservando el default. Los ocho legs intercalados de carrera qemu pasaron smoke; media 219 vs. 163.25 frames/15 s (+34.1% en qemu-user). Ocho legs Vita3K arrancaron y ejecutaron escenas D3D; guest Hz media 15.723 vs. 9.521, sólo indicativo del emulador.

## HW-047 — prerequisitos del oracle qemu runtime (2026-10-03)
- `init.sh` reporta ARM Linux GCC/G++ y `qemu-arm` con OK/FALTA, sin hacerlos críticos al build Vita. Regresión con PATH aislado verifica los seis estados y salida exitosa cuando faltan herramientas. Code review y diseño APRUEBAN (`coordination/review-HW-047.md`); HW-047 `done`. En este entorno el worktree oracle sigue sin poder compilar porque faltan las tres herramientas; HW-034 torture queda pendiente.

## HW-033/034 — clocks y oracle mínimo (2026-10-04)
- El init de consola ahora fija y registra clocks efectivos ARM/bus/GPU/Xbar 444/222/222/166; objeto Vita PASS y test estático PASS. La sesión PMU de integ sigue en curso; no se usó consola.
- El oracle mínimo `tools/torture/run_minimal.sh` enlaza `libwinx86.a` sin `rt_boot_arm`, con A/B de cadena sobre un loop sintético. Queda listo para correr cuando aparezcan GCC/G++ ARM Linux y qemu; hoy devuelve rc 77 explícito. HW-034 sigue `in_progress` hasta obtener esa evidencia dinámica y la corrección diferencial completa.
- winx86 patches 0001–0004 aplican limpio sobre `bd5d299`; builds qemu/Vita winx86 PASS y Hot Wheels VPK PASS (11,336,988 B). El target `tools/torture` no pudo construirse desde D2Vita por WIP qemu: `frame_profile.cpp` refiere `gil::wait_read/wait_acq/wait_top` ausentes. No se modificó el checkout D2.
- No agregar caché inline de saltos indirectos: `jump_to_next()` ya usa la tabla global de bloques, con invalidación SMC existente; esperar medida PMU para justificar otro nivel y su protocolo de vida/invalidez.
- No usar Vita hasta resolución/liberación de BUG-026; medir HW-033 por escenas y por hilo. `hotwheels/build-vita/` quedó libre para HW-038.

## HW-033 — diagnóstico de sesión PMU (2026-10-04)
- La corrida con `HW_PMU=1` no produjo `pmu:` porque el resumen se emite al salir del hilo guest. Agregué marcadores opt-in `pmu: init begin/end` por tid antes del primer `cpu_->run`, incluso para `enabled=0`; el resumen final se mantiene sin escrituras por ventana.
- El `STALL` del heartbeat no es confiable en el eboot observado: las edades impresas `4294967288..90` representan deltas negativos como unsigned, aunque el source actual los recorta a cero; DDraw terminó en punto 4 y los contadores de interp/audio/logsink siguieron avanzando. No hubo transición a 800×600/carrera.
- `test_runtime_pmu_init_markers.sh` PASS. Parches 0001–0004 aplicados en orden sin `--recount` a winx86 `bd5d299`; `git diff --check` PASS. Consola y `build-vita` sin uso. Sigue pendiente una compilación que contenga los marcadores/clocks nuevos y una nueva captura coordinada de integ; HW-033/034 continúan `in_progress`.

## HW-042 — índice completo (2026-10-03)
Test-first reproduce detalle huérfano BUG-034; fila añadida con cierre/evidencia de `733dd2f`. Validator standalone PASS (37 detalles únicos/completos), reviews de código y SPEC APRUEBAN. `run_all` no verde por test D3D concurrente HW-040 (`test_d3d.cpp:67`), por eso mantengo la feature en progreso sin intervenir en render.

Reintento de `run_all` para HW-042: PASS=25/FAIL=1/SKIP=1; CTest/D3D pasan, único fallo SHORT_BURST de HW-036 mientras su owner tiene el puente en WIP. El test del índice pasa aislado. No marqué HW-042 done hasta satisfacer el gate de suite verde.

HW-042 cerrada tras reintento final de `run_all.sh` contra los commits actualizados de HW-040/HW-036: PASS=26/FAIL=0/SKIP=1; test propio y reviews ya estaban verdes.

HW-040 `done`: cache de estado GL con test host/golden, build Vita y 3 ventanas Vita3K A/B de 25 s. Mediana 4,0 vs 3,6 guest FPS; 97,98% menos llamadas estimadas desde la secuencia anterior por 64 escenas (no conteo interceptado). Reviews code y spec APRUEBAN. Ver spec 027 y FINDINGS #37.

## HW-036 cierre — prefill rearmado tras ring vacío (2026-10-03)
Re-review APRUEBA la regresión de drenaje exacto y el fix (`7a8ea35`, `6dab4e0`; informe `coordination/re-review-HW036-final-luna.md`). `run_audio.sh` PASS y cross-build del TU Vita PASS. HW-036 done; BUG-031 sigue abierto a la escucha de integ en Vita.

## HW-043 — estado vigente de BUG-002/003 (2026-10-03)
Sincronicé metadatos con cierres ya evidenciados; BUG-003 conserva reapertura como historia supersedida. Test guard exige 1 fila cerrada por ID. `test_diff_trace.py` 11/11, `test_smoke.sh` 6/6, `run_all` 26/0/1; reviews APRUEBAN.

## HW-043 — estado vigente de BUG-002/003 (2026-10-03)
Metadatos alineados a cierres evidenciados; BUG-003 conserva reapertura como historia supersedida. Guard exige una fila cerrada por ID. `test_diff_trace.py` 11/11, `test_smoke.sh` 6/6, `run_all` 26/0/1; reviews APRUEBAN.

## HW-044 — estado vigente BUG-004/009 (2026-10-03)
Cabeceras alineadas a cierres: CA14 DInput y MMIO_ALLOCBUF con carrera QEMU de 635 frames/missing=0. `run_dinput`, `run_winmm` y `run_all` 26/0/1 PASS; reviews APRUEBAN.

## HW-045 — estado vigente BUG-007 (2026-10-03)
Alineé la cabecera al cierre registrado: `ddsCaps` offset 104 en mingw/spec, DDraw 2/2, reviews ABI/regresión APRUEBAN. Guard índice y `run_all` 26/0/1 PASS.

## HW-046 — estado vigente BUG-005 (2026-10-03)
Cabecera marcada cerrada sólo para validación emulada según review-HW-021; sin afirmar consola ni cerrar HW-021. `run_all` 27/0/1 PASS; reviews APRUEBAN.

## HW-041 — métricas de carga cerradas (2026-10-03)
Re-review APRUEBA `67eacd1`; tests sintéticos y la línea base qemu del owner cubren TTF1/fps/frames y propagación de fallos. HW-041 done; no evidencia Vita/Vita3K.

## HW-027 — soak cerrado (2026-10-03)
Re-review APRUEBA `64cfd4c`: la poda tiene cota total y selftest PASS. La corrida qemu de 30 minutos previa sigue siendo la evidencia dinámica; no se repitió el soak.

## HW-048 — cleanup de RETR deploy cerrado (2026-10-03)
TDD `debf09d` reprodujo el `.new` huérfano en fallo parcial de RETR; `85c72e2` limpia el temporal antes de relanzar el error y reporta errores combinados si DELE falla. `test_vita_deploy.py` 15/15 PASS, reviewer APRUEBA `4d5217f`; sin consola.

## HW-049 — cleanup tras SIZE no verificable (2026-10-03)
Test-first repro `SIZE` de transporte y 550; fix intenta limpiar `.new` también cuando `size()` devuelve `None`, y reporta cleanup no confirmado. Suite deploy 19/19, reviewer APRUEBA. Sin VitaShell/consola.

## 2026-10-03 — HW-038 Vita3K
Con el VPK HEAD y stage aislado (`HW_AUDIO_SELF_PACED=1`, `race_enter.script`, 40 s), Vita3K PASS: 22 capturas, carrera visible en `shot_040.png`, D3D/vitaGL frame 1.100 y escena 900. Tres intervalos consecutivos del último DSound self-paced: 237.056 frames/10.159 ms, 233.472/10.137 ms y 233.472/10.116 ms; 0 underruns/overruns. Telemetría de SDL dummy, no sustituye la aceptación física de 30 s. Consola real permanece `CONSOLE_LOCKED` por pantalla de bloqueo.

## HW-050 — instrumentación de modos DirectDraw (2026-10-03)
HW-050 done: callback opcional registra solicitud/aceptación/modo efectivo. Tests DDraw 2/2, build Vita + Vita3K PASS; traza muestra 800×600 aceptado y target D3D 800×600 tras settings. La queja 320×240 de BUG-032 sigue pendiente de captura en consola. Código `1141bfa`, spec `2a34c7c`/`e26cf0f`.

## HW-060 — captura aislada de audio.log Vita3K
TDD `867fee2` → helper + hook `8b7da51`. Test unitario 3/3 PASS. Smoke real de `vita3k_run.sh` 20 s PASS; el artefacto run-specific de 1.320 B coincide byte a byte con el delta fuente 90.522→91.842 B y no altera el prefijo histórico. Review de código pendiente. HW-038 se conserva bloqueado por BUG-035/CONSOLE_LOCKED.
Re-review APRUEBA `7bcb91b`/`812db4c`; HW-060 done. El runner detecta reemplazo/truncate+regrow por device/inode+SHA-256 de prefijo, y conserva artifact aun ante fallo al arrancar Docker. Última suite 6/6 PASS.


## HW-061 — logs de soporte por corrida Vita3K
Runner captura también delta `progress.txt`, honra `VITA3K_HOME` y mantiene el home compartido como default. Vita3K 20 s: progreso 717 B y audio 1.320 B, exactos al delta fuente; test runner aislado 6/6 PASS incluyendo fallo de `docker run`. Review `3b5ebcc` y re-review `78a2ba4` APRUEBAN. HW-061 done.

## HW-062 — delta de vita3k.log
Snapshot/extract (`98f6fe9`) captura sólo el delta append o todo el archivo si fue reemplazado. `flock` (`e046088`) serializa home compartido. Smoke Vita3K 10 s PASS; log nuevo 77.824 B igual al artefacto tras reemplazo desde 86.016 B; audio 890 B y progress 717 B igual a sus deltas. Tests 7/7 PASS; review detached `7502a03` APRUEBA. HW-062 done.

## BUG-039 — build host limpio (integ, parcial)
`60df553`/`39be502` corrigen el source list de `build_host.sh`. Worktree limpio `e40c68c` build ARM en `hw-host-arm:24.04`: PASS (`hotwheels-host` enlazado). No ejecuté QEMU ni `run_all`; permanecen fallos winmm/test API asignados a luna/re. BUG-039 sigue abierto.

## HW-053/HW-055 (render, 2026-10-03)
Pipeline D3D de tres slots y texture cache por FNV64 completados. Builds Vita aislado / qemu, CTest D3D pipeline 4/4 y reviews de código/diseño PASS; A/B medidos en Vita3K original/interp60 y qemu microbench. Ver `coordination/render.md`, specs 037/038 y FINDINGS; no es evidencia de consola. HW-030 sigue bloqueada por BUG-025 (contador real de vueltas/piloto pendiente).

## HW-063 — carry de estado persistente en drops D3D
Fix `447b786` guarda transiciones finales TexUpload/TexFree y binding Texture cuando se descarta READY; golden host demuestra el Draw conservado en verde. Revisión detectó binding faltante y aprueba tras la corrección. CTest 4/4, build Vita y Vita3K 25 s PASS (228/230, 2 drops de arranque). Sigue resultado de estrés/telemetría abajo.

QEMU stress del pipeline (2026-10-03): 879/915 drops en 35 s, repetición 441/466; guest 39.6–40.0 Hz y consumer 1.466 Hz. Backend uploads 0.066–0.333/frame según ventana. Sync baseline 36/36, 1.310 Hz, 0 drops. `frame_00200.png` muestra escena texturada durante pipeline, pero falta aislar recuperación de una transición de ring descartado. Ambas pruebas pipeline dieron SIGSEGV/139 después del shutdown cooperativo; control sync rc 0 (BUG-043, integ). HW-063 permanece in_progress hasta validar recuperación del carry bajo drops.

Seguimiento: `resource_ops_dropped/carried` verifica el camino bajo presión; QEMU 25 s: 360.868 ops resource en rings descartados→20 estados reinyectados, 453 drops, 40.032/1.566 Hz guest/consumer, 0.250 backend uploads/frame. Golden RefRaster confirma pixel final; captura qemu muestra escena texturada. CTest D3D/queue/bridge/interp60 4/4, build Vita PASS. BUG-043 post-shutdown SIGSEGV sigue asignado a integ.
HW-063 done: review de telemetría APRUEBA tras cubrir `close(false)`; commits `c9a860d`, `c93fba6`, `591f666`, `579d19d`. La falla posterior al shutdown se sigue como BUG-043.

HW-065 done (render): cache de habilitación de atributos del vertex shader D3D; spec `042-vitagl-vertex-attrib-cache.md`, review Luna y review de código aprobadas. Build/test host `test_d3d` PASS. Vita3K A/B VPKs del mismo commit base `34d34d6` (base `0673199d…`, cache `9c0b882b…`) con `race_enter.script`: guest FPS estable base 33.236–35.822 Hz, cache 32.639–37.713 Hz (variación solapada; no atribuir ganancia de FPS). Comparación P8 mismo guion/estado: viewport 764×535, MAE RGB 2.925, 97.019 % píxeles con delta máximo ≤8. Cache instrumentado: 55.532 draws, 4 enables reales, 222.124 evitados (>99.99 %). Secuencia compartida P8→D3D→P8→D3D visible en Vita3K.


## HW-051 — fix atribución y DX-001 (2026-10-03)
El muestreo publica EIP al entrar cada bloque; top-30 qemu menú/carrera actualizado en `docs/guest-profile.md`. El API dyn86 del profiler ahora es opcional al link: test con patch y sin patch PASS; objeto Vita registra weak refs. Build Vita aislado aún bloqueado antes de link por integración WIP de VFS en `src/shims`/`mmio_bridge.h`; reprobar al cerrar DX-057. Vita3K y review del corte pendientes; HW-051 sigue `in_progress`.


## HW-030 — waypoints por posición (2026-10-03, render)
Reactivada por orquestador para resolver BUG-025. Implementados recorder DINPUT X/Z Q12 + teclas, parser/extractor, replay espacial con corrección proyectada y guardas de rango; tests host y build Vita PASS. Cuatro corridas Vita3K headless con XTest no salieron del menú (sin filas waypoints ni D3D scene), por lo que no existe ruta grabada ni resultado de carrera. BUG-025 sigue abierto; HW-030 está bloqueada por BUG-041 (XTest no llega a DINPUT); pasar a la siguiente tarea render hasta que integ resuelva el canal interactivo. Commits `bbcdbb6`, `7f79b52`, `4f0d2c3`; ver `coordination/render.md` y spec 020.


## HW-057 — winx86-dx reutilizable (2026-10-03)
Extracción de dinput, winmm/MCI/MMIO, io/VFS/ReadStream, DSound, DDraw, D3D y shims base a `winx86-dx`; Hot Wheels conserva perfil/mapeos/rutas/quirks, bridges PE32 y backend. Tests y reviews registrados en `coordination/dxlib.md`. Validación final: run_all PASS=33/FAIL=0/SKIP=2; los dos suites omitidos por modo host pasaron ejecutados aparte (BUG-001 qemu: 4000 traps y shutdown limpio; load_metrics PASS). `tools/run_host.sh 10` construyó y ejecutó el port en Docker/qemu-arm: PASS, 32 frames normalizados, 598 procesados, missing=0, exit 0. `build_vita.sh` PASS, VPK 1,918,683 B. El qemu-arm local falta; se usó `hw-host-arm:24.04`.

## HW-064 — USER32 PeekMessage idle opt-in (dxlib, 2026-10-03)
Implementado/revisado: callback genérico sólo al sondear cola vacía; Hot Wheels habilita umbral `HW_PEEK_IDLE_US` y cede 1 ms cuando detecta polling apretado. `run_all.sh` 34/0/0/2, build Vita PASS (VPK 1.921.919 B), smoke QEMU PASS (`missing=0`, shutdown limpio). A/B QEMU carrera: −39,0 % medias de ms CPU/frame, frames pareados dentro de 4,1 %. Tasa vacía trazada: ~17,3k/s → ~535/s (−96,9 %). Revs Luna/reviewer APRUEBAN. HW-064 queda done; detalle y límites en `coordination/dxlib.md` y spec 041.


## HW-057 — DXlib lista para nuevos ports (2026-10-03)
Cerrada tras extracción modular, enlaces CMake DDraw/D3D, guía `docs/PORTING.md`, `winx86-dx/README.md` y template `winx86-dx/template/hello-port`. El PE32 llvm-mingw importa DDraw/DSound/DInput y corre bajo qemu-arm mediante `winx86::Bridge`; PASS, unresolved=0 y los tres cores ejercitados. Review code + Luna APRUEBA. En feature_list: `done`. El build Vita existente pasó con fallback transitorio para las listas todavía reservadas por runtime HW-051.

## HW-054 / HW-059 — runtime (2026-10-03)
HW-054: Ghidra encontró 96 `FNSTSW AX` y 84 consumidores inmediatos en caminos reales. Patch aditivo implementa PE/DE/IE sticky, ES/B según máscaras y C1; cubre el fast path exacto FILD/FISTP sin usar VFP indeterminado. Oracle Wine x86/QEMU ARM: 13/13 casos de valor y status iguales. No valida entrega #MF no enmascarada; esa limitación queda documentada. Review de código aprobada; HW-054 global sigue `in_progress` por el resto de CA1/CA4/CA5. HW-059: SPEC-045; formato `WXTCARM3` integra tabla de fixups de ramas PC-relative locales y `ADD #dyn86_membase`. Torture PASS y review pendiente. Benchmark menú QEMU 4 pares × 2 s: 268/1425 hits por corrida, 103.368 B, smoke PASS y hash de primer frame idéntico; medianas 221 ms frío/213,5 ms tibio; delta pareada −4 ms, mejora no distinguible del ruido. Build `TARGET=vita` de libwinx86 PASS (168 objetos); build completo D2Vita pendiente. HW-051 conserva `in_progress` porque SPEC-037 CA3 Vita3K no está satisfecho.

## 2026-10-21 — sol-gfx HW-068, C1 parcial
Auditoría D3D6 en docs/d3d6-gaps-bug047.md. Overflow de longitud u16 del ring reproducido por 4 fixtures (texturas/draw/interp/carry); spec 0ce7917 → rojo 51eaf4e → fix 9da4281. Host+ASan/UBSan+fixture QEMU ARM PASS, CTest bridge/queue/interp60 3/3, golden idéntico. Build Vita aislado PASS 1.936.046 B (fab6a3bd…95a5a6c6); review rev-gfx diseño/código APRUEBA. No atribuido a BUG-047: pendientes máximos reales y oráculo por pista. Orquestador repartió sol-gfx=Vita3K/GXP, opus=qemu+Wine; no se usan scripts track2/3 hasta selección verificada por opus.

## 2026-10-22 — HW-075 primer corte diagnóstico (sol-cores)
SPEC-064 + implementación d19ab13/hooks978db12/fix7487572/tests bb30c91: snapshots crudos de guest/render/DSound/feeder/decoder MCI, opt-in HW_THREAD_CPU=1. Reviews par-luna CA1 y sonnet código/follow-up aprobados; host/ASan/UBSan/TSan, run_audio y build Vita PASS según entrega/reviews. Orquestador verificó artefactos locales: VPK 1.966.708 B SHA256 b9b5cd4bc7d85868bc52a67edc9a125eba90342c024083e59b41eeccd9d8366a; eboot 1.970.283 B SHA256 3ec3c7280eb6b0b72dee73aabc126abda056f7e0edac632ca0dcc3ed62db9263. Manifiesto incluye WIP previo usado; protocolo docs/hw075-diagnostico.md. Sin deploy ni validación dinámica. HW-075 sigue in_progress: calibración/captura física y offload medido pendientes; no mejora FPS afirmada. Decoder MCI sin afinidad explícita es candidato a medir, no cuello confirmado.

### HW-075 — cierre de preparación de calibración
CAL1 reproducido APRUEBA par-luna 881b9b6; CAL2 preparado APRUEBA 790e628 (siete inputs prehash, manifiesto, build desde copia y VPK verificados). Código/follow-ups Sonnet APRUEBA. V4 congelada: VPK SHA256 e8f9b3bd4ee2b04a7276b2fa248f04242042c3e4113c08948fa25647ca7f5064; juego diagnóstico previo intacto. Protocolo samples/thread-clock/README.md. Sin deploy ni evidencia física; HWCLOCK01 requiere ampliar explícitamente alcance antes de instalar. HW-075 in_progress: primero A/B OFFICE e6749fb; luego CAL3/CA2 físicas, determinación de unidad y offload medido. No continuar implementación especulativa antes de datos. Resumen agregado por orq-sol sin incluir WIP ajeno en commit.
