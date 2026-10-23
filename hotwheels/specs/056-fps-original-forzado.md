# SPEC-056 — El runtime corre siempre en modo original (interp60/native60 inalcanzables)
- Feature: HW-067 (BUG-047) · Milestone: M8 · Dueño: opus-gfx · Estado: implementada (8dc6031; CA4 en Vita3K, docs/bug047-oraculo.md)

## Problema
Videos de Vita3K en SLUMBER con `race_full.script` (acelerando), mismo VPK V0 (orquestador, 2026-10-21): con
`fps_mode=interp60` hay agujeros negros y polígonos fuera de lugar en toda la pista; con `fps_mode=original`, en los
mismos momentos, la imagen está limpia. Las capturas de grilla del oráculo (SPEC-046) no lo mostraban porque la cámara
estaba quieta, y en Vita3K/GXP corrimos con `fps_mode=original`. Decisión del usuario: "olvidate del interpolado, que
funcione como el original".

Mecanismo (estático, `src/fps/interp60.cpp`): el consumidor de 60 Hz no tiene geometría propia. Arma cada frame
intermedio emparejando cada draw de mundo del frame N con uno del frame N-1 (mismo ordinal o, si no coincide, el más
cercano en el espacio dentro de `kSearchRadius`, con la misma firma: textura, generación, primitiva, cantidad de
vértices y viewport) e interpola XY entre los dos. Con la cámara quieta la lista de draws es estable y el par es el
mismo polígono. En movimiento, el motor reordena y cullea draws de un frame a otro, y en las pistas con tiles repetidos
(misma textura y vértices) el par puede ser **otro polígono**: el vértice interpolado cae a mitad de camino entre dos
objetos (polígono fuera de lugar), y el polígono correcto no se dibuja en su lugar (agujero, fondo negro). Es un error
de correspondencia, no de precisión: no se arregla afinando la heurística sin identidad real de primitivas.

Origen del interp60 en los perfiles: el runtime lee sólo `ux0:data/hotwheels/hwvita.ini` (desde 9af52ed). La vía para
activarlo es el selector de arranque (mantener L: el ítem "FPS mode" se guarda en ese ini) o escribirlo a mano. El
`ux0:data/hwvita.ini` con `fps_mode=interp60` que quedó en el perfil Vita3K (2026-10-03) no lo lee nadie.

## Contrato observable
1. `select_runtime_mode(guardado, backend)` devuelve **siempre** `Original`. Si el valor guardado no es `original`,
   marca `ignored_saved=true`, y el runtime loguea
   `[hw] fps_mode=<guardado> ignorado: el runtime corre siempre en original (BUG-047, SPEC-056)`.
2. El selector de arranque deja de ofrecer "FPS mode": sus ítems son control, escalado y filtro. Guardar desde el
   selector no cambia el `fps_mode` cargado.
3. El parser del ini sigue aceptando `fps_mode` (un ini viejo no genera warnings nuevos). El ini por defecto documenta
   que el valor se ignora.
4. Reversible: el código de interp60 queda compilado e intacto. Su única entrada (`select_runtime_mode`) no lo elige
   nunca. Revertir = devolver la selección anterior y el ítem del selector.

## Criterios de aceptación
- [x] CA1 `select_runtime_mode(Interp60|Native60, backend=true|false)` → `Original`, `ignored_saved=true`;
      `Original` → `ignored_saved=false` → `tests/test_fps_runtime.cpp`.
- [x] CA2 `BootMenu` tiene 3 ítems sin "FPS mode"; recorrer todos con Left/Right no cambia `fps_mode` → `tests/test_io.cpp`.
- [x] CA3 `parse("[display]\nfps_mode=interp60")` sigue cargando el valor sin warning (compatibilidad) → `tests/test_io.cpp`.
- [x] CA4 Vita3K: VPK con un ini `fps_mode=interp60` en SLUMBER/`race_full.script` loguea `ignorado` y
      `fps_mode guardado=interp60 efectivo=original`; video/capturas en movimiento sin agujeros.

## Diseño (hilos y buffers)
Con `Original` el runtime no arranca el hilo consumidor de 60 Hz (USER_2) ni el historial de snapshots de interp60. El
replay queda en el camino de `fps_mode=original`, que es el que validaron qemu/RefRaster, Wine y Vita3K (SPEC-046), con
la pipeline de 3 slots de HW-053 sin cambios. No cambia ningún dueño de buffer CPU↔GPU: se apaga un consumidor entero.
Costo: el juego vuelve a su cadencia de presents (≈ 40 Hz de guest en Vita3K); no se agrega trabajo.

## No-objetivos
Borrar interp60 o sus tests (decisión reversible). Arreglar la heurística de emparejamiento.

## Evidencia
estático (interp60.cpp, runtime_vita.cpp, config.cpp), Vita3K (videos del orquestador; CA4), consola: Fase 2.
