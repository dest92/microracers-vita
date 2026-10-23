# HW-055 — caché de texturas D3D por contenido

## Contrato observable

Cuando un `TexUpload` trae el mismo ID, dimensiones, pitch, formato, bytes nativos, paleta y rango/estado de colorkey que el último upload vivo, el `Replayer` no convierte ni llama al backend. Un hash FNV-1a de 64 bits resume el origen antes de decodificar a ARGB. `TexFree` invalida la entrada; cambios en cualquier campo incluido generan una subida.

La comparación se hace en el consumidor que posee el backend; no retiene una copia extra de los píxeles y vale tanto para `original` como `interp60`, pues ambos usan `Replayer`.

## Criterios verificables

- Test host repite contenido y verifica un upload backend, modifica texel/paleta/metadata y verifica una nueva subida, libera y vuelve a subir.
- Golden D3D permanece idéntico.
- `fps-window` separa `tex_uploads_per_frame_milli` (comandos guest) de `tex_backend_uploads_per_frame_milli` (llamadas efectivas al backend), para comparar A/B en Vita3K y qemu.
- A/B usa el mismo `Replayer` persistente y cambia sólo el bypass de caché; el microbenchmark qemu repite el mismo `TexUpload` 100.000 frames y cuenta hits/subidas. El workload del juego debe registrar repeticiones/hits; cero repeticiones significa inconcluso, no PASS.
- Aceptación: en workload repetido baja `tex_backend_uploads_per_frame`; el caso de 100.000 frames baja de un upload/frame a una subida de calentamiento. La mediana `guest_d3d_hz_milli` no debe caer más de 5%. Si falla, refutar la optimización o revisar hashing. Reportar tres ventanas sostenidas por modo/plataforma; no inferir consola desde emulador.

## Riesgo

El fingerprint de 64 bits tiene riesgo residual de colisión. El estado cacheado se indexa por ID de textura y se invalida con `TexFree`; esto evita copiar/retener un segundo buffer de tamaño potencialmente grande en Vita.
