# BUG-025 — La carrera guionada no alcanza resultados en Vita3K

- Estado: abierto; bloquea HW-030. Dueño sugerido: integ/gameplay con render para repetir Vita3K.
- Nivel: emulador Vita3K, 2026-10-03. Severidad: mayor para el gate HW-030.

## Evidencia

`HW_INPUT_SCRIPT=ux0:data/hotwheels/race_full.script` se cargó por DINPUT en el runtime Vita.
Dos intentos arrancan la carrera sin crash observado:

- `build-vita3k/hotwheels-vita-20261003-062448`: 140 s, acelerador DIK_UP continuo; el HUD llega a 02:30 y sigue en pista.
- `build-vita3k/hotwheels-vita-20261003-063026`: 300 s, acelerador continuo más pulsos DIK_LEFT/RIGHT alternados; 301 screenshots, HUD llega a 05:53. `shot_300.png` sigue mostrando carrera activa, sin resultados ni menú.

El segundo log registra `input por frames`, D3D frame 14200 con `backend=vitaGL` y escenas D3D durante la ejecución. Vita3K reportó 104 errores de archivo/módulo ausente (por ejemplo `proc/self/maps` y archivos opcionales de save); no se encontró SIGSEGV/fault en el log. El harness envió TERM al vencer los 300 s, por lo que esto sólo demuestra ausencia de crash antes del timeout.

## Próximo diagnóstico

Verificar por telemetría o captura si el jugador cruza la línea de meta y aumenta el contador de vueltas. Ajustar navegación/controles guionados con esa evidencia; no usar el cheat `onelap`. Repetir hasta capturar largada, resultado y menú después de la tercera vuelta.

## Hallazgo estático del guion (2026-10-04)
`tools/race_full.script` mantiene DIK_UP desde frame 600 por 12.000 frames, hasta el exclusivo 12.600. Las correcciones de dirección terminan antes: último pulso inicia en 11.680 y dura 80 frames. El segundo run informa D3D frame 14.200 al timeout de 300 s; por lo tanto, durante al menos los últimos 1.600 frames no había ninguna tecla guionada. Esto puede contribuir a que el vehículo no complete la carrera, pero no prueba causalidad ni confirma cruce/contador de vueltas. Próxima corrida, cuando se retome BUG-025: extender acelerador y correcciones más allá de la duración observada y capturar el contador de vueltas; conservar la restricción de no usar `onelap`.

## Piloto DINPUT guest — reintentos render (2026-10-03)

El shim DINPUT ahora puede envolverse con `HW_RACE_PILOT=1`: fuerza DIK_UP mientras `DAT_00475108 == state_raceLoop`, observa por `Cpu::read` el short `players[0]+0x1c`, y aplica pulsos de giro. El estado/contador se reportan por `sceClibPrintf` en Vita para que una vuelta sea evidencia directa, no inferencia desde el HUD.

- Unit `bash hotwheels/tests/run_scripted_input.sh`: PASS; prueba aceleración, pulsos, baseline y parada de giro al completar +3. Build final `bash hotwheels/tools/build_vita.sh`: PASS (VPK 8,005,394 B); el soak usó el VPK previo de 8,005,282 B.
- Vita3K run `build-vita3k/hotwheels-vita-20261003-085012233864256-1339889`: 300 s, `HW_RACE_PILOT=1`, baseline guest 0, ningún log de incremento de vueltas, D3D frame 11.100 con `backend=vitaGL`, sin fault/SIGSEGV. `shot_300.png` muestra aún carrera activa a 04:45.08, sin resultados; auto contra geometría/obstáculo. Harness PASS sólo significa boot normal + timeout solicitado, no gate de carrera.
- Las capturas/logs prueban que el shim activa y corre, pero tres variantes de pulsos (16/240, 48/360 izquierda-primero y 48/360 derecha-primero) no producen incremento. El cambio del sentido inicial no rescata al auto; el problema ya requiere identificar orientación/estado de recuperación o el mapeo efectivo del control guest.
- Un intento QEMU con copia temporal de winx86 se canceló después de ~600 escenas: el renderizador de referencia no dio iteración útil. El checkout winx86 compartido no se modificó. El build host directo sigue afectado por una referencia PMU sólo Vita en `sched_native.cpp`.

**Estado histórico:** BUG-025 sigue abierto. HW-030 fue reactivada `in_progress` por el orquestador para implementar captura/replay por posición; el criterio de carrera aún no está satisfecho.

## Ghidra — controles y telemetría RacePilot (2026-10-03)
Análisis headless de `HotWheels.exe`, proyecto `/home/user/ghidra-proyectos/HotWheels`, sin editar el proyecto:

- `FUN_00426a80` inicializa los códigos: `0xc8` Up, `0xd0` Down, `0xcb` Left, `0xcd` Right, `0x36` Shift, `0x9d` Control. `inpUpdate` transforma Up/Down/Left/Right en bits `0x10/0x40/0x80/0x20`; `FUN_00412c90` aplica `0x10` al campo acelerador `player+0x314`, `0x40` a reversa `+0x318`, y los bits de izquierda/derecha producen dirección `-45/+45` en `+0x308`. Los DIK usados por `race_full.script` son correctos según el default guest; la hipótesis de un mapeo erróneo de esos cuatro controles pierde sustento estático.
- **Telemetría no validada:** RacePilot lee un `short` desde `*(uint32_t*)0x00475460 + 0x1c` y lo etiqueta como vueltas. Ghidra muestra a `FUN_004181b0` modificando el campo de esa misma base/stride como contador corto de una transición/secuencia; no encontré xref que pruebe que sea el contador de vueltas. Por eso `baseline=0 / sin deltas` de las capturas anteriores no demuestra que el auto no complete vueltas.
- El script anterior deja de sostener Up después de frame 12.600 y termina los giros antes; el último run observó frame 14.200 al timeout. Es una brecha de cobertura, no causa demostrada.

Siguiente histórico: render/gameplay debía localizar un contador de vueltas corroborado. La decisión actual de HW-030 reemplaza ese plan por grabación/replay de waypoints desde posición; las capturas previas no prueban cruce de meta.


## Waypoints DINPUT — implementación y captura fallida (render, 2026-10-03)
Implementé `HW_RACE_RECORD=1`, lectura guest validada de X/Z Q12 (`player+0x1c0 → node+0x1c → physics+0x1c/+0x24`), playback espacial monótono y corrección por proyección/lookahead. `tools/race_waypoints_from_log.py` extrae filas `[waypoint]`. Tests host PASS; build Vita PASS (VPK 1.916.585 B), con alias include temporal necesario porque `tools/build_vita.sh` aún no incluye `winx86-dx/include`.

Cuatro corridas headless Vita3K (80/65/55/50 s) bootearon sin fault. Inyección XTest con taps Return/X y flechas no entró a carrera: cero muestras `[waypoint]`, CreateDevice 640×480, `guest_d3d_frames=0`; capturas de menú. Una captura expuso el modal de actualización; dismiss/click/focus lo cerró, pero no resolvió navegación. El harness PASS sólo certifica boot/timeout normal. No hay ruta real ni replay/resultados; BUG-025 sigue abierto; HW-030 está `blocked` por BUG-041, que impide la captura manual en el runner headless.

## Seguimiento render — bloqueo de grabación real (2026-10-03)

Los reintentos XTest aislados de BUG-041 (45 s y 50 s, env sin `HW_INPUT_SCRIPT`) siguen en selección: CreateDevice S_OK pero cero frames/waypoints. Por eso no pude grabar una ruta de teclado real en Vita3K ni validar/recalibrar el piloto espacial en esta sesión. BUG-025 sigue abierto y HW-030 bloqueada por BUG-041; no se afirma llegada a resultados.
