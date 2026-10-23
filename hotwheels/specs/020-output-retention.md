# SPEC-020 — Salidas acotadas de harness

## Problema y contrato

Las corridas largas de Hot Wheels llenaban el disco con PNG, WAV, core dumps y directorios de ejecución. Los runners deben desactivar core dumps antes de qemu; retener por defecto sólo cada 50.º frame y los últimos 20; emitir WAV sólo con `HW_WAV=1`; conservar artefactos si falla la corrida y borrar el directorio propio si pasa. `HW_FRAMES_ALL=1` conserva todos los frames para diagnóstico explícito.

El presenter host DDraw y el rasterizador D3D comparten una política serializada: el frame completo más nuevo ocupa cada uno de los 20 slots recientes, y los samples periódicos siguen disponibles. El WAV ausente usa un sink descartable, sin abrir archivos ni alterar el sink de Vita. El runner smoke conserva las salidas de etapas hasta escribir el resumen; en PASS borra sus directorios y deja el Markdown.

## Aceptación verificable

- Una secuencia de 101 frames produce samples 50/100 y los últimos 20 (22 PNG); `HW_FRAMES_ALL=1` produce todos.
- La prueba de audio sin ruta WAV no crea WAV; el modo con ruta conserva su cobertura existente.
- `run_host.sh`, `run_race.sh`, `vita3k_run.sh`, `smoke.sh` y `soak.sh` ejecutan con core dumps desactivados y llaman a la poda; PASS limpia su salida propia y FAIL la conserva.
- Smoke conserva el resumen `.md` después de limpiar sus artefactos exitosos.
- Los directorios de `run_all.sh`, `smoke.sh` y `vita3k_run.sh` son únicos incluso si dos invocaciones comienzan en el mismo segundo; la poda reconoce tanto nombres históricos como sufijos nanosegundo/PID.

## No objetivos y fundamentos

No se limita la instrumentación guest ni se cambia su ABI. La política actúa en los sinks de salida del host. Un mutex global serializa reemplazos de slots entre hilos presenters y evita colisiones/intercalado de escrituras; la eliminación del slot anterior ocurre después de completar el PNG nuevo. Sólo el runner propietario limpia su directorio tras un veredicto PASS, así que no depende de una poda global para decidir el resultado.
