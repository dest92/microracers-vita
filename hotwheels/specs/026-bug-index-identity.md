# HW-039 — identidad única del índice de bugs

## Problema

`hotwheels/BUGS.md` asigna BUG-023 y BUG-024 a dos defectos distintos. Una referencia por ID es ambigua aunque cada fila enlace a un archivo distinto; historial y coordinación pueden terminar cerrando el defecto equivocado.

## Contrato observable

La suite host falla si el índice repite un ID o si alguna fila enlaza a un detalle inexistente/con encabezado cuyo ID no coincide. IDs existentes conservan sus evidencias y se renumeran los dos registros duplicados a IDs libres.

## Aceptación

- Test automático descubre IDs duplicados antes del arreglo.
- Suite host pasa tras renumerar registros y mover referencias directas.
- La asociación entre IDs viejos y nuevos queda documentada para interpretar logs históricos.

## Rol SO

El índice es estado compartido entre agentes: identidad estable evita una carrera lógica al actualizar el defecto equivocado. El chequeo es de lectura, corre en host y no afecta scheduling ni memoria del runtime.

## Renumeración

- `BUG-023-qemu-frames-caida` → BUG-036: observación cerrada de rendimiento bajo contención; los logs históricos siguen mostrando el ID usado entonces.
- `BUG-024-smoke-qemu-wav-path` → BUG-037: fallo transitorio cerrado de arranque; el BUG-024 GIL mantiene su ID canónico.
