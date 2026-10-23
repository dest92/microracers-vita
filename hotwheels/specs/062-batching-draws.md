# SPEC-062 — Batching de draws consecutivos con estado idéntico en el replay D3D
- Feature: perf consumer vitaGL (M6, 40 Hz) · Dueño: opus-gfx · Estado: borrador

## Problema (medido en consola, 2026-10-22)
OFFICE, D3D 800×600, modo original, clocks 444/222/222/166 (log del orquestador `run3-hotwheels.log`): 18–35 fps,
promedio ~25. El replay del consumer cuesta ~13,5 ms con ~360 draws/frame y 28–42 ms con ~730–780 draws/frame, o sea
**~40–55 µs por draw**, con ~2,2 state ops por draw y casi cero uploads. guest Hz = consumer Hz, así que el consumer es el
cuello de botella. Objetivo: replay < ~20 ms en el peor caso (40 Hz).

## Perfil estático del camino por draw (hoy)
`Replayer::play` arma un `DrawCall` por `DrawPrimitive` del juego; `vitagl_backend draw()`, por draw:
1. Diff de estado contra caché (program, unidad activa, bind de textura, filtros, paleta, blend, alpha). Con la caché de
   HW-042, ~2,2 llamadas GL por draw. vitaGL marca uniforms/estado como sucios en cada cambio.
2. `resize` de 4 `std::vector` (xy, uv, color, pal) y llenado de 9 floats por vértice (36 B).
3. 4× `glVertexAttribPointer` con client arrays.
4. `glDrawArrays`: vitaGL copia **cada** client array al circular pool (4 reservas + 4 memcpy), valida y sube el estado
   del programa FFP/shader (uniforms del vértice/fragmento, `sceGxmSetVertexStream` ×4) y llama a `sceGxmDraw` con el
   buffer de índices por defecto.
Estimación (sin profiler: estático + el total medido): el costo fijo por draw (validación de estado + uniforms +
4 reservas de pool + sceGxmDraw) domina sobre el costo por vértice, porque los draws del juego son chicos
(medido en rings de qemu WAVE: 1.542 draws → 2.344 triángulos, ~1,5 triángulos o 3–4 vértices por draw). Juntar N draws en uno amortiza N-1 veces el costo fijo. Los draws con
el mismo estado son consecutivos con frecuencia: el motor ordena por página de textura (25 páginas por pista).

## Contrato observable
1. El `Replayer` acumula draws consecutivos cuya **clave de estado** es igual: textura, blend, src, dst,
   alpha_test/colorkey, filtro y scissor/viewport. Los emite como **un** `DrawCall` de lista de triángulos (prim 4).
   Strips y fans se convierten a lista con los vértices de cada triángulo en el mismo orden en que el rasterizador
   los arma (sin invertir los impares del strip: el culling está apagado y rotar vértices cambia el orden de la suma de
   baricéntricas en float: medido ~14 px distintos por escena). Un lote
   de un solo draw se emite sin convertir.
2. El lote se emite (flush) antes de: `Begin`, `Clear`, `TexUpload`, `TexFree`, `End`, `Present`, un draw con otra clave, y
   al terminar el ring. `State`, `Tss`, `Texture` y `Viewport` sólo cambian la clave del próximo draw. Así se preserva el
   orden de pintado: entre dos draws del lote no se intercala nada que el backend pinte o use.
3. Tope de 32.768 vértices por lote (`glDrawArrays` de vitaGL usa índices U16 por defecto con `MAX_IDX_NUMBER`=49.152; por
   encima dibuja basura). Un draw que no entra cierra el lote y abre otro.
4. Contadores `draws_guest` y `draws_emitted` en el Replayer, publicados en el log del consumer.
   `HW_D3D_BATCH=0` lo desactiva (A/B en consola).

## Criterios de aceptación
- [x] CA1 Ring sintético con strips, fans y listas del mismo estado, cambio de textura, upload y clear intercalados: el hash
      de RefRaster con batching es **igual** al hash sin batching, y `draws_emitted < draws_guest` → `tests/test_d3d_batch.cpp`.
- [x] CA2 Orden: un draw con otro estado entre dos del mismo estado no se fusiona (3 emitidos); un `TexUpload` entre dos
      draws del mismo estado parte el lote → mismo test.
- [x] CA3 Tope: un lote no supera 32.768 vértices → mismo test.
- [x] CA4 Golden existente (`run_d3d.sh`, 0x6d85c1e26d7a5b3c) sin cambios.
- [x] CA5 Oráculo con rings reales (qemu WAVE/OFFICE volcados): replay offline batching on/off con `compare -metric AE` = 0
      en todas las escenas, y razón draws_emitted/draws_guest medida. **Medido** (qemu OFFICE manejando, 150 escenas,
      `HW_D3D_SYNC_BASELINE=1`): 0 escenas distintas (hash RefRaster), 131.361 draws del juego → 16.730 emitidos (0,127).
- [ ] CA6 VPK candidato con su SHA. El usuario compara fps en consola contra el baseline (OFFICE, mismos clocks), con
      `HW_D3D_BATCH=0` como control en el mismo VPK.

## Diseño (hilos y buffers)
Todo ocurre en el hilo consumer (único dueño del contexto vitaGL). El lote es un `DrawCall` propiedad del Replayer, que
vive entre draws dentro de un mismo `play()`, y el flush final del `play()` garantiza que no cruza frames. Los vértices
se copian una vez al lote (ya se copiaban al `DrawCall`); el backend los vuelve a copiar a sus arrays y vitaGL al pool:
la cantidad de bytes por frame no cambia salvo la expansión strip/fan → lista (un quad de 4 vértices pasa a 6: ×1,5),
a cambio de dividir por N el costo fijo. Circular pool: 2 MiB / 3 ≈ 699 KiB por frame; 780 draws × 6 vértices × 36 B
≈ 170 KiB, holgado. Si se pasa, vitaGL cae al alloc
por draw (seguro; SPEC-052 lo cuenta).

## No-objetivos
Índices propios, VBO persistentes o reordenar draws (cambiaría el resultado del blending).

## Evidencia
host (tests + golden), qemu (oráculo de rings reales), Vita3K (smoke), consola (fps, usuario).

## Revisión 2026-10-22: texturas negras en consola (WAVE) con e6749fb
Evidencia: reporte visual del usuario en consola (texturas negras en la playa con e6749fb; b5e3fda sin batching, sin
glitches). No reproduce en qemu/RefRaster (150 escenas reales idénticas on/off) ni en Vita3K. La clave de estado está
completa por construcción: el backend vitaGL lee sólo campos de `DrawCall` y todos están en `same_state`; uploads y frees
cortan el lote.
Hipótesis (refutable, sólo-hardware): el tope de 32.768 vértices permite un envío de ~1,18 MB (36 B por vértice en 4 arrays)
que desborda la porción del circular pool de vitaGL (2 MiB / 3 ≈ 699 KiB por frame). vitaGL cae a
`gpu_alloc_mapped_for_cpu` por reserva y, si falla con la memoria justa de la consola, devuelve NULL y dibuja igual
(`vgl.c` reserve_data_pool): stream de UV/color inválido, textura negra. Vita3K tiene memoria de sobra.
Cambio: **tope de 4.095 vértices por lote** (múltiplo de 3; 4.095 × 36 B ≈ 144 KiB por envío, ~1/5 de la porción).
Refutación en consola (misma pista y escena, playa, manejando 60 s): VPK nuevo vs mismo VPK con `HW_D3D_BATCH=0`. Si
con el tope chico persisten las texturas negras, la hipótesis cae y se mide con el VPK V0 de diagnóstico (`circ_overrun`,
`vgl_alloc_fail`).
- [ ] CA7 Tope 4.095 con borde exacto: 1.365 listas de 3 (= 4.095) salen en 1 envío; 1.366 salen en 2. Flush en `Begin` y
      `End` entre dos draws iguales → 2 envíos → `tests/test_d3d_batch.cpp`.

### Corrección (review-1ea906c-sonnet P2, 2026-10-22)
El circular pool se desborda por **bytes acumulados en el frame**, no por el tamaño de un envío: el tope de 4.095 no
cambia los bytes por frame. Estimación: 650–960 draws × 3–4 vértices × 1,5 (expansión) × 36 B ≈ 105–210 KiB por frame,
holgado frente a 699 KiB. La hipótesis del desborde queda **débil y sin atribuir**. Discriminación en consola (playa,
misma escena, manejando): (A) VPK con batching vs `HW_D3D_BATCH=0`; (B) batching ON + `HW_DIAG_CIRC_MB=8`; (C) VPK V0 de
diagnóstico con batching, leyendo `circ_overrun`, `vgl_alloc_fail` y `canary_bad` (`HW_DIAG_CANARY=1`). Si (A) reproduce,
(B) no cambia y (C) da contadores en 0, la causa no es el pool: siguen el estado GL del backend con lotes grandes y el
camino `GL_TRIANGLES` vs strips de vitaGL.
