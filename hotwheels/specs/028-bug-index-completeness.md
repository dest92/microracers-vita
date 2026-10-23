# HW-042 — detectar detalles de bug huérfanos

## Problema

HW-039 hace únicos los IDs y valida cada link, pero no exige que cada `bugs/BUG-NNN*.md` aparezca en el índice. El detalle cerrado BUG-034 quedó invisible desde `BUGS.md`.

## Contrato observable

La regresión host compara los archivos de detalle canónicos con los destinos del índice y falla si falta o sobra un detalle. La tabla debe incluir BUG-034 con su resolución ya documentada.

## Aceptación

- Test-first falla antes porque BUG-034 no tiene fila.
- Agregado el registro de BUG-034 como cerrado con referencia al fix y test existente.
- `test_bug_index.py` y `run_all.sh` pasan; review APRUEBA.

## Rol SO

Los bugs son estado compartido para coordinar tareas concurrentes. Un detalle huérfano equivale a una pérdida de visibilidad del estado: puede provocar duplicación de trabajo o que se reabra un defecto ya cerrado. El control corre sólo en host y no toca el runtime.

## Implementación y evidencia

Test-first `43d1c2c` falló por BUG-034 huérfano; la tabla ahora lo enlaza como cerrado con `733dd2f` y `INT32_MAX`/scratch 4 KiB. `test_bug_index.py`: 37 IDs únicos, enlaces, encabezados y detalles PASS. `run_all.sh` final: 26 PASS, 0 FAIL, 1 SKIP. Revisión de código y diseño APRUEBAN (`coordination/review-HW-042.md`).
