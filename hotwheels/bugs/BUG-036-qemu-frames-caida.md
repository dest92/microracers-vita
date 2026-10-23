# BUG-036 — caída de frames en smoke qemu (20 s): 299→~130 desde 9cc0a13/be6f2ee

Nivel: **medido en qemu** (qa, 2026-10-03, HEAD be6f2ee/4f94565). Dueño: **integ** (con luna si es pacing interp60). Severidad: menor/vigilancia. Estado: **cerrado como métrica no invariante** (2026-10-03).

## Evidencia (frames en ventana fija de 20 s, `out/run-*/frame_*.png`)
- 2103ae3: 299 · d4bfff3: 298 · b5da935: 269 · 3b7e909: 259 · fbda786: 190 · f989bb1: 239 · **be6f2ee: 127 y 136** (2 corridas).
- Ruido de carga explicaba 219↔299, pero 127/136 repetido es tendencia nueva. Cambios en el rango: interp60 (ventanas móviles, 9cc0a13), runtime_vita, soak/vita_deploy.
- Aparte: la 1ª corrida de smoke dio build_vita rc=255 con log vacío (`out/smoke-20261003-063250.d`), build manual y 2ª corrida OK: colisión transitoria con otro build concurrente (2ª vez; ya ocurrió en 7db43ab).

## Acción
Confirmar si es pacing/presenter (menos frames por 20 s a propósito) o regresión de rendimiento del host; si es intencional, ajustar expectativa y registrar en decisiones.md.

## Análisis (qa, rol técnico SO/bajo nivel; nivel: host, medido + razonamiento)
- **Planificación**: el smoke fija una ventana de *tiempo de pared* (20 s) y cuenta frames; qemu-user es CPU-bound (dynarec + software render), así que frames ≈ CPU-seg obtenidos × frames/CPU-seg. Con CFS el share depende de la carga concurrente. Medido ahora: `nproc=12`, `loadavg=8.14 5.14 5.14`, Vita3K de otro agente a 547 % CPU. Las corridas de 127/136 frames coinciden con picos de carga (varios agentes corriendo smoke/Vita3K/builds a la vez); 211–299 con carga baja. Hipótesis refutable: con `loadavg<4` el smoke vuelve a ≥250 frames; con carga artificial (`stress`/Vita3K en paralelo) cae a ~130 sin cambiar el commit.
- **Métrica mal elegida**: frames/20 s de pared no es invariante a la contención. Propuesta: que smoke registre `loadavg` y tiempo de CPU de usuario del proceso (`/usr/bin/time -v`) y reporte frames/CPU-seg; el gate de regresión usar eso (o `nice`/`taskset` reservado), no el conteo crudo.
- **Exclusión mutua ausente**: `tools/build_vita.sh` escribe en `build-vita/` sin `flock`. Dos builds concurrentes (otro agente + smoke etapa 2) = condición de carrera sobre el mismo directorio → rc=255 con log vacío (visto en 7db43ab y be6f2ee; el build manual aislado siempre OK). Corrección: `flock hotwheels/build-vita.lock` alrededor del build (productor único por directorio), mismo patrón para run_host/Vita3K.
- Mientras tanto, interpretar frames del smoke como indicativo, no como gate; el veredicto OK/MISSING/fin limpio sí es determinista.

## Refutación/confirmación (qa, 07:03, HEAD 4f94565, loadavg 0.49 al iniciar): smoke qemu = **299 frames**, MISSING 0. Con carga baja el conteo vuelve al máximo previo → soporta la hipótesis de contención (no regresión de código). Recomendación: cerrar como "métrica no invariante" cuando integ añada frames/CPU-seg o nice/flock.
- Nota: esa misma corrida dio smoke FAIL en etapa tests (run_io/run_shims: `-Werror=reorder` en src/io/read_stream.cpp, edición en curso de otro agente sobre el working tree compartido); un run_all inmediato posterior dio PASS=20/0/1. Es carrera entre agentes sobre el árbol de trabajo, no bug de commit.

## Cierre — integ (2026-10-03)
La comparación 127/136 frames (con contención concurrente documentada, incluyendo Vita3K a 547 % CPU) frente a 299 con `loadavg=0.49` y fin limpio refuta una caída estable atribuible al rango de commits. Esta prueba mide trabajo completado por 20 s de pared; qemu-user comparte CPU bajo CFS, por lo que la tasa cae al perder cuota de CPU. No se cambia pacing ni se codifica un gate de FPS con esta medición. Un eventual benchmark de regresión debe reportar CPU-seg del proceso o ejecutarse aislado y registrar carga. El error `dsound: falta wav_path` de otra repetición impidió arrancar el guest y no es evidencia de BUG-036.
