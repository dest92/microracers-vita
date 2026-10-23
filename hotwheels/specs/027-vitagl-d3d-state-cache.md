# HW-040 — reducir estado GL redundante en D3D6

## Problema y contrato

Las escenas Vita3K observadas contienen cientos de `DrawPrimitive` D3D6; el backend repite en cada draw llamadas `glUseProgram`, binds/filtros de texturas, blend y alpha test aunque el estado no cambie. Cachear sólo esas transiciones consecutivas, preservando orden de draws, vértices, texturas y pixels.

El contexto vitaGL se comparte con el presenter P8. `begin_frame()` invalida toda la caché antes del primer draw de cada ring/escena, porque el presenter puede cambiar estado entre reproducciones. Upload/free de textura invalidan las referencias afectadas. El backend y su caché se usan desde el mismo hilo GXM serializado; config/estado no se comparte con threads guest. No se mueve trabajo entre hilos, no se cambia el GXP y no se difieren ni reordenan comandos.

La caché compara programa, textura+filtro de la unidad 0, paleta de unidad 1 y estado blend/alpha. `glTexParameteri` se vuelve a emitir al cambiar de textura aunque el filtro coincida: la propiedad pertenece al objeto textura. Habilitaciones de textura se restablecen en `begin_frame`; al procesar paleta se vuelve a la unidad 0 como exige el camino vertex/fragment actual.

## Aceptación

- Test host valida primera transición, draw idéntico, cambios individuales, textura A→B→A, filtros, blend/alpha e invalidación de escena/liberación de textura.
- Con escena ordenada idéntica, RefRaster conserva golden `0x6d85c1e26d7a5b3c`; el estado cacheado nunca altera calls ni vértices.
- Vita3K instrumenta draws, llamadas GL de estado y skips por escena. Con el mismo `race_enter.script`, config `native/fit` y número de segundos, reduce ≥25% las llamadas de estado por draw frente a build sin cache; no se observa crash ni cambio de resolución.
- Tres runs A/B intercalados informan guest D3D fps y dispersión. No se promete mejora de hardware con datos de Vita3K; si el fps cae >5% en mediana o la reducción de estado no llega a 25%, se retira la optimización o se corrige el conteo.

## Resultado medido

Golden host `test_d3d`: `0x6d85c1e26d7a5b3c`; build Vita PASS (VPK 8,014,142 B). Vita3K 0.2.1, backend OpenGL, stage `stage-d3d` (settings hash `0e51d8d1…`), `race_enter.script` SHA256 `fefc5a83…`, misma escena HW034/interp60 y tres corridas A/B de 25 s. A=backend cacheado; B=`bug026-rollback.vpk` (baseline pre-cache, SHA256 `41442ac9…`). Se toma la última ventana reportada por `fps-window` en cada corrida, cada una cubre 10 s: A 1.7, 4.0, 5.0 fps (mediana 4.0); B 3.6, 3.2, 5.4 fps (mediana 3.6). La mediana no cae; Vita3K presenta dispersión alta y este resultado sólo compara ese emulador.

En una ventana A de 64 escenas, el backend emitió 11.196 llamadas de estado para 55.656 draws. El contador reconstruye las llamadas del camino viejo desde sus operaciones por draw: programa; `activeTexture(0)` antes y después; enable/bind de unidad 0; filtros cuando hay textura; cuatro operaciones extra de paleta cuando aplica; blend y alpha. De 555.890 llamadas de referencia estimadas se evitaron 544.694 (97,98%). Es una reconstrucción auditable del código previo, no un interceptador de GL ejecutado en el binario B; el margen supera el gate de 25%. Una captura de carrera sin crash quedó en `build-vita3k/hotwheels-vita-20261003-101144668142759-1566286/shot_025.png`.

## Razonamiento bajo nivel

El emulador entrega rings al hilo de presentación; D3D y P8 comparten contexto GXM y no existe propiedad exclusiva persistente del estado GL entre rings. Por ello el límite de invalidez es `begin_frame`, sin mutex adicional dentro de `draw`: un lock por cada cientos de draws sumaría contención y no establecería una frontera mejor que la ya impuesta por `vita_gl_mutex`. Los objetos textura viven en el mapa del backend; los handles GLuint pueden reciclarse después de `glDeleteTextures`, así que free/upload invalida comparaciones antes de reusar id. No cambian ABI, guest memory, llamadas syscall ni sincronización ARM/x86.

El costo que se pretende reducir es CPU/driver por transición redundante. Los contadores distinguen `draws`, GL state calls efectivamente emitidas y skips de caché. La tasa Vita3K incluye CPU del runtime y raster del emulador: es un A/B de regresión, no una atribución pura al GPU o una predicción de consola. vertex pointers y buffers se actualizan por draw y quedan fuera del cache porque apuntan a vectores temporales que cambian cada llamada.

## No objetivos

No agrupar/reordenar DrawPrimitive, cambiar texturas/filtros/colorkey/blend, modificar resolución, alterar DINPUT, ni optimizar host RefRaster.
