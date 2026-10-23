# HW-030 — completar una carrera D3D6 con Vita3K o QEMU

## Problema y contrato

HW-029 prueba entrada a pista y renderer, pero termina antes de demostrar el ciclo completo de carrera. HW-030 valida DirectInput reproduciendo waypoints capturados desde una carrera conducida con teclado y corrigiendo desvíos desde la posición del vehículo leída en memoria guest. Se acepta Vita3K o QEMU para el criterio observable.

El VPK debe arrancar, navegar a carrera, largar, completar las tres vueltas, mostrar resultados y volver al menú principal. El recorrido no debe terminar en crash. Guardar capturas de la largada, pantalla de resultados y menú de retorno junto con el log del run.

## Guion y validación

- `HW_RACE_RECORD=1` registra cada 8 polls DINPUT y cada cambio de teclas como `tick x_q12 z_q12 keymask`. `tick` avanza una vez por poll activo; una transición fuera del muestreo periódico se escribe en su propio tick y reemplaza la siguiente muestra periódica, así que no hay ticks duplicados dentro de una carrera. X/Z son enteros con signo Q12 (4096 = una unidad del mundo); `keymask` decimal usa bits UP=1, DOWN=2, LEFT=4, RIGHT=8. La sesión manual usa teclado de Vita3K (`ArrowUp/Left/Right`; X/Enter para menú). El control se mantiene entre muestras. `tools/race_waypoints_from_log.py run.stdout race_waypoints.csv` extrae las filas `[waypoint]` del log; ejecutar una carrera por log porque cada largada reinicia `tick`.
- `HW_RACE_WAYPOINTS=ux0:data/hotwheels/race_waypoints.csv` carga al menos 3 filas con ticks estrictamente crecientes. Cada poll busca el punto más cercano en la ventana monótona `[índice actual, índice+64]`, toma sus teclas y usa el segmento hasta la muestra `índice+3` (limitado a la última muestra). Proyecta X/Z actuales sobre ese segmento con `t=clamp(dot(P-A,B-A)/|B-A|²,0,1)` y mide distancia a la proyección; si el segmento es degenerado (`|B-A|²=0`) o ya no queda lookahead, no corrige ese poll. Cross negativo para el segmento +Z identifica el lado derecho. Si la distancia supera 3 unidades, reemplaza LEFT/RIGHT por el giro hacia la ruta; aceleración y reversa siguen la captura. La ruta debe cubrir la carrera completa y todas sus vueltas.
- El stage mantiene exe/datos locales fuera del VPK y envía a `env.txt` sólo las opciones de grabación o reproducción elegidas.
- Ejecutar Vita3K o `run_race.sh` QEMU hasta observar `largada → vueltas 1/2/3 → resultados → menú`. Capturar el contador/HUD y la pantalla final; el contador `record+0x1c` previo no se considera evidencia hasta corroborarlo dinámicamente. Comprobar salida sin crash/fault.
- Registrar las capturas en `hotwheels/out/` (ignoradas, no commitear imágenes/activos del juego), el path del run, duración y los hitos observados.

## Límites

La validación usa el emulador y el guion DINPUT; no demuestra pad físico en consola. No se usan cheats que reduzcan el número de vueltas.

## Piloto y fundamento de bajo nivel

Ghidra indica que la variable global en VA `0x00475460` contiene el puntero guest al array de jugadores (stride `0x4bc`); jugador 0 contiene el nodo de vehículo en `+0x1c0`; su campo `+0x1c` apunta al estado físico. `FUN_004345b0` integra X/Y/Z Q12 en offsets físicos `+0x1c/+0x20/+0x24`; X/Z forman el plano de pista. Se leen con `Cpu::read` y validación de cada puntero, sin retener memoria guest. La referencia es `shared/ghidra-proyectos/HotWheels.gpr`, función `FUN_004345b0`; la hipótesis X/Z y el signo aún requieren validación dinámica.

El piloto anterior de pulsos DIK_UP/LEFT/RIGHT no completó la carrera y su campo etiquetado como vueltas no está validado; ver BUG-025. El recorrido pasa a capturarse y reproducirse por posición.

La lectura se hace con `Cpu::read` dentro de `InputSource::poll`, invocado sincrónicamente por GetDeviceState. El GIL de winx86 serializa el estado host, no actúa como lock de memoria guest; se infiere que simulación y DINPUT poll comparten el hilo principal. El acceso queda por VA validada por backend, no `hostptr`, y sólo lee palabras alineadas. Antes del replay largo se calibra el signo: grabar tramo +Z y pulsos LEFT/RIGHT, comprobar el lado observado de X/Z respecto de la ruta; la prueba sintética fija cross negativo como derecha para +Z. Si la observación contradice la convención, invertir el giro correctivo. Criterio final: resultados y retorno al menú sin fault.
