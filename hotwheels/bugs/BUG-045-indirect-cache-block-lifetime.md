# BUG-045 — caché indirecta: puntero a dynablock liberable tras soltar mutex

- **Severidad:** mayor
- **Owner:** runtime
- **Estado:** abierto; bloquea integrar `winx86-0009-indirect-cache.patch`
- **Detectó:** runtime/review, 2026-10-03

## Síntoma y evidencia

El prototipo de HW-034 conserva `dynablock_t*` en una tabla EIP→bloque.
`dyn86_ibr_find()` tomó `mutex_dyndump`, cotejó clave/época y leyó metadata
con el mutex held, pero soltó el mutex antes de devolver el puntero.
`FreeDynablock()`/`FreeInvalidDynablock()` pueden entonces liberar metadata/code
antes de que `DBGetBlock` revalide/consuma el puntero en dispatcher.

Review code reportó P1 dos veces: la comprobación de época evita hits futuros,
pero no fija la vida del bloque actual. El mecanismo de gracia existente sólo
protege la ruta de eviction; no todos los frees directos. Torture actual es
secuencial y no cubre la intercalación de free concurrente.

El candidato se retiró de `hotwheels/patches/`; quedó archivado como
`hotwheels/out/winx86-0009-indirect-cache-experimental.patch` y no debe
activarse. La prueba vigente sólo cubre encadenamiento, memoria guest y SMC
secuencial. El A/B del prototipo (4 pares QEMU) fue ruidoso/inconcluso y no se
considera validación de rendimiento.

## Hipótesis y próximo paso

Hace falta pin/refcount/hazard hasta finalizar el uso del bloque, o hacer que
todas las rutas de reclamación respeten la misma grace period. Probar una
intercalación determinista cache-hit/free con ASan/TSan o un harness que fuerce
`FreeDynablock` entre lookup y dispatch; no reintroducir el patch hasta que ese
caso pase y code review apruebe.

## Resolución (opus, 2026-10-03): NO SE ARREGLA — se retira la caché por diseño
- **Lo que reemplaza la caché ya es O(1) y sin locks**: `DBGetBlock`→`internalDBGetBlock`→`getDB(addr)` = `box86_jmptbl[idx1][idx0]` + una indirección (`custommem.c:1054`). La tabla de saltos de box86 ES un mapa directo y completo EIP→bloque. La caché IBR antepone `mutex_trylock`, cargas atómicas seq/época, chequeos de metadata y `unlock`: **más trabajo que el lookup que evita** (no puede ganar), y además crea el problema de lifetime de este bug.
- **Techo medido** (perfil fiel por PC host, ventana sólo carrera, build con 0006–0008): `DBGetBlock` 0.08 % + `internalDBGetBlock` 0.08 % + `getDB` ~0.02 % del hilo guest; dispatch total 1.62 % (domina `UpdateFlags` 0.88 %, no el lookup). Contadores WX86_JITPROFILE: recompiles=0, revalidations=0 (sin churn SMC que una caché pudiera evitar).
- Coherente con el A/B ruidoso/inconcluso del prototipo: no hay señal que medir.
- Por tanto: no reintroducir `winx86-0009-indirect-cache`; el archivado en `hotwheels/out/` queda como referencia. Un test ASan de use-after-free para código que no debe existir no aporta; si en el futuro se propone otra estructura de lookup, el criterio de entrada es superar al jump table en un microbench aislado antes de discutir lifetime.
- Si el dispatch importara (menú 4.5 % en el perfil de runtime), el costo está en la vuelta al dispatcher (DynaRun/epílogo/UpdateFlags), no en la búsqueda: atacarlo es encadenar más (menos salidas), no cachear.
- **Estado: cerrado (won't fix).**
