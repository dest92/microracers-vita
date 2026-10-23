# SPEC-063 — Medir costo del hilo guest y diseñar offload desde core 0

- Milestone: M8, diagnóstico previo a offload
- Estado: diseño preparado; requiere instrumentación de ventanas antes del VPK de carrera
- Evidencia de entrada: consola, reporte del usuario del 2026-10-22

## Problema

La hipótesis por evaluar es que la ejecución serial del guest limita el rendimiento y que repartir trabajo entre los tres cores de usuario podría ayudar. Los logs revisados no identifican una ventana de carrera de forma suficiente: run4 y la captura nueva sólo muestran `CreateDevice` 640×480, y las etiquetas/ventanas de pipeline no prueban que se ejecutara `state_raceLoop`. El `CreateDevice` 800×600 de run3 tampoco enlaza por sí solo sus ventanas de métricas con una carrera. Además, las referencias de build proceden del handoff, no de una identidad registrada dentro de esos logs.

Por tanto, 17,6 guest Hz de run4 es una estadística de ventanas pipeline del log recibido, no una base verificada de carrera. Los 20 QPC calls/s de la captura nueva son una tasa de llamadas observada, no ocupación de CPU ni evidencia de saturación de core 0. No hay aún medición por hilo que permita atribuir tiempo de CPU a guest, render o audio, ni lectura de afinidad efectiva correlacionada con las ventanas. La saturación de core 0 y la ganancia potencial de multicore siguen siendo hipótesis.

## Contrato de medición

Una ventana válida para la primera medición de HW-075 debe:

1. Delimitar una ventana monotónica de duración registrada (objetivo 10 s), con TID y nombre de guest/render/audio; snapshot de `sceKernelGetThreadInfo` al comienzo y al final con `runClocks`, `currentCpuAffinityMask`, `currentCpuId`, `lastExecutedCpuId`, prioridad y estado. Calcular deltas de los contadores acumulados por TID. No muestrear por llamada ni escribir logs en los hilos medidos.
2. Registrar frames guest, build ID/hash del eboot obtenido de la misma ejecución y clocks efectivos. No inferir identidad de build desde un handoff externo.
3. El primer corte no implementa detector de carrera: publicar `scene=unverified` por defecto y conservar cualquier etiqueta de entorno como `scene_declared` sin tratarla como evidencia. Una futura clasificación de carrera requiere PC/EIP guest verificado contra build y witness de gameplay/D3D correlacionado dentro de la misma ventana. Resolución o etiqueta de entorno, aislados, no bastan.
4. Registrar afinidad efectiva como máscara leída de vuelta para cada TID en ambos extremos. `lastExecutedCpuId` es sólo el último CPU observado, no distribución de ejecución por CPU; la máscara es elegibilidad, no prueba de dónde corrió el hilo. `runClocks` mide ejecución acumulada por hilo, no tiempo bloqueado ni ocupación total del core.
5. Marcar ventana incompleta ante TID ausente/cambiado, error de API, reset/decremento del contador, timestamp inválido o build no identificada. No normalizar faltantes a cero. Mantener métricas de FPS/espera/replay separadas de CPU ejecutada. No publicar `cpu_us` ni inferir saturación en este corte.

La lectura inicial usa `SceKernelThreadInfo.runClocks` (64-bit `SceKernelSysClock` en el VitaSDK revisado) con `info.size=sizeof(info)`. Registrar el TID estable antes de entrar en `ns->run()`; ese llamado ejecuta el guest principal en el caller. Tomar snapshots fuera de hooks por frame y reportar deltas crudos. Rechazar errores de API, TID cambiado, reset/decremento de contador y tiempo inválido. Es una API de snapshot por ventana, no un perfilador por llamada. No convertir a `CPU_us` ni afirmar saturación en el primer corte.

## Hechos de implementación existentes (evidencia estática)

- `HW_GUEST_PROFILE=1` habilita muestreo EIP a 1 ms. `HW_GUEST_PROFILE_START_MS` y `HW_GUEST_PROFILE_DURATION_MS` fijan un intervalo desde el origen del sampler, pero no permiten iniciar al entrar a carrera. El resumen se escribe al salir de `ns->run()` y separa `guest`, `shim`, `idle` y funciones guest.
- `HW_PMU=1` habilita PMU por hilo guest y los acumuladores del scheduler. Su línea final contiene `cpu_run_us`, contadores A9, dispatch muestreado 1/64, traps y `gil_wait_us`. La línea se emite al terminar el guest thread.
- Al cerrar la ejecución, `natprof_window` devuelve tiempos por shim. El formato está oculto detrás del `Bridge` integrado en el checkout actual, y la salida no incluye límites de ventana. Los tiempos de shim pueden ser inclusivos; sumarlos como clases disjuntas duplicaría tiempo anidado.
- `fps-window` ya informa frames/HZ por ventanas de 10 s, pero no etiqueta cada ventana con contadores de CPU por categoría.
- `init.sh` encuentra VitaSDK y Vita3K; faltan ARM Linux GCC/G++ y qemu-arm. La consola ya está marcada `CONSOLE_BUSY`; M8 brief-comun además reserva consola para Fase 2 con el usuario presente.

## Diseño propuesto para una fase posterior de telemetría por categoría

Instrumentar en el runner del guest un reloj monotónico de bajo costo y un clasificador de fase/category con pila por hilo. Al entrar en un shim, contabilizar el delta al bucket actual y abrir una región; al salir, cerrar la región y restaurar el padre. La contabilidad exclusiva evita doble conteo de callbacks anidados. Para sleeps/bloqueos, separar CPU ejecutada de tiempo bloqueado; el timer PMU mide CPU sólo mientras el hilo corre y no debe acreditarse a la categoría que conserva el GIL mientras otro hilo corre.

La ventana se abre/cierra con eventos de fase emitidos por el runtime al confirmar que D3D está presentando frames de carrera, no por un offset fijo desde boot. Un buffer fijo por hilo acumula ventanas; el hilo guest es único escritor. El recolector solo toma snapshots después de una publicación release/acquire y el escritor retoma el buffer libre tras el acuse. En ARMv7, publicar índices y datos exige `dmb ish` o atomics C++ con release/acquire; la coherencia de caché no sustituye la barrera. No se escribe a memory card desde el hilo medido: el logger de baja prioridad escribe lotes cerrados después de la ventana.

## Diseño de offload (no implementar en esta feature)

- **Core 0:** conservar ejecución x86 traducida, trampas con efectos inmediatos sobre estado guest y operaciones síncronas que el guest observa antes de retornar. Reducir trabajo con batching/copias diferidas solo si el contrato COM y el orden visible se preservan.
- **Core 1:** ya posee el consumer D3D/vitaGL. Recibir snapshots inmutables del ring mediante cola acotada SPSC. El productor guest no toca un slot hasta que el consumer lo devuelve; el consumer conserva propiedad hasta completar replay/upload y no retiene el GIL durante trabajo GPU. La cola llena aplica política explícita de descarte/espera sin descartar actualizaciones de recursos necesarias para draws posteriores.
- **Core 2:** audio ya corre allí. DSound/MCI solo se offloadean si el bloque de PCM o comando tiene dueño único, secuencia y timestamp monotónicos, y capacidad acotada; no compartir buffers guest mutables. No agregar otro consumidor al core 2 sin medir su presupuesto y prioridad.
- **Planificación:** prioridad del productor guest por encima de consumidores que esperan sus publicaciones; consumer y logger a prioridad inferior para evitar inversión. Sin busy-wait: sleeps o semáforos con tiempo límite. Las colas deben ser acotadas para que una GPU/audio lenta no convierta memoria en buffer infinito ni bloquee indefinidamente al guest.
- **Concurrencia:** verificar las cuatro condiciones de Coffman al diseñar cada espera; evitar ciclo guest→GIL→consumer→guest. El guest conserva semántica x86 TSO: publicaciones a colas usan sincronización explícita que también sea correcta bajo el modelo ARM débil. Barreras de publicación no resuelven la coherencia I/D de código generado; esta feature no modifica el dynarec.

## Estimación de Amdahl (inferencia, no medición)

Escenario matemático, no pronóstico: si una base verificada de guest fuera 17,6 Hz (56,8 ms/frame) y se eliminara una fracción `f` del camino serial con solapamiento perfecto, el techo sería `17,6/(1-f)` Hz. Eso daría 19,6/22,0/25,1 Hz para `f=10/20/30%`; alcanzar 25 Hz requeriría 29,6% y 40 Hz requeriría 56%. Los 17,6 Hz actuales vienen de ventanas no identificadas como carrera; no sirven como base validada ni implican saturación de core 0. No se afirma una ganancia esperada.

## Criterios de aceptación

- [ ] CA1: el agregador host lee fixtures de ventanas categorizadas, mantiene unidades y denominadores separados, detecta categorías faltantes/duplicadas, y falla con muestras inconsistentes o ventana inválida.
- [ ] CA2: el build de medición opt-in reporta una fila por ventana de 10 s detectada como carrera, con al menos 95% de `cpu_run_us` atribuido o explícitamente `unclassified`; no altera el build normal.
- [ ] CA3: en consola, capturar al menos tres ventanas comparables de la misma build/configuración y resumir medianas de porcentaje por categoría y guest Hz. Registrar overhead comparando FPS de diagnóstico frente a build sin profiler.
- [ ] CA4: decidir offload con las categorías medidas; validar una cola a la vez y comparar FPS y latencia antes/después. No declarar `done` sin review de semántica x86, propiedad de buffers, orden ARMv7 y medición.

## No objetivos

Implementar offload, cambiar prioridades/afinidad de producción, sumar tiempos inclusivos como CPU exclusiva, o extrapolar fps de Vita3K/qemu a consola.
