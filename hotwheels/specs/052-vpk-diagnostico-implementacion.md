# SPEC-052 — anexo de implementación GPU (sol-gfx, HW-068)
Contrato: `052-vpk-diagnostico-bug047.md` (opus-gfx). Evidencia de diseño: source vitaGL c707ff0 y archive SDK/vendor idénticos SHA aef3f477…640689. Sin consola Fase 1.

## Teoría y límites
El productor guest posee el ring hasta publish; la lease impide reuso CPU mientras consume. Esto NO garantiza lifetime GPU: el driver debe copiar/preservar arrays y texturas hasta completar sus submissions. En c707ff0 client arrays se copian a `gpu_alloc_mapped_temp`; pool circular normal se divide por buffers de display y hace fallback alloc+lazy-free al exceder la porción. El speedhack circular sí wrappea, pero no está habilitado. GC de textura reciente usa 4 frames, display queue máximo 2 pendientes; verificar con fences reales, no inferir de Vita3K. No se cambia el guest, x86 flags/FPU, afinidad ni GIL.

Backend/telemetría corren en el mismo hilo/contexto que replay. Hooks vendor de alloc fallido usan contador atómico u32 relaxed (pueden ocurrir durante init/GC); ningún lock extra ni espera circular. Lectura de notification GXM como volatile con barrera CPU al reciclar; no sustituye coherencia/mapeo GXM. `glFinish`/`sceGxmFinish` son toggles diagnósticos: espera total deliberada, no fix de rendimiento.

## Implementación
- Parser y modelo host independientes de vitaGL: toggles, ventana estable, memoria libre mínima muestreada, presión estimada de vértices y lifetime de objetos borrados. `tex_del_inflight` significa uso aún no presentado en CPU; no afirmar que swap acredita GPU idle.
- Acumulación de `ring_stats` en el Replayer (mismo hilo de replay) vía `Replayer::set_diag(window, ringstat)`, muestreo 1 de cada 4 `play()` cuando HW_DIAG_RINGSTAT=1; no se agrega un virtual nuevo a `Backend` (el ring es propiedad del replay y el muestreo vive en un solo lugar). Sólo x/y/u/v consumidas participan en ring_bad; Z/RHW quedan fuera.
- Cache off en constructor Replayer, incluido presenter interp60 sin editar su lógica. Default cache=true.
- TEXDEFER difiere objetos GL únicos por N presents; no guarda punteros a TextureData ni a guest. Cada nombre se libera una sola vez. Sin defer se preserva delete inmediato del API (vitaGL ya hace lazy-free de backing).
- Circular size configurable 2..32 MiB antes de vglInit; RAM/CDRAM mantienen knobs existentes. MemFree por todos los pools al cambiar target/entrar en D3D; high-water es **muestreado** por frame.
- Patch de hooks aplicado sólo a clones de vitaGL bajo out/, nunca al SDK global: reserva (slot/offset/size/frame), seal y recycle, alloc NULL. Perfil V0 log+hooks; V1 además SAFE_DRAW/SAFE_UNIFORMS; V2 además NO_CIRCULAR_POOL. Source+patch+flags+archive+VPK SHA en manifiesto. Los speedhacks quedan explícitamente 0 en los tres; un experimento con uno activado debe ser otro build, no un env falso.
- Hook seal conserva checksum de hasta 128 B (principio/final del rango usado) y fence esperada; recycle compara antes de sobrescribir. Detecta escrituras inesperadas y reuso antes de fence; **no** demuestra por checksum que GPU dejó de leer. HW_DIAG_CANARY/HW_DIAG_TRACE_DRAW opt-in por costo/volumen.
- glFinish por present; toggle separado HW_DIAG_GXMFINISH por EndScene usando contexto GXM del driver y espera explícita. Índices del backend son DrawArrays, primitivos 3/4 vértices; contador rechaza/reporta draws ≥64000, índices sintéticos ushort de vitaGL. Sin índice guest que reinterpretar.
- LOG_ERRORS=2 no escribe archivo en este commit: FILE_LOG está sin usos, vgl_log=sceClibPrintf. Fallos reales del allocator se cuentan por hook y `[hw] diag` se deriva al logger persistente del port.

## Tests antes de implementar
CA1 parser defaults/límites/inválidos + warning. CA2 modelo 3 frames/delete/defer y nombre único. CA3 bytes/frame, boundary porción y reset. CA4 claves estables/ventanas, ring XYUV; Z NaN no invalida. Prueba fake GL del backend verifica toggles y chequeos de error/puntero tras upload; contador alloc NULL comprobado por hook sintético. Build V0/V1/V2 y replay Vita3K con `race_full.script` (acelera/steering) en WAVE/OFFICE. Capturas HUD20–40 s y comparación de ventanas; reportar intervalo/ruido si no se puede distinguir una cota del 2 %.

## Contrato de fence y re-upload en vuelo (rev-gfx P2, cc-gfx 2026-10-21)
En c707ff0 la porción del circular pool **rota y su puntero se resetea antes** de que `scene_end` incremente
`query_fence.value` y lo pase a `sceGxmEndScene`. La fence del rango escrito en el frame N sólo existe al `scene_end` de
N, así que el orden soportado es: (1) CPU escribe la porción `N % 3`; (2) `sceGxmEndScene(N)` incrementa la fence y encola
su notification → se guarda `fence_N` como la asociación de la porción; (3) el flip de N se procesa después del fragment
de N (la display queue admite ≤ `buffer_count-1` flips pendientes); (4) la porción se reusa recién en N+3 y sólo tras
observar `fence >= fence_N` por poll/wait de la notification GXM. Una barrera de CPU sola no espera a la GPU y un
`volatile` sólo evita el cacheo por el compilador; `sceGxmFinish` es la cota superior diagnóstica (toggles), no el fix.
**Test de modelo**: asocia cada frame a la fence con la que se submitió y detecta una asociación corrida un frame
(`last_fence(N)` debe ser la fence de N, no la de N-1) y el wrap del slot cada `buffer_count` frames.

Re-upload de una textura en uso: `vitagl_backend.cpp:upload_texture` arranca con `free_texture(t.id)` y crea objetos
nuevos; si el id fue usado por un frame no presentado el reemplazo debe diferirse igual que el delete (`TEXDEFER=N`) y
contarse en `tex_up_inflight`. `glTexImage2D` sobre una textura viva realoca y marca la vieja como sucia: cubrir los dos
caminos con el mismo defer. El checksum/canary queda como **detector de escritura**, nunca prueba de GPU idle.

## Razonamiento SO/hardware del módulo host (cc-gfx, 2026-10-21)
`src/diag/hw_diag.h` es header-only, sin estado global ni hilos propios: es un modelo puro que el hilo que ya posee el dato
invoca. Toggles: se leen **una vez** al arranque (mismo `getenv` que `load_env_txt` ya deja en el proceso) antes de crear el
backend; no se re-leen por frame, así no hay carrera entre el hilo de replay y el de telemetría. Los contadores del modelo
son locales al llamador; cuando se compartan entre hilos (replay ↔ telemetría fps) se acumularán `u32` relajados sin
locks, porque sólo se leen para reportar y nunca se usan para decidir una transición de ownership.
Ownership CPU↔GPU: `TexLifetime` modela **vida útil de un id de textura**, no la de un buffer GXM. Un id referenciado por
un frame que aún no se presentó sigue "en vuelo" (el consumidor puede leerlo); con `TEXDEFER=N` el delete real se programa
`N` presents después y recién ahí sale por `drain()` — que es donde el backend ejecuta el `glDeleteTextures`. El modelo es
explícito en que presentar (swap) **no** acredita GPU idle: sólo dice que el frame dejó de ser "no presentado" en CPU.
`CircWindow` suma bytes de vértices por frame (el productor conoce `verts×stride` por draw) y los compara con la porción
`pool_bytes/display_buffers`; no toca el allocator. Bordes: `== porción` no es overrun (el fallback sólo dispara al
*exceder*); la porción usa división entera, así que un pool no divisible se reparte por defecto. `add_ring` acumula sólo
x/y/u/v vía `RingStats::bad_verts()`; z/rhw NaN quedan fuera de `ring_bad` (el replay los ignora, aviso sol-gfx). El
formato de línea usa `key=value` sin espacios en el valor para que el regex `(\w+)=([^\s]+)` de `track_oracle.py` lo lea
sin cambios y `ring_edge` se imprime con un decimal fijo (sin notación científica).

## Correcciones del modelo host (review rev-gfx P2, cc-gfx 2026-10-22)
Revisión: `coordination/review-SPEC052-host-diag.md` (CAMBIOS). Esta sección fija el contrato corregido del modelo; el
módulo host es la **referencia** de la telemetría (no una fence real de GXM ni un allocator).

### P2#1 — la fence ausente es "sin evidencia", no "fence 0"
En GXM la fence del frame N existe recién en el `sceGxmEndScene(N)`. Un slot circular que se recicla sin saber si el frame
que lo escribió llegó a `EndScene` se está reciclando sobre datos que la GPU quizá todavía lee. Por eso "nunca sometida" tiene
que ser un estado explícito que **bloquea** el reuso, no un 0 que se compara como fence completada (0 también es una fence
legítima: el primer `EndScene` puede valer 0).
- `FenceSlots::submit(frame, fence)` asocia la fence del frame **sometido** y poda la tabla: se conservan sólo las
  `buffers_+1` asociaciones más recientes (la de `frame-buffers_` es la que consulta el próximo reuso). La tabla no crece
  por frame.
- `FenceSlots::submitted(frame)` = hay evidencia de submit.
- `FenceSlots::may_reuse(frame, completed)` = `frame < buffers_` (el slot nunca giró) **o** (`submitted(frame-buffers_)` **y**
  `completed >= fence(frame-buffers_)`). Sin evidencia ⇒ no se reusa, aunque `completed` sea altísimo.
- Orden de uso: `may_reuse(frame)` se consulta **antes** de `submit(frame)` (el submit es la evidencia de que el frame se
  entregó).

### P2#2 — un delete por nombre, y el nombre que se reusa manda
El doble free rompía el ownership: el primer `free_texture` difería el id y el segundo lo entregaba ya por `ready_`
(el llamador ejecutaba `glDeleteTextures` con el frame en vuelo) y el schedule viejo entregaba el mismo id otra vez.
- Estado por id: `live` (existe objeto) + `last_use` (frame en que lo usó por última vez).
- `free_texture(id)` de un id **no vivo** (doble free o id desconocido) es no-op: no cuenta ni entrega.
- `free_texture(id)` de un id vivo cuyo `last_use > presented_` (usado por un frame no presentado) cuenta
  `deletes_inflight` y difiere la entrega; si no está en vuelo, la entrega es inmediata.
- Re-upload del mismo nombre antes de que venza el delete pendiente: el delete pendiente queda **obsoleto** (el objeto
  nuevo reemplaza al viejo) y se cancela — el `free+create` del backend ya liberó los objetos GL viejos. El re-upload de un
  id en vuelo cuenta `reuploads_inflight`: es la métrica de (2), no una corrección (SPEC-052 no arregla BUG-047).

### P2#3 — el diferimiento se cuenta en presents, no en begins
`begin_frame()` abre una escena; el swap (`present`) es lo que cierra el frame en la CPU. Varios begins antes de un swap
hacían vencer el diferimiento con menos presents de los pedidos (o nunca, si `presented_` dejaba de avanzar).
- Se lleva un contador de **presents** (`presents_`), distinto del de frames abiertos.
- Vencimiento: `deadline = presents_en_la_peticion + defer + 1`. El `+1` es el present que libera el frame que usó la
  textura; con `defer=0` el delete sale en ese present (mínimo seguro: la textura dejó de estar en vuelo en CPU).
- Entrega: `presents_ >= deadline` **y** el frame que usó la textura ya fue presentado (`presented_ >= last_use`).
  Si el frame nunca se presenta, el delete no sale (sigue en vuelo).
- `defer` son presents de espera **extra**; es lo que el protocolo Fase 2 mide con `HW_DIAG_TEXDEFER=N`.

### P2#4 — política única del parser: inválido ⇒ default + warning
CA1 (contrato, `052-vpk-diagnostico-bug047.md`) dice "valores inválidos ⇒ default + warning". El parser recortaba al rango
(`HW_DIAG_CIRC_MB=99` → 32): dos políticas distintas para el mismo contrato. Se unifica: no numérico **o** fuera de
[lo,hi] ⇒ **default + warning** (el warning dice el valor, el rango y el default usado). El rango sigue validándose:
`HW_DIAG_CIRC_MB=1` (mínimo 2) y `HW_DIAG_TEXDEFER=-3` (mínimo 0) son inválidos y avisan.
