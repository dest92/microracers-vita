# HW-062 — aislar `vita3k.log` por corrida

## Problema

`vita3k_run.sh` copia el log persistente completo de Vita3K desde su home al artefacto de corrida. Como el mismo home sobrevive entre ejecuciones, el artefacto mezcla sesiones y distorsiona recuentos/diagnósticos.

## Contrato observable

- Cada artefacto `vita3k.log` contiene sólo los bytes agregados desde el snapshot previo a instalar y lanzar el juego.
- Dos invocaciones que usen el mismo `VITA3K_HOME` no corren simultáneamente; el lock abarca stage, snapshots, emulador y extracción.
- Si Vita3K trunca, reemplaza o crea el log durante la ejecución, se conserva el archivo nuevo entero.
- Si el log no cambia o desaparece, se produce un artefacto vacío; el home persistente queda intacto.
- Se comparte el extractor robusto de HW-060; no se cambia stdout, el estado Vita3K ni su ciclo de vida.

## Criterios verificables

1. Test del runner simula append a `~/.cache/Vita3K/vita3k.log`, incluso con error al iniciar Docker, y compara captura/delta byte a byte.
2. Test con dos runners simultáneos sobre un mismo home verifica que `docker run` no se solapa.
3. Suite del extractor y `bash -n vita3k_run.sh` pasan.
4. Smoke Vita3K genera `vita3k.log` igual al sufijo nuevo del log del home.

## Razonamiento de sistemas

El log está en el home del emulador, independiente del árbol guest `ux0:data`. Se toma el snapshot en el host antes de que arranquen las dos pasadas de Vita3K y se extrae luego de esperar/cancelar el contenedor. El lock del home evita que otra ejecución contribuya bytes al mismo sufijo o altere settings mientras se toma la medición. El artefacto es una vista por corrida; el archivo append-only compartido y su prefijo histórico no se mutan.

## No objetivos

- Rotar, truncar o borrar el log persistente.
- Cambiar la verbosidad de Vita3K ni capturar stdout del proceso.
