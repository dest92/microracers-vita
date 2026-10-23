# Review HW-075 — SPEC-064 y fixture host

- Revisión de `4060c44` (`hotwheels/specs/064-paralelismo-vita.md`) y del estado disponible de `tests/test_thread_cpu.cpp`.
- Resultado: **CAMBIOS concretos antes de aprobar CA1/CA2**. Revisión estática; no ejecuté el test. El test está en rojo por falta de `src/diag/thread_cpu.h`, según el estado reportado por el owner.
- Alcance respetado: no edité SPEC-064, test ni código de sol-cores. Sólo escribí este informe.

## Lo que está bien especificado

- Contrato raw coherente con el acuerdo: `scene=unverified`, `scene_declared` no probatoria, sin `CPU_us`, porcentajes ni saturación antes de calibrar `runClocks`.
- Incluye los cuatro roles solicitados: caller/guest, consumer D3D, productor self-paced DSound y feeder sceAudioOut. Capturar productor además del feeder evita que el tiempo bloqueado en audio oculte el trabajo de mezcla.
- Los snapshots tienen timestamps individuales y el texto aclara que los intervalos no son simultáneos exactos. Conserva ambos extremos y maneja errores/reset sin convertirlos a cero.
- No requiere PMU. Evita `wx86_vita_core_window_line()` por la consulta de CPU3 y el registro no sincronizado. La separación entre primera instrumentación diagnóstica y medición posterior de carrera/offload es explícita.

## CAMBIOS pedidos

1. **CA1 / escenas declaradas:** el contrato de línea 10 exige que el primer corte publique `scene=unverified` y `scene_declared`, pero `test_thread_cpu.cpp` sólo prueba deltas y el registry. Agregar fixture/caso de contrato que alimente `CreateDevice=800x600` con `scene_declared=menu-production`, y otro con `scene_declared=race`, y exija `scene=unverified` en ambos mientras conserva la declaración. Así CA1 prueba que resolución y env no se convierten en evidencia de escena.
2. **CA1 / extremos del TID y error:** probar TID inicial ausente además del final ausente (`a.tid=0` y `b.tid=0`). En los dos casos de API fallida, afirmar el estado inválido y que ningún delta previo puede salir como válido; el comentario de líneas 31–32 dice “un error”, pero ese tramo ejercita `CounterReset`, no `ApiError`. Mantener el caso de delta cero como válido cuando ambos snapshots pasan, TID coincide y tiempo crece.
3. **CA1 / concurrencia:** el test lanza escritores simultáneos en slots distintos, hace `join()` y recién después lee. Esto prueba el resultado final básico, pero `join()` aporta sincronización, así que no ejercita la publicación acquire/release que invoca la SPEC. Aclarar si sólo se exige identidad final por rol (en cuyo caso el criterio no debe atribuirle prueba de release/acquire) o agregar un lector concurrente/patrón de publicación acorde a los datos que realmente se comparten. No hace falta una prueba probabilística de orden de memoria para un único `atomic<int>` independiente por slot.
4. **CA2 / evidencia observable:** precisar que el log de integración debe mostrar los cuatro roles identificados, `rc`, TID, extremos de `runClocks`, timestamps por extremo, intervalo real, afinidad/CPU/prioridad/estado en ambos extremos y estatus explícito de delta válido/inválido. Si algún rol no existe o su API falla, publicar el estado de ausencia/error sin presentarlo como cero. Incluir `scene=unverified` y `scene_declared` si se configuró.
5. **CA2 / ventana de snapshot:** el collector toma cuatro snapshots secuenciales. Conservar el intervalo individual de cada rol y correlacionarlo con FPS por timestamps; no rotular el grupo como una ventana simultánea de 10 s. Marcar si cambian máscara de afinidad o prioridad entre extremos, para no atribuir el delta a una configuración estable. El delta raw puede seguir siendo válido como delta del hilo.
6. **CA2 / opt-in y costo:** exigir que `HW_THREAD_CPU=0` no emita estos registros y que `=1` haga sólo snapshots del collector, sin consultas repetidas en draw/frame/audio. La compilación del VPK por sí sola no acredita ese límite; adjuntar una comprobación de ruta de llamada/código y una captura diagnóstica que confirme el intervalo real y los campos anteriores.
7. **Protocolo A/B:** dejar claro que A/B `HW_THREAD_CPU=0/1` mide el costo de diagnóstico, pues la SPEC no cambia afinidades ni prioridades. La medición de una reducción serial pertenece al CA4 posterior y requiere su propia condición experimental y aprobación de consola por el orquestador. La clasificación de carrera del CA4 no debe atribuirse a este primer corte: requiere witness separado y PC verificado, como ya especifica el protocolo.

## Test en el estado revisado

`tests/test_thread_cpu.cpp` contiene pruebas útiles para delta de 64 bits (>32), delta cero, API error en ambos extremos, TID ausente al cierre/cambiado, timestamps iguales/decrecientes y contador decreciente. También intenta preservar identidades de cuatro slots escritos en paralelo. Falta `hotwheels/src/diag/thread_cpu.h`, por lo que todavía no existe implementación contra la que validar tipos, estados o la semántica del registry; esto es consistente con un test rojo previo a implementación, no una falla de SPEC por sí sola.

La línea 31 del test necesita corregirse: el comentario de no reutilizar un delta previo no coincide con el caso que sigue, que sólo cambia `b.clocks` y espera `CounterReset`. Añadir una aserción explícita `ApiError` con delta inválido/estado no válido, o ajustar el comentario si el tipo no conserva deltas entre llamadas.

## Dictamen

El diseño de base y el alcance son aprobables. CA1/CA2 quedan en **CAMBIOS** por falta de casos de exclusión de escenas declaradas, límites incompletos del fixture actual y contrato de evidencia de integración insuficientemente observable. No hay objeción a usar `runClocks` crudo ni dependencia de PMU. Cuando el owner actualice SPEC/tests, revisaré los cambios antes de dar APRUEBA con SHA.

## Re-review SPEC/tests y hooks — `d19ab13` + `978db12`

- Revisión estática inicial de commits y test actual; el owner reportó host + ASan/UBSan PASS. En la re-verificación siguiente ejecuté el comando host solicitado y reproduje PASS. El build Vita informado por el owner compila; la validación dinámica de consola sigue pendiente, así que CA2 no queda aprobado.
- **CA1: APRUEBA para SPEC/tests en `d19ab13`.** Se incorporan generación, TID inicial ausente, errores en ambos extremos y estados/deltas inválidos; el test fuerza un lector listo y espera observación de cada escritor antes de los joins. La segunda fixture usa generación estable y delta válido para comprobar que `menu-production` y `race` permanecen declarados y no se promueven a escena verificada. También verifica `affinity_changed`/`priority_changed` y la ausencia de `cpu_us`, `%` y saturación.
- El diseño conserva los cinco roles acordados: guest caller, render, productor DSound, feeder sceAudioOut y decoder MCI. `978db12` registra los tres roles de audio; `d19ab13` registra guest/render e integra el collector opt-in y sus filas. No se exige PMU ni se infiere ocupación a partir de `runClocks`, afinidad o `lastCpu`.
- **CA2: contrato suficiente; no aprobado.** La SPEC exige cinco roles, filas aun cuando falten, rc/estado/extremos/timestamps/intervalos, cambios de máscara/prioridad y ausencia de filas con `HW_THREAD_CPU=0`. Hubo un build Vita PASS inicial para `build-vita-hw075/hotwheels-vita.vpk` (1.967.015 bytes), anterior a `7487572`; el rebuild final de los fixes está en curso. La validación dinámica de consola sigue pendiente según el brief. No se aprueba CA2 sólo por compilación. El código de integración incluido en `d19ab13` consulta `sample_threads()` sólo en el collector de métricas de modo Original y emite cinco filas por ventana cuando la opción está activa.
- **Aclaración para el dictamen de código de Sonnet:** los RAII de registro por rol y los hooks aparecen sin condición `HW_THREAD_CPU`; la opción desactiva snapshots/filas, pero siguen las escrituras atómicas de registro/retirada al iniciar/terminar esos hilos. Definir si eso cuenta como parte constante del sistema bajo prueba o si “opt-in” exige suprimir también esos stores; el A/B actual mide principalmente consultas y logging. No lo considero bloqueo de SPEC/test mientras CA2 se interprete literalmente como “no emite filas”, pero debe quedar explícito en el review de código y en la atribución del overhead.
- El protocolo queda limitado al overhead del diagnóstico: especifica tres pares A/B, con witness de carrera separado. CA4 continúa como fase física posterior y no es requisito de este primer corte.

**Dictamen:** APRUEBA CA1/spec-tests en `d19ab13`; CA2 continúa pendiente de build Vita final para `7487572` y evidencia dinámica de consola. Sin CAMBIOS pendientes a SPEC/test en esta revisión. La aprobación de implementación/código queda en el review de Sonnet.

## Reproducción host CA1 — 2026-10-22

Ejecuté el comando solicitado desde la raíz del repo:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Ihotwheels/src hotwheels/tests/test_thread_cpu.cpp -o hotwheels/out/par-luna-thread-cpu
hotwheels/out/par-luna-thread-cpu
```

Resultado: `PASS thread_cpu: raw deltas, invalid endpoints, generations, scene and concurrent publication` (exit 0). El binario no existía previamente y fue borrado después de pasar. El rojo original en `dffc20a` por header ausente queda como evidencia de TDD reportada; no lo reproduje porque el header ya está disponible. No ejecuté ASan/UBSan en esta vuelta.

## Seguimiento del review de código — 2026-10-22

El owner informa que Sonnet confirmó el diagnóstico y pidió dos ajustes: gate P2 y formato de tamaño P3. El rojo `8b01743` quedó corregido por `7487572`; `tests/run_thread_cpu.sh` pasa host + TSan y quedó visible en `run_all --list`. El formatter emite `size=unknown` en runtime y tiene test dedicado. Este reporte no ejecutó ese runner; mi reproducción independiente del comando host anterior permanece PASS. No cambia el contrato ni el dictamen de CA1.

El rebuild Vita final está en curso y reemplaza la evidencia de build anterior para el código corregido. Por eso, la compilación Vita final aún no está confirmada en esta revisión. CA2 sigue sin aprobar hasta recibir resultado/hash del VPK final y la evidencia dinámica de consola autorizada por el brief; no se cierra por compilación sola.

## Revisión propuesta: calibración aislada de `runClocks`

**Dictamen preliminar: CAMBIOS de precisión al protocolo antes de cerrar el addendum.** La comparación raw busy/sleep, serial por USER_0/1/2, sin PMU ni touch al VPK diagnóstico, es adecuada para una prueba de plausibilidad conductual. No calibra frecuencia ni unidad; debe titularse/descrbirse como contraste raw, conservar `runClocks` y wall deltas y evitar umbrales que declaren equivalencia con microsegundos/ciclos.

1. **Cerrar la carrera del barrier:** que el worker señale “fin” inmediatamente antes de `sceKernelWaitSema` no demuestra que ya esté WAITING cuando el observer despierte. Especificar cómo se garantiza/observa el estado bloqueado antes de cada `sceKernelGetThreadInfo`. Una opción de baja complejidad es ejecutar worker y observer en el mismo USER core durante la calibración, con worker de prioridad efectiva más alta; luego del signal el worker sigue hasta bloquearse y recién entonces corre el observer. Registrar las prioridades efectivas y validar `status`, `waitType` y `waitId` de la barrera. Si no se puede verificar WAITING, invalidar esa muestra. Repetir lo mismo en el inicio y fin de cada tramo.
2. **Afinidad/core target:** capturar rc de la solicitud de afinidad, máscara efectiva leída en ambos extremos, `currentCpuId` y `lastExecutedCpuId`. Sólo etiquetar el tramo como core aislado si la máscara efectiva permite únicamente el USER core solicitado; si el readback es cero/ambiguo o hay cambio, conservar la muestra raw y marcar `core_target_unverified`. `lastExecutedCpuId` en un extremo no prueba por sí solo dónde corrió todo el tramo.
3. **Duración y sobrecosto de barrera:** 200 ms es objetivo, no duración garantizada. Conservar wall timestamps propios del observer por muestra, delta real y orden de trial; no reemplazar el intervalo real por 200 ms ni atribuir la latencia de wakeup al worker. El delta `runClocks` abarca sólo entre snapshots e incluye trabajo de entrada/salida de la rutina en el worker; describirlo como delta acumulado de ese intervalo. Ejecutar un solo worker de carga a la vez; fijar y registrar frecuencia efectiva, prioridad y demás config iguales en los tres cores.
4. **Orden de trials:** correr tres busy y tres sleep por core en secuencia predeclarada y alternada/counterbalanced, registrar el orden y no mezclar muestras de cores. Usar la misma rutina busy y duración sleep solicitada para todas las repeticiones. Reportar los seis pares/muestras crudos por core; una tendencia busy > sleep puede describirse como observación, sin convertirla en validación de unidad.
5. **Fixture host `thread-clock`:** validar sólo integridad: rc exitoso, TID+generación positivos y estables, wall estrictamente creciente, contador no decreciente (delta cero permitido), y worker en estado/waitType/waitId esperado en ambos extremos. API error, identidad/generación distinta, reset, tiempo inválido o worker ejecutando invalidan el sample. Afinidad no verificable conserva raw pero elimina la etiqueta de core confirmado. No exigir que fixture host prediga magnitudes busy/sleep ni convierta unidades. Incluir inicio/final de cada uno de USER_0/1/2 y ausencia de conversión/porcentaje.

El acuerdo conserva fuera de alcance consola, VPK diagnóstico y PMU. La lectura de fuente candidata en `manifest.json` está marcada reconstructed/candidate, no como contenido probado de VPK; esta calibración debe versionarse y reportarse como experimento independiente, sin atribuirle resultado al binario previo. No edité SPEC-064, `samples/thread-clock` ni tests `clock_calibration`.

## Re-review calibración aislada — `627e703` + `cec2685` + `3730ee9`

**Dictamen: CAMBIOS puntuales de precisión sobre el reloj de control; los fixes de validez de target/config y sus fixtures quedan aprobados.** Revisión estática de SPEC-064, `measurement.h`, `test_clock_calibration.cpp` y `main.cpp`. El owner reporta build Vita aislado PASS; no corrí test ni usé consola.

- `3730ee9` corrige la aceptación de etiquetas basadas en snapshots fallidos: `target_verified()` y `config_stable()` exigen `rc == 0` en ambos extremos además de sus condiciones anteriores. Esto evita tratar campos residuales de `ThreadInfo` como evidencia cuando la llamada falla.
- El fixture posterior a `cec2685` cubre error en A y en B: en cada caso comprueba `integrity`, `target_verified` y `config_stable` falsos, con los demás campos poblados. El test también conserva la separación correcta: target requiere pin exitoso, máscara exclusiva y `last_cpu` coincidente; configuración requiere MHz positivo e igual y prioridad estable. Afinidad/último CPU son evidencia acotada del target, no utilización total; los `runClocks` permanecen crudos.
- La barrera cumple el protocolo acordado: el extremo es la misma consulta de `ThreadInfo` que observa WAITING, `waitId` del semáforo de comando y `waitType` no cero; el observer no señala el comando hasta terminar de registrar la muestra. Esto conserva bloqueado al worker durante el snapshot. Los trials son seriales por core y alternan busy/sleep.
- **CAMBIOS:** `main.cpp` usa `sceKernelGetProcessTimeWide()` para el deadline busy, los timestamps llamados `wall_us` y el timeout del observer. VitaSDK documenta esa función como “Get the process time of the current process”, mientras que documenta [`sceKernelGetSystemTimeWide()`](https://docs.vitasdk.org/group__SceThreadMgrUser.html) como “Get the system time (wide version)”. La documentación de [`sceKernelGetProcessTimeWide()`](https://docs.vitasdk.org/group__SceProcessmgrUser.html) no establece que sea tiempo de pared monotónico. Usar `sceKernelGetSystemTimeWide()` en los tres usos de tiempo de pared, o aportar evidencia reproducible de que `GetProcessTimeWide()` satisface aquí ese contrato y cambiar nombres/documentación si mide otra cosa. Esto no cuestiona ni convierte `runClocks`.
- SPEC-064 dice que se registran timestamps/intervalo real, pero debería exigir explícitamente que `rc` sea exitoso en ambos snapshots para `core_target_verified` y `config_stable`, aunque el helper ya lo garantiza. Así el contrato observable coincide con el fix y con los fixtures.

**Alcance de evidencia:** build aislado Vita PASS reportado por el owner para `627e703`; la revisión confirma estáticamente que `3730ee9` y su test corrigen la regresión de rc. No hay validación física CAL3 ni veredicto de unidad, y no se atribuye resultado al VPK del juego.

## Re-review resolución de reloj — `ee76534` + `1ef518c`

**Dictamen: APRUEBA CAL1 (SPEC/addendum + fixtures) en el estado actual.** La SPEC limita `sceKernelGetSystemTimeWide()` al addendum aislado para timestamps, deadline busy y timeout; explícitamente separa el reloj de control del `runClocks` crudo y exige `rc == 0` en ambos snapshots para `core_target_verified` y `config_stable`. `main.cpp` usa ese reloj en los tres usos, mientras que el código de medición runtime existente queda fuera del cambio. Las funciones helper siguen separando validez raw, confirmación de target y estabilidad de configuración; ningún resultado expresa unidad, utilización o saturación.

Los fixtures cubren API error en A y B para integridad/target/configuración, identidad y generación ausente/cambiada, tiempo no creciente, reset del contador, WAITING y la identidad de barrera, core fuera de rango, máscaras/CPU no confirmados, pin fallido, MHz no positivo/cambiante y prioridad cambiante. El refinamiento `448e89b` incluye cores reservados (3/4 y valor máximo) y estados inválidos adicionales.

Reproduje `hotwheels/tests/run_clock_calibration.sh`: **PASS normal y ASan/UBSan**, ambos reportan `PASS clock_calibration: raw integrity, barrier identity, target and config separated` (exit 0). El runner crea su directorio bajo `hotwheels/out` y lo elimina al pasar; no ejecutó consola ni build del juego. No añadí ni ejecuté otras pruebas.

CAL1 queda aprobada con evidencia host. CAL2 sigue pendiente del snapshot v4 prehash/build final que el owner prepara; el build aislado anterior de `627e703` continúa como evidencia sólo para ese corte anterior. CAL3 física sigue pendiente; `runClocks` conserva significado y unidad sin resolver.

## Cierre CAL2 preparado — snapshot v4

**Dictamen: APRUEBA CAL2 preparado.** Revisión sólo lectura de `out/hw075-clock-calibration/v4/`; no reconstruí ni modifiqué snapshot o artefactos.

- `prebuild-manifest.json` registra captura previa a compilación, fuente copiada y compilación desde el snapshot. Su SHA-256 calculado es `5cb9b7465b204d89be10815153fd47337769317e4d0c7d50ba4599dd348b79b0`, igual al `prebuild_manifest_sha256` de `manifest.json`.
- Volví a calcular SHA-256 de cada uno de los siete inputs listados (`main.cpp`, `measurement.h`, `Makefile`, `thread_cpu.h`, SPEC-064 y test/runner): los siete archivos bajo `v4/source/` coinciden con el premanifest. El repo HEAD registrado es `1ef518c`, que incluye los fixes declarados `3730ee9`, `6f25249`, `448e89b` y `1ef518c`.
- `build.log` muestra compilación VitaSDK del Makefile dentro de `v4/source/.../thread-clock`, flags `-O2 -Wall -Wextra -Werror` y link de los stubs Vita; no referencia el target del juego. El manifest registra GCC Vita 15.2.0 y declara explícitamente “VitaSDK build only; unit unverified; not run on console/Vita3K”.
- Artefacto verificado en disco: `build/thread-clock.vpk`, 56.125 bytes, SHA-256 `e8f9b3bd4ee2b04a7276b2fa248f04242042c3e4113c08948fa25647ca7f5064`, coincidente con `manifest.json` y con el valor reportado por el orquestador. `source_hashes_verified_unchanged` es verdadero y también coincide con la verificación independiente del snapshot.

Esto cierra CAL2 como build aislado preparado, sin instalación ni ejecución de HWCLOCK01. CAL3 física permanece pendiente; no hay evidencia de consola ni veredicto de unidad `runClocks`.
