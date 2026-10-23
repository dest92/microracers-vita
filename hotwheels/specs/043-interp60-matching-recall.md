# HW-066 — recuperar matches de interp60 sin alterar HUD

## Problema y señal medida

En el run Vita3K `hotwheels/build-vita3k/hotwheels-vita-20261003-170720399876513-2562932/vita3k.log`, a 394 rings capturados y 46 ticks, se informaron 3.914 draws interpolados frente a 39.409 fallback (9,03 % de interpolación acumulada; esos contadores cuentan draws presentados en cada tick, no primitivas únicas). El sample ya está en carrera con `draws=756..963` en checkpoints de 100 escenas.

El matcher vigente descarta el frame completo si cambia el total de draws, y luego sólo busca misma ordinal con `(world, viewport, textura+generación, primitiva, vertex_count)` idénticos. También rechaza desplazamientos por vértice mayores que el viewport. Por lo tanto una inserción/desaparición desplaza todas las ordinales posteriores y el mismatch global anula matches independientes. El log actual sólo permite cuantificar este resultado total, no atribuir los fallos por causa; HW-066 agrega contadores para medir frame count, identidad en ordinal, rematches, ventana, distancia y tolerancia en la siguiente corrida.

## Resultado de implementación y carrera — 2026-10-04

- Matcher anterior: 3.914 interpolados / 43.323 draws presentados = 9,03 % en el log Vita3K `20261003-170720`.
- Matcher nuevo: 76.685 / 96.239 = 79,68 % en `build-vita3k/hotwheels-vita-20261003-173308622004303-2668929/vita3k.log` (VPK rebuilt; harness 55 s, 1.294 snapshots, 129 ticks). Cumple la meta de ≥70 % para esta carrera. Es comparación observacional n=1 por versión, no A/B intercalado.
- Ventana diagnóstica cercana al cierre: delta de draws 150; mismatch ordinal textura 1.676/topología 692; 13.279 rematches; 668 sin candidato; 1.287 rechazo espacial; 16 por desplazamiento. Estos contadores son tick-weighted por ventana, no draws únicos.
- El gate de pacing de esa corrida dio `FAIL_RATE`: `presents_10s=25`, `avg_hz_milli=2500`, `min_1s_hz_milli=2000`; por tanto el porcentaje de correspondencias sí mejora, pero esta corrida no demuestra ritmo sostenido de 60 Hz. La escena mostró entre 557 y 1.274 draws en checkpoints.
- `run_interp60.sh` PASS, incluidos tests sintéticos 4/5 (80%) al insertar y 3/3 (100%) al borrar. HUD no clasificado mantiene el payload actual. Benchmark host 765 draws: `fast_tick_thread_cpu_ns` p95 55.539 µs y cero allocs/frame; no extrapolar a A9.
- `build_vita.sh` produjo VPK 1.932.640 B. Reviewer APRUEBA estáticamente `2d526ca`, cerrando el P2 de `35c89f2`; `run_interp60.sh` y el gate de matching en carrera pasan. HW-066 queda cerrada. El `FAIL_RATE` de presentaciones (2,5/s en ventana final) se conserva como limitación de pacing separada, no como éxito de 60 Hz.

## Contrato

- Mantener interpolación de sólo vértices XY y conservar Z/W/UV/color y comandos del frame actual. HUD/menu no clasificado sigue repitiéndose sin alteración. En la integración actual, D3D es escena 3D y HUD/menu pasan por DDraw; `capture()` genérico sigue exigiendo ordinals positivos de mundo.
- Quitar la igualdad global de cantidad de draws como precondición. Emparejar 1:1 y en orden de pintado por firma exacta `(mundo, textura, generación, viewport, primitiva, vertex_count)` dentro de una ventana simétrica fija de ±8 ordinals.
- Entre candidatos de la firma, elegir costo mínimo de distancia entre centroides; ante empate, preferir menor delta ordinal. Exigir distancia de centro ≤ diagonal del viewport y mantener el límite existente de delta por vértice. Un candidate usado no puede volver a emparejarse, y los índices del frame previo elegidos deben ser estrictamente crecientes: no cruzar painter order. Si no hay candidato seguro, dejar el draw actual intacto.
- La búsqueda es O(draws × ventana), sin asignación por tick; se precomputan centroides al decodificar el snapshot. Se mantiene una sola hebra consumidora y los snapshots inmutables; el productor sólo publica rings cerrados bajo la sincronización existente.
- `InterpStats` expone contadores de diagnóstico por tick para cambio en draw count, mismatch ordinal por textura/topología/viewport, rematches, falta de candidato en ventana, rechazo espacial y rechazo por tolerancia de vértices. Son métricas del par actual repetidas por tick, no conteo de frames guest únicos.

## Aceptación verificable

1. Test-first: insertion/deletion antes de draws estables produce ≥70 % matches; misma firma repetida elige la instancia espacial correcta; reordenamiento no permite cruce ni altera orden del output; textura generation/topología/viewport incompatible cae a fallback.
2. Un draw no clasificado como mundo mantiene bytes XY exactos. En la integración de Vita3K, HUD sigue por DDraw sin cambios.
3. Tests actuales `interp60` preservan golden y regresiones. Reportar antes/después `interpolated/(interpolated+fallback)` y breakdown de rechazo; objetivo ≥70 % en la misma escena de carrera. Si no se dispone de carrera reproducible, informar sólo resultado sintético y no declarar alcanzado el objetivo de producto.
4. Complejidad O(n·17), temporalidad por valores host monotónicos y sin allocs por tick; benchmark existente compara p50/p95. No extrapolar Vita3K a consola/A9.

## Razonamiento de bajo nivel

El hilo presentador serial procesa snapshots host inmutables; el mutex de `Interp60` publica/acquire el par en ARMv7, así que no se agregan barreras manuales ni se comparte memoria guest. El radio ordinal constante acota instrucciones/caché a diecisiete candidatos por draw, en lugar de Hungarian global O(n³) o matching all-pairs O(n²); centroides contiguos en `DrawMeta` evitan releer todos los vértices para cada candidato. Monotonicidad de índices mantiene painter order. Texture ID+generation y clasificación `world` son límites de identidad/UI, no campos que se relajan para subir la métrica.

## No objetivos

No cambiar física, paso guest, HUD/UI, textura usada para replay, pipeline producer-consumer, estrategia de present ni el límite de movimiento por vértice. No declarar equivalencia visual completa sólo por subir el porcentaje de matches.
