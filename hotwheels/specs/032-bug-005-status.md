# HW-046 — alinear metadato BUG-005

## Problema

BUG-005 tiene sección de cierre, captura P8/RGBA y menú Vita3K revisados APRUEBA, pero la cabecera todavía dice “review del fix pendiente”. El índice lo marca cerrado para validación emulada.

## Contrato observable

El guard del índice exige fila única y `Estado: cerrado` vigente para BUG-005. Se preserva la limitación explícita: no hay prueba en consola ni cierre de HW-021.

## Aceptación

- Test-first falla con el estado stale de cabecera.
- Metadato se alinea con el cierre emulado y review ya registrada.
- Test de índice y `run_all.sh` pasan; review código/spec APRUEBAN.

## Rol SO

El estado debe expresar el contrato que la evidencia realmente valida: presentación emulada dentro del pool de memoria, sin extrapolar al hardware Vita. Esta edición es documental.

## Implementación y evidencia

Test-first `279c386` detectó “resuelto, review pendiente”; el header ahora dice cerrado para validación emulada. `coordination/review-HW-021.md` APRUEBA fixture P8/RGBA y menú Vita3K, sin extrapolar a consola ni cerrar HW-021. El guard busca metadata en las primeras cinco líneas; test de índice PASS. `run_all.sh` final 27 PASS, 0 FAIL, 1 SKIP. Reviews de código/spec APRUEBAN (`coordination/review-HW-046.md`).
