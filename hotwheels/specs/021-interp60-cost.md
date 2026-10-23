# HW-035 — costo host del presenter interp60

## Problema

`Interp60::capture` copia las palabras completas del ring, clona el mapa de generaciones y crea snapshots/metadatos con vectores anidados. `build_interpolated` crea un payload temporal por draw coincidente antes de copiarlo al ring de salida. La correspondencia actual es ordinal y lineal; esta tarea debe medirla y preservar esa complejidad, no reemplazarla por búsquedas globales.

## Contrato observable

- La salida, los conteos `interpolated/fallback`, el orden painter, el gate 60 Hz y los fallbacks son idénticos para los mismos snapshots/timestamps.
- Tras warm-up con capacidad suficiente, `capture` y `tick` no hacen asignaciones dinámicas por frame. Un crecimiento excepcional puede reservar capacidad; el benchmark reporta ese caso separado.
- El perfil host informa ciclos/tiempo de captura y presentación, bytes copiados por ruta, asignaciones dinámicas y escala al duplicar draws/vértices; separa medición de matching de la copia obligatoria del ring de entrada/salida.
- Usar una escena sintética reproducible con distribución de carrera basada en FINDINGS #18 (≈765 draws/escena p50 747, 3–4 vértices). Agregar al menos un caso de estrés mayor para detectar crecimiento superlineal.
- Estimación de Vita A9 explícita y conservadora; 2 ms a 444 MHz equivalen a 888.000 ciclos. Los ciclos x86 host no se presentan como ciclos A9: documentar fórmula, operaciones/bytes considerados y supuestos; HW-033 contrastará en consola cuando integ tenga ventana, sin hacer depender esta tarea de acceso a consola.

## Criterios de aceptación

- [x] CA1: `tests/bench_interp60.cpp` mide baseline compatible/fast apareados; escenas deterministas 765 draws y estrés 383/1530/3060, TSC serializado + CPU ns, allocs, bytes, percentiles y equivalencia de salida. `perf stat` no está permitido en host (`perf_event_paranoid=4`), por eso no se atribuyen TSC host a A9.
- [x] CA2: pool fijo de 4 snapshots y buffers reservados; ring cerrado compartido elimina copia de entrada y ordinales de HUD; append+patch en ring final elimina payload temporal. Captura/tick fast midieron 0 allocs/frame tras warm-up; no se muta la salida salvo XY.
- [x] CA3: ordinal único y pasada O(D+V+W); generaciones usan hash abierto fijo (768 IDs históricos máx., 384 IDs tocados/frame), staging transaccional por epoch y overflow que rechaza el snapshot sin alias ni mutación parcial. Cotas de probing: 1024/512 buckets. Benchmark incluye 32/128/384 TexUpload únicos/frame: p50 fast capture 24,4/25,4/37,1 us; p95 47,6/44,6/54,1 us con variación del host. La escala Draw 383/765/1530/3060 midió p95 ~21,9/48,4/93,9/185,1 us en corrida previa.
- [x] CA4: `bash hotwheels/tests/run_interp60.sh` pasó; agrega golden bit-exacto XY (`0x42fecf77`) respecto de la aritmética double anterior, overflow de 385 updates únicos/frame y 769 IDs históricos sin alias; overflow conserva la generación previa. También equivalencia de salida para ownership compartido.
- [x] CA5: estimación explícita: para escena de 765 draws, el p95 alto post-fix fue 50.123 us CPU host/tick; aplicando factor arquitectónico/memoria conservador 20x y 444 MHz: 50.123 us × 20 × 444 MHz ≈ 445.093 ciclos, 1,003 ms. Es modelo, no medida de A9; queda margen ≈1,99x frente a 888.000 ciclos y HW-033 debe contrastar en consola.

## Resultado / límites

La salida paired del último run base (`bench_interp60.sh 765 800`) dio fast capture/tick 0 allocs, input copy 0 B/frame y patch-temp 0 B/frame; output copy obligatoria 78.088 B/frame. TSC fast tick p95=119.504 ticks y CPU-thread p95=46.343 ns. El baseline pre-cambio (400 iteraciones, host no apareado) tuvo 1 alloc/tick y ~65.808 B de payload temporal más copia de input de 78.088 B/frame; comparaciones de tiempo entre esas corridas no son controladas.

Review `381277b` encontró dos regresiones y ambas se corrigieron: sustitución de vector ordenado por tablas hash abiertas acotadas, con staging transaccional y límites verificables; y restauración de la expresión doble previa al redondeo float final, fijada por golden. Corridas churn con 765 draws/100 frames y 32/128/384 uploads/frame mantuvieron 0 allocs fast; captura p50 fue 24,4/25,4/37,1 us (ruido p95 alto en las dos primeras por carga). El test host verifica rechazo de overflow y que no altera la generación comprometida.

La muestra host fluctuó con carga (la corrida pre-review registró hasta 48.370 us p95 fast tick; tras restaurar double la corrida paired p95 dio 50.123 us). Para estimar A9 usé el mayor p95: 50.123 us × 20 × 444 MHz ≈ 445.093 ciclos (1,003 ms). Sigue bajo 888.000 ciclos, pero factor 20x es hipótesis conservadora aún no calibrada. La escena 4x midió hasta 185.064 us p95 host antes del fix; no se declara que el estrés 4x satisfaga el mismo presupuesto A9.

## Diseño y lentes de bajo nivel

- Mantener productor/consumer: el guest cierra snapshots y el presenter conserva ownership hasta terminar `tick`; un pool no puede reciclar un snapshot mientras un reader lo sostiene. Elegir pool acotado y política explícita si todos sus slots están en uso; no bloquear el guest bajo el lock del backend/GXM.
- La generación de texturas invalida en orden upload/free mediante tablas de hash abierto acotadas. Límite: 768 IDs únicos históricos y 384 IDs tocados en un frame; el overflow rechaza el snapshot sin reemplazarlo ni actualizar generaciones. No se reciclan IDs ni se aliasan; reset explícito vacía el historial.
- Reutilizar capacidad de ring/scratch; no conservar punteros a memoria del producer después de `Present`. Hacer que el trabajo sobre draws, vertices y palabras sea O(D+V+W), donde W es el tamaño de los rings, con una sola pasada de matching por ordinal.
- El target Vita es Cortex-A9 ARMv7 a 444 MHz: el límite de 888.000 ciclos no se deduce de clocks x86. Para una estimación estática revisar ensamblador ARM de la ruta hot, memoria L1/L2, lineas de 32 B, presión de heap y coste de memcpy; pedir validación PMU a HW-033 en consola después de integrar.

## No-objetivos

- Cambiar semántica gráfica, clasificación de HUD, pacing, backend vitaGL o política de CPU/afinidad.
- Atribuir al matching tiempos totales de Vita3K, qemu o x86.
- Ejecutar consola/deploy; la valida integ bajo HW-033.
