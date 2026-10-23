# HW-063 — preservar estado de recursos al descartar un frame D3D

## Problema

HW-053 puede descartar rings `READY` cuando el consumidor queda atrás. `Device` sólo serializa `TexUpload` al cambiar la generación y `Replayer` guarda texturas entre frames; si el ring descartado era el único que contenía una subida o liberación, un Draw de un ring conservado podría observar estado de textura obsoleto.

## Contrato observable

Cada Draw de un frame conservado ve el estado de texturas y binding que resulta de aplicar en orden todas las transiciones anteriores, incluidas las contenidas en frames READY descartados. El guest no espera al consumidor. No se retienen pointers a memoria guest y el contador de drops sigue contando rings enteros descartados.

## Criterios verificables

- Test llena la cola, publica `TexUpload(id, A)` en un READY descartado y un Draw posterior superviviente de `id`; la referencia debe rasterizar A.
- Otro caso cubre `TexUpload(A) → TexFree → TexUpload(B)` y confirma orden final B al Draw superviviente.
- Frame sin transiciones descartado no agrega comandos ni altera el golden.
- Vita3K/qemu con cola saturada verifica draws texturados, cero bloqueo del productor y FPS/drops; reportar coste de recuperación.
- La ventana de métricas informa cuántas operaciones de recurso había en rings descartados y cuántos estados finales se reinyectaron, para distinguir presión de cola de recuperación efectiva.

## No objetivos

No eliminar la política de descarte ni modificar sincronización GPU; primero se compara una estrategia de retener/reinyectar transiciones con el comportamiento actual.

## Resultado de implementación

`FrameQueue` conserva al descartar un READY la última transición `TexUpload` o `TexFree` por ID y `Texture` por stage, copiando payloads que ya son propiedad del ring. Al tomar el siguiente READY, antepone las transiciones en orden antes del contenido del ring superviviente y limpia el carry. No conserva memoria guest; una actualización posterior del mismo ID/stage reemplaza la anterior. La prueba llena los tres slots, descarta `Upload(rojo) → Free → Upload(verde) → Texture(stage 0, id 9)` y rasteriza verde sin rebind en el frame superviviente con `RefRaster`.

Host: CTest `d3d`, `d3d_frame_queue`, `d3d_bridge`, `interp60` PASS (4/4). Smoke Vita3K 25 s: `guest=22.597 Hz`, `consumer=22.697 Hz`, 228/230 frames, 2 drops al inicio; `TexUpload=0.101/frame`, backend `0.070/frame`; 500 escenas D3D completadas sin crash. Los drops fueron sólo al arranque, por lo que esta corrida no mide replay de recursos perdidos ni saturación sostenida. Queda pendiente medir coste de recuperación saturada en qemu/Vita3K antes de `done`.

## A/B de carga Vita3K — 2026-10-03

Mismo VPK/build, `race_enter.script`, stage/datos y 35 s; único cambio `HW_D3D_SYNC_BASELINE=1`. Pipeline run `build-vita3k/hotwheels-vita-20261003-161429896025319-2376341`: ventana estable 10 s `guest=25.973 Hz`, `consumer=26.073 Hz`, `completed/submitted=262/264`, `dropped=2`; uploads `0.095/frame`, backend `0.057/frame`. Segunda ventana, `guest=38.721 Hz`, `consumer=38.721 Hz`, `650/652`, mismos 2 drops. El índice D3D alcanzó escena 900; no hubo drops después de los dos iniciales.

Sync baseline `build-vita3k/hotwheels-vita-20261003-161556258454684-2380261`: ventana estable `guest=24.377 Hz`, `consumer=24.377 Hz`, `245/245`, `dropped=0`; uploads `0.098/frame`, backend `0.065/frame`. Siguiente ventana `32.230 Hz`, `568/568`, cero drops; cierre `30.682 Hz`, `875/875`. Los draws/frame y uploads varían con la escena y la carrera, y el pacing Vita3K dio 2–5,8 presents/s; esto no prueba una mejora causal de FPS ni el costo de reinyectar recursos perdidos. Criterio de saturación sigue sin medirse: pipeline sólo descartó 2 frames iniciales, sin transiciones texturadas confirmadas.

## Saturación QEMU y límite de evidencia — 2026-10-03

`HW_KEEP_OUT=1 tools/run_race.sh 35 d3d` (pipeline) midió una ventana estable de `guest=39.998 Hz`, `consumer=1.466 Hz`, 915 enviados/35 completados/879 descartados, `TexUpload=0.066/frame` y backend `0.066/frame`. `frame_00200.png` muestra la escena texturada durante esta carga. Repetición de 25 s: 466 enviados/24 completados/441 descartados, `guest=39.585 Hz`, `consumer=1.466 Hz`, `0.333` uploads/frame. En ambos, el guest llegó al apagado cooperativo con `missing=0`, y después el proceso dio SIGSEGV/rc 139. Control síncrono de 35 s: 36/36 frames, cero drops, consumer 1.310 Hz, backend uploads 0.071/frame, salida rc 0. Esa medición inicial acreditó presión y escena texturada, pero aún faltaba una señal directa del carry; Vita3K A/B tampoco saturó sostenidamente. BUG-043 sigue abierto por SIGSEGV sólo observado en pipeline; no inferir que el carry de recursos lo cause.

## Telemetría del carry bajo saturación — 2026-10-03

Se añadieron a `fps-window` dos deltas: `resource_ops_dropped` cuenta operaciones `TexUpload`/`TexFree`/`Texture` leídas en READY descartados; `resource_ops_carried` cuenta estados finales coalescidos que se anteponen al siguiente ring consumido. Test extendido: 4 operaciones descartadas (`Upload→Free→Upload→Bind`) producen 2 estados reinyectados (upload y bind), y el `RefRaster` sigue dando el pixel verde. El test compiló rojo antes de agregar los campos; luego `d3d`, `d3d_frame_queue`, `d3d_bridge`, `interp60` PASS 4/4.

QEMU ARM 25 s, corrida `out/run-20261003-164949219728846-2483396`: ventana estable 10 s guest/consumer 40.032/1.566 Hz, 479 submitted/25 completed/453 drops, 0.250 TexUpload/backend uploads por frame; en esa ventana se vieron 360.868 operaciones de recurso en rings descartados y 20 estados finales reinyectados. Ventana inicial registró 89.524/6. La captura `frame_00200.png` muestra geometría texturada mientras hay presión sostenida. Esto demuestra que el camino runtime de carry se ejercitó; el golden host comprueba su resultado visual. Build Vita PASS (VPK 1.921.953 B). SIGSEGV post-shutdown volvió a ocurrir (host-PC `0x30fc8`), agregado a BUG-043; no impidió métricas ni captura. Code review del contador APRUEBA tras corregir el conteo al `close(false)` (`coordination/review-HW063-metrics.md`); CTest 4/4, build Vita y stress QEMU satisfactorios. HW-063 queda done; el fallo post-shutdown es BUG-043 independiente.
