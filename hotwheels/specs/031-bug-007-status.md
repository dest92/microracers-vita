# HW-045 — sincronizar metadato BUG-007

## Problema

La cabecera de BUG-007 aún declara regresión pendiente de review, pero el mismo documento cierra la causa y cita el header llvm-mingw, spec corregida y DDraw 2/2 PASS; `BUGS.md` también lo marca cerrado.

## Contrato observable

El guard del índice exige fila única y estado cerrado vigente en la ficha BUG-007. Su evidencia de offset 104 y el resultado DDraw existente permanecen en el historial.

## Aceptación

- Test-first falla porque la cabecera contiene `review pendiente`.
- El metadato vigente se alinea con el cierre documentado y no altera el código DDraw.
- `run_ddraw.sh` PASS (2/2), `run_all.sh` verde; reviews APRUEBAN.

## Rol SO

La compatibilidad ABI depende del layout binario exacto; conservar el header y offsets medidos junto al cierre permite revisar la afirmación sin repetir un cambio de ABI. Esta tarea sólo ajusta metadatos, no modifica memoria ni llamadas guest.

## Implementación y evidencia

`5e467c3` test-first falló con la metadata “regresión pendiente de review”. Alineé el estado con el cierre ya documentado: header mingw `ddsCaps` offset 104, spec corregida, DDraw 2/2 PASS. Reviews de ABI/regresión APRUEBAN (`66db451`, `bce7432`). `run_all.sh` final 26 PASS, 0 FAIL, 1 SKIP; review de código/spec APRUEBA (`coordination/review-HW-045.md`).
