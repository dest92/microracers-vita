# SPEC-052 — VPK de diagnóstico de BUG-047 para Fase 2 (consola)
- Feature: HW-067 (diseño, opus-gfx) → HW-068 (implementación, sol-gfx) · Milestone: M8 · Estado: borrador

## Problema
El oráculo (SPEC-046) **no reproduce BUG-047 fuera de la consola**: OFFICE se ve igual que Wine x86 en qemu/RefRaster
(quieto y manejando, identidad por I/O) y en Vita3K/GXP (quieto, identidad por `sceIoOpen`), y WAVE coincide en los tres.
Vita3K ejecuta la GPU casi sincrónica y con memoria holgada, así que esconde dos familias de bug que sólo aparecen en
hardware real (decisión del orquestador, 2026-10-21):
1. **Vida útil CPU→GPU de vértices**: memoria de vértices reescrita antes de que la GPU termine de leerla. Ejemplos: el
   circular pool de vitaGL (`vglSetCircularPoolSize(2 MiB)` en `vita_present.cpp`, partido por display buffer, con
   fallback `gpu_alloc_mapped_for_cpu` por draw al desbordar y **NULL silencioso** si ese alloc falla; sin
   `LOG_ERRORS` no queda rastro) o los `std::vector` del backend pasados como client arrays → "polígonos que saltan".
2. **Vida útil y memoria de texturas**: `glDeleteTextures`/re-`glTexImage2D` mientras un frame en vuelo las usa
   (pipeline de 3 slots HW-053 + caché por contenido HW-055 + carry HW-063), o fallo de alloc de textura sin
   chequear (`GL_OUT_OF_MEMORY`) cuando las pistas no-playa suben más páginas → "texturas mal".
Hipótesis alternativa que el VPK también tiene que poder separar: (3) **runtime x86 distinto en consola** (dynarec
ARM real vs qemu, x87 HW-054, tcache HW-059). En ese caso la geometría ya sale mal **en el ring**, antes de la GPU.

## Contrato observable
Un único VPK (mismo TitleID HWMR00002) con telemetría siempre activa (barata) y toggles por `ux0:data/hotwheels/env.txt`
(ya soportado: `load_env_txt`, líneas `KEY=VAL`). Sin env.txt, comportamiento = VPK normal + telemetría.

### Telemetría (log `ux0:data/hotwheels/hotwheels.log`, línea `[hw] diag ...` cada 10 s y al salir de carrera)
| clave | qué | por qué |
|---|---|---|
| `ring_bad` / `ring_verts` / `ring_maxn` / `ring_edge` | `ring_stats` (header `src/d3d/ring_stats.h`) acumulado en la ventana: vértices no finitos en **x/y/u/v** + fuera de guarda, total, n máx, arista máx | si sube en OFFICE y no en WAVE ⇒ (3) runtime/shims, la GPU queda descartada. **Z/RHW sólo diagnóstico** (el replay los ignora, aviso sol-gfx) |
| `vgl_ram_free_min` / `vgl_vram_free_min` | mínimo de `vglMemFree(VGL_MEM_RAM/VRAM)` en la ventana (muestreo por frame) | presión de memoria ⇒ fallos de alloc |
| `circ_peak` / `circ_overrun` | bytes de vértices pedidos por frame (suma calculada en el backend: verts×stride por draw) vs `circular_pool_size / display_buffers`; overruns = frames que superan la porción | (1): overrun ⇒ fallback a alloc por draw |
| `vgl_alloc_fail` | fallos de alloc de vitaGL: **hook propio** en el clon aislado de vitaGL (contador expuesto al backend). En c707ff00 `LOG_ERRORS` sólo hace `sceClibPrintf`: no hay `FILE_LOG`/log a archivo | (1)/(2) |
| `gl_err` | `glGetError()!=0` por frame (contado, 1.er código guardado) | (2) `GL_OUT_OF_MEMORY` en `glTexImage2D` |
| `tex_live` / `tex_bytes` / `tex_up` / `tex_del_inflight` | texturas vivas, bytes estimados, uploads por ventana, y **deletes o re-uploads de un id usado por un frame aún no presentado** (último frame de uso vs frame presentado) | (2) vida útil |
| `q_dropped` / `q_carried` | ya existen en `fps-window` (pipeline) | contexto |
Costo: O(palabras del ring) por escena para `ring_stats` (sólo con `HW_DIAG_RINGSTAT=1`, muestreo 1 de cada 4 escenas);
el resto, contadores enteros. Medirlo por **A/B repetido** (WAVE, Vita3K, ≥3 corridas por lado): la diferencia de fps debe
quedar dentro del ruido medido entre corridas iguales o ≤ 2 %, lo que sea mayor; no se acepta una muestra suelta.

| `circ_wraps` / `circ_slot` / `circ_off` | por draw (agregado en la ventana; detalle sólo de los primeros 64 draws de la escena con `HW_DIAG_DRAWLOG=1`): slot del circular pool (`vgl_circular_idx`), offset y tamaño de cada reserva, y cuántas reservas cayeron al fallback por overrun | ring reciclado mientras la GPU lee (precedente exacto: Vita3K PR #4248, "spikes + UV incorrectas") |
| `pool_enter` | `vglMemFree`/`vglMemTotal` por pool (RAM, VRAM, SLOW/PHYCONT si existe) **al entrar a la pista** (fin de `state_raceLoad`), en el primer frame y al salir | agotamiento/leak por pista |
| `canary_bad` | al rotar un slot del circular pool (antes de resetear su puntero): checksum del área usada en su último frame vs el checksum guardado al hacer submit; distinto ⇒ alguien escribió el área mientras estaba en vuelo. Con `HW_DIAG_POISON=1` además se rellena el slot reciclado con un patrón (NaN/0xDEADBEEF): si la GPU lee un área ya reciclada el glitch cambia de forma (spikes al infinito) | escritura en uso / lectura tardía |

Índices U16 (estático + qemu, 2026-10-21): el backend dibuja con `glDrawArrays`; vitaGL c707ff00 usa un buffer de índices
U16 por defecto con `MAX_IDX_NUMBER=0xC000` (49152) y, si `count` lo supera, **sólo loguea y dibuja igual** leyendo fuera
del buffer (`draw.c:210`). Medido en qemu (ringstat, WAVE y OFFICE): payload máx de cualquier op = 32776 palabras
(un TexUpload 256² 16 bpp), así que todo Draw tiene ≤ 5462 vértices ≪ 49152, lejos también del límite de valores U16 (<64000).
**Descartado** salvo que el runtime de consola genere draws distintos (lo cubre `ring_maxn` en la telemetría).

### Toggles (env.txt, uno por corrida; default = actual)
| env | efecto | separa |
|---|---|---|
| `HW_DIAG_GLFINISH=1` | espera total a la GPU por escena: `sceGxmFinish`/`glFinish()` tras cada `vglSwapBuffers` (cada escena del juego = un frame; CPU espera a la GPU, como Vita3K). Sólo diagnóstico: si arregla, el fix es ownership por slot + notificación, no la espera global | si arregla ⇒ carrera CPU/GPU, familias (1)/(2) |
| `HW_D3D_SYNC_BASELINE=1` | ya existe: sin hilo consumidor ni pipeline de 3 slots | pipeline HW-053/carry HW-063 |
| `HW_DIAG_TEXCACHE=0` | `Replayer(texture_cache=false)`: re-sube siempre (sin caché FNV64 HW-055) | caché por contenido |
| `HW_DIAG_TEXDEFER=N` | difiere `glDeleteTextures` N frames (cola de borrado) | (2) delete en vuelo |
| `HW_DIAG_CIRC_MB=N` | `vglSetCircularPoolSize(N MiB)` (2..32) | (1) circular pool |
| `HW_VGL_RAM_MB` / `HW_VGL_VRAM_MB` | ya existen (vgl_pools.h, acotados) | presión de memoria |
| `HW_DIAG_RINGSTAT=1` | telemetría `ring_*` (arriba) | (3) runtime |
`fps_mode=original` y `filter=game` en `hwvita.ini` durante toda la Fase 2 (interp60 hace matching heurístico y puede
"pegar" polígonos parecidos: se prueba aparte, sólo si el usuario jugaba con interp60).

### Variantes de vitaGL (compilación; un `eboot` por variante dentro del mismo VPK o VPKs etiquetados)
Hoy vitaGL se compila **sin ningún speedhack** (`build_vitagl.sh`: sólo `HAVE_VITA3K_SUPPORT=1 HAVE_FFP_SHADER_SUPPORT=1`),
así que la matriz útil es: **V0** = flags actuales + hook de diagnóstico propio (clon aislado de vitaGL, SDK intacto);
**V1** = V0 + `SAFE_DRAW=1 SAFE_UNIFORMS=1`; **V2** = V0 + `NO_CIRCULAR_POOL=1` (buffers temporales no reciclados, más
CPU; si el glitch desaparece ⇒ lifetime del circular pool, prioridad 1 según research). Hechos de vitaGL c707ff00 (sol-gfx, estático): los client arrays se copian
al circular pool en cada draw; el overrun del circular pool cae a alloc por draw (no hace wrap, speedhack apagado); los
`glDeleteTextures` de una textura en uso se liberan en diferido (lazy free, 4 frames). Si se elige un solo VPK, el selector de arranque (`boot_selector`, mantener L) lanza `app0:eboot_v1.bin`
con `sceAppMgrLoadExec`; si no, dos VPK con hash registrado. SHA de vitaGL (`c707ff00`), flags y VPK en cada fila.

## Protocolo Fase 2 (con el usuario; consola)
Pistas por `settings.dat` (SPEC-046: `tools/race-d3d-track{1,2,3}.dat` = WAVE/OFFICE/SLUMBER; respaldar el del usuario y
restaurarlo). Cada corrida: 60 s de carrera **manejando** (acelerar, chocar el borde una vez), captura de pantalla a los
~20 y ~40 s, copiar `hotwheels.log` (la telemetría de vitaGL va ahí vía el hook). Orden (parar en cuanto se aísle la causa):
1. V0 WAVE sin toggles (control: sin glitches; telemetría de referencia).
2. V0 OFFICE sin toggles (debe reproducir BUG-047; si no reproduce ⇒ comparar `settings.dat`/ini del usuario).
3. OFFICE con `HW_DIAG_RINGSTAT=1` → `ring_bad` OFFICE ≫ WAVE ⇒ **(3) runtime**: avisar al orquestador antes de seguir.
4. OFFICE `HW_DIAG_GLFINISH=1` → si arregla, seguir con 5–7 para ubicar el recurso; si no, saltar a 8.
5. OFFICE **V2** (`NO_CIRCULAR_POOL=1`) y luego `HW_DIAG_CIRC_MB=16` → si V2 arregla ⇒ (1) lifetime del circular pool
   (mirar `circ_wraps`/`canary_bad`); si sólo arregla el pool grande ⇒ tamaño insuficiente.
6. OFFICE `HW_DIAG_TEXDEFER=3` y luego `HW_DIAG_TEXCACHE=0` → si arregla ⇒ (2) vida útil de texturas / caché.
7. OFFICE `HW_D3D_SYNC_BASELINE=1` → si arregla ⇒ pipeline/carry.
8. OFFICE V1 (SAFE_DRAW/SAFE_UNIFORMS) → si arregla ⇒ estado/draw de vitaGL.
9. Mirar telemetría de la corrida 2: `vgl_*_free_min` cerca de 0, `vgl_alloc_fail>0` o `gl_err` ⇒ memoria; probar
   `HW_VGL_RAM_MB=40`/`HW_VGL_VRAM_MB=96`.
Tabla de resultados: pista × variante × toggle × (glitch sí/no, captura) × telemetría.

## Diseño (SO/hardware: hilos, dueño de cada buffer, orden de memoria, bordes)
**Hilos.** El hilo guest (dynarec) produce el ring D3D en un slot de `frame_queue` (3 slots); el consumidor VitaGL (hilo
propio, afinidad USER_2 en interp60, o el mismo hilo con `HW_D3D_SYNC_BASELINE=1`) hace el replay y es el **único** que
llama a vitaGL/GXM. Toda la telemetría de vitaGL/texturas se cuenta en ese hilo (sin atomics); `ring_stats` corre donde se
cierra la escena (productor) y publica sus contadores al hilo de log con atomics relajados (sólo estadística, ningún
dato depende del orden).

**Dueño de cada buffer (CPU↔GPU) en c707ff00.**
1. Slot del ring: productor hasta `submit`, consumidor hasta `release`; nunca la GPU (se copia en el replay).
2. `xy_/uv_/color_/pal_` del backend: CPU. `glDrawArrays` con client arrays **copia** al circular pool en el draw,
   así que la CPU puede reusarlos apenas vuelve la llamada.
3. Circular pool: 3 porciones (`gxm_display_buffer_count=3`) de 2 MiB/3 ≈ 699 KiB. La porción `i` es de la CPU mientras se
   arma el frame N (`vgl_circular_idx = N % 3`), pasa a la GPU en el `sceGxmEndScene` del frame N, y la CPU la vuelve a
   escribir en el frame N+3. Esto es seguro **sólo si** la GPU terminó N antes de que arranque N+3. Lo garantiza la cola
   de display: `sceGxmDisplayQueueAddEntry` admite como mucho `buffer_count-1` flips pendientes y el flip de N se procesa
   después del fragment de N. Ese invariante es el que miden `canary_bad`/`POISON`.
4. Overrun de la porción: cada reserva siguiente va a `gpu_alloc_mapped_for_cpu` + `mark_as_dirty`, y el GC la libera
   frames después. Es seguro en ownership, pero **si el alloc falla devuelve NULL y el draw igual sale** (`vgl.c`
   reserve_data_pool). Mecanismo plausible de "polígonos que saltan" sólo en HW con memoria justa. Contar
   `circ_overrun` y `vgl_alloc_fail`.
5. Texturas: `glDeleteTextures` de una textura en uso → lazy free 4 frames (> 3 en vuelo); `glTexImage2D` sobre una
   existente realoca y marca la vieja como sucia. Con eso vitaGL debería ser correcto; `tex_del_inflight` verifica que
   nuestro uso (caché HW-055, carry HW-063, pipeline) no tenga un camino que escriba memoria de textura sin pasar por esas
   dos llamadas.

**Orden de memoria.** La GPU lee lo que la CPU escribió en memoria mapeada para GXM. vitaGL la reserva para CPU como
memoria no cacheada/write-combined (se verifica en el hook: tipo de memblock). Si fuera cacheable sin flush, la GPU podría
leer líneas viejas de 32 B, y el síntoma sería parecido. `HW_DIAG_GLFINISH` no lo arregla (sólo ordena en el tiempo), así
que separa "lifetime" de "coherencia". El canary se calcula sobre la vista de la CPU: detecta escrituras en vuelo, no
incoherencia de caché.

**Bordes.** Escena con 0 draws (no reserva; la rotación igual avanza). Draw de 0 vértices (el backend no debe llamar a
vitaGL). Varias escenas por swap o draws antes del primer swap: la porción no rota y crece hasta el overrun (contarlo).
Wrap de contadores: u64. Draw > 49152 vértices (U16 de `glDrawArrays`): hoy ≤ 5462 medido; `ring_maxn` lo vigila.
Fallo de `glTexImage2D` (`GL_OUT_OF_MEMORY`): contarlo y **no** usar la textura incompleta (hoy no se chequea).

## Criterios de aceptación (cada uno ⇒ un test; implementa sol-gfx)
- [ ] CA1 parser de toggles `HW_DIAG_*` (valores inválidos ⇒ default + warning) → test host.
- [ ] CA2 contador `tex_del_inflight`: modelo host con 3 frames en vuelo; delete de un id usado en frame no presentado
      cuenta 1; con `TEXDEFER=N` el delete real ocurre N frames después → test host.
- [ ] CA3 `circ_peak/overrun`: suma de bytes por frame y overrun contra la porción configurada → test host.
- [ ] CA4 línea `[hw] diag` con todas las claves, formato estable (parseable por `tools/track_oracle.py`) → test host.
- [ ] CA5 build Vita de las tres variantes V0, V1 (SAFE_DRAW/SAFE_UNIFORMS) y V2 (NO_CIRCULAR_POOL) PASS, con SHA de cada VPK registrado; Vita3K WAVE/OFFICE con telemetría sin regresión visual y costo
      medido por A/B repetido (dentro del ruido entre corridas iguales o ≤ 2 %, lo mayor; ver Telemetría).

## No-objetivos
Arreglar BUG-047 (eso viene después de la Fase 2). Tocar interp60. Cambiar defaults del VPK de usuario.

## Riesgos
- Que el glitch dependa del `settings.dat`/ini del usuario (resolución 320/640/800, interp60): registrar ambos al inicio.
- `glFinish` cambia el timing del juego (40 Hz), pero sólo se usa para clasificar.
- El hook de vitaGL vive en un clon aislado: el SDK instalado y el VPK de usuario no cambian.

## Evidencia
estático (código vitaGL c707ff00: `vgl.c` reserve_data_pool / `gxm.c` rotación por frame), qemu/Wine/Vita3K (SPEC-046,
no reproduce), consola: Fase 2.
