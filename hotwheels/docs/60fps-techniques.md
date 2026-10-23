# 60 fps con lógica fija a 40 Hz — técnicas y recomendación (apoyo a HW-023)

- Autor: `investigador` (opencode deepseek)
- Fecha: 2026-10-02
- Insumo para: **HW-023** (60 fps, owner `luna`). No decide; ordena las opciones con evidencia.
- Niveles: `[estático]` (código/specs locales), `[web]` (fuentes citadas al pie).

---

## 0. TL;DR

El juego corre **lógica a 40 Hz (25 ms de busy-wait, `0x412df0`)** `[estático, HW-023]` y
renderiza **D3D6 pretransformado**: `DrawPrimitive(TRIANGLESTRIP, FVF 0x104 = XYZRHW|TEX1)`,
vértices en **píxeles de pantalla** (x, y, z, rhw, u, v), **sin matrices, sin Z-buffer**,
~**887 draws** por escena (spec 003, FINDINGS #18). No hay estados de simulación accesibles para
"interpolar el mundo"; lo único que tenemos es **el stream de draws**.

**Recomendación:** **interpolar/predecir sobre el stream de draws D3D6** (categoría C), reusando
el modelo de `d2vita/D2Vita/src/glide_ring/replay60` y la idea de *motion prediction* de
**D2DX**. No tocar la lógica (no re-timear a 60 Hz, categoría A) salvo que el RE de HW-023
demuestre que la física es *dt-based*. La opción "motor de estados + interpolación tipo
Gaffer/sm64/SoH" (B) no aplica directamente porque **no reimplementamos el juego**, pero su
marco (alpha de interpolación, dos snapshots) es exactamente lo que se implementa a nivel de
comandos de render.

---

## 1. El blanco exacto (por qué este caso es favorable)

`[estático]` spec 003 / HW-023:

| Propiedad | Valor | Consecuencia para 60 fps |
|---|---|---|
| Lógica | 40 Hz fija (busy-wait 25 ms) | no se puede renderizar más rápido sin desacoplar |
| API | D3D6 `Device3`, un solo shape: `SetTexture`+`TSS(COLOROP,SELECTARG1)`+`SetRenderState`+`DrawPrimitive(TRIANGLESTRIP,0x104,v,3\|4,DONOTLIGHT)` | cada draw es un **primitivo chico con textura** |
| Vértices | **XYZRHW** (screen-space), u,v (0x104) | interpolar = **lerp de x,y** (y u,v fijos); nada de matrices/T&L |
| Profundidad | sin Z-buffer, **orden painter** | la correspondencia es por **orden + textura**, no por depth |
| Volumen | ~887 draws/escena | interpolar es barato; el costo es la **correspondencia** |
| Present | DirectDraw (Flip/Blt) | el `Present` marca el fin de frame lógico |

Es, literalmente, el mejor caso para interpolación por correspondencia de primitivas: no hay
que re-transformar geometría, sólo desplazar vértices de pantalla.

---

## 2. Catálogo de técnicas

### A. Re-timear la lógica a 60 Hz (dt-based / retiming)

Correr el tick de simulación cada ~16.7 ms en vez de 25 ms.

- **Pros:** 60 fps "de verdad", sin artefactos de interpolación, sin latencia añadida; si la
  física usa `dt` explícito, es el resultado correcto.
- **Contras:** los juegos de 2000 suelen tener la física/aceleración/animation **acopladas al
  paso fijo** (como OoT: "all the physics and such is tied to the frame rate" `[web]`). Cambiar
  25→16.7 altera velocidad de juego, IA, timers, animaciones y audio; y `0x412df0` es un
  **busy-wait**, no un acumulador. Riesgo alto y difícil de acotar.
- **Verificación necesaria (HW-023):** ¿el tick lee `dt`/`QueryPerformanceCounter` o multiplica
  constantes por un paso fijo? Si es lo segundo (probable), **descartar A**.
- **Precedente:** D2DX **no** re-timea: mantiene el tick de 25 fps y predice el render `[web]`.
  SoH 60 fps es interpolación, no física a 60 `[web]`.

### B. Fixed timestep + interpolación de estado (Gaffer / sm64 / SoH / OpenLara)

El marco canónico: acumulador, física a paso fijo, y `alpha ∈ [0,1]` con el que se interpola
entre el **estado anterior y el actual** al renderizar (Gaffer, *Fix Your Timestep!*) `[web]`.

- **Precedentes:** `sm64ex/60fps_ex.patch` guarda el frame previo y hace
  `interpolate_vectors_s16(...)` por nodo al renderizar `[web]`; Ship of Harkinian expone un
  modo 60 fps que la comunidad describe como interpolación (`[web]`); OpenLara es
  *framerate-independent* (30/60/90/120) `[web]`.
- **Pros:** matemáticamente correcto y estable; sin overshoot; determinista.
- **Contras para **este** port:** requiere acceso al **estado de simulación** (posiciones de
  entidades, huesos, cámara) en el momento del render. Nosotros **ejecutamos el exe original**
  (no hay motor de estados propio) y el render llega ya pretransformado; no hay "estado
  anterior/actual" salvo **el frame de draws anterior**. Además, interpolar agrega ~1 paso de
  latencia (40 ms) si se usa interpolación pura.
- **Qué se rescata:** el concepto `alpha` y el patrón de **dos snapshots**; se aplica en C, no
  en B.

### C. Interpolación/predicción sobre el stream de draws (replay60 / D2DX)

Guardar los comandos de los dos últimos frames lógicos y, entre ellos, **mover los vértices**
de cada primitiva según correspondencia.

- **Precedente local `[estático]`:** `d2vita/D2Vita/src/glide_ring/replay60` mantiene las dos
  últimas frames del ring (vértices, batches, unidades, cámara) y, en un **hilo host a 60 Hz**,
  re-emite la última moviendo:
  - cada **unidad** por `Δu = pos(t) − pos(N)` (convertido a píxeles),
  - el **mundo** por `−Δcam`,
  - la **UI no se toca**.
  Dos modos: `interp` (t ∈ [N−1,N], α−1, +40 ms de latencia, sin overshoot) y `extrap`
    (t ≥ N, α, sin latencia, riesgo de "rewind").
  Correspondencia por **`(id,type)`** para unidades (hooks por RVA) y por **`(u,v,argb)` +
  proximidad ≤48 px** para el suelo/estático. Métricas: `conv-err`, `reversals`, `frozen`,
  `capped`.
- **Precedente web `[web]`:** **D2DX Motion Prediction** (bolrog/d2dx) predice posiciones de
  unidades entre los ticks de 25 fps: *"doesn't add any lag, but may mispredict the motion
  during 1/25th of a second, after which it gradually compensates for the prediction error"*.
  Es **extrapolación** con compensación del error; cubre proyectiles, monstruos, clima, texto,
  sombras.
- **Pros:** no toca la lógica; cero riesgo de corromper física; usa lo que ya interceptamos (el
  ring de D3D6); barato (lerp de x,y); versión extrap = **sin latencia**.
- **Contras:** hay que resolver la **correspondencia** entre frames (primitivas que aparecen/
  desaparecen, orden painter, texturas compartidas); sólo modela **traslación** (no rotación/
  escala/morph) salvo que se interpole el set completo de vértices de una primitiva emparejada;
  extrapolación puede **sobre-disparar** en giros/colisiones (métrica `reversals`); la UI debe
  excluirse; efectos atados a la lógica (destellos, parpadeos) siguen a 40 Hz (correcto).
- **Encaje con HWM:R:** muy alto, porque el shape de draw es **un primitivo texturizado
  pretransformado** — la correspondencia natural es **(textura, orden de draw, nº de vértices)
  + cercanía de centroide**, y para objetos se puede **taggear por hook** (como replay60 hace
  con `Game+0xdc7b0`). La cámara del juego es un offset de pantalla (`offX/offY` en replay60),
  así que el mundo se mueve de forma **rígida** → `−Δcam` global + `Δobjeto` por primitiva.

### D. Wrapper de presentación (dgVoodoo) — **no resuelve el problema**

dgVoodoo2 implementa DDraw/D3D1-7 sobre D3D11/12 y ofrece **límite de FPS** y escalado, pero
**no interpola frames**: es un wrapper de API, no un sintetizador temporal `[web]`. Sirve como
referencia de *cómo envolver* DDraw/D3D6 y para tracing, no como estrategia de 60 fps. Su
`FPSLimit` sólo cambia el pacing de presentación; a 60 Hz con lógica a 40 Hz seguiría mostrando
el mismo frame repetido.

### E. Interpolación de imagen (MEMC / optical flow) — **descartar**

Estimar movimiento por píxel en la imagen final (estilo TV) y sintetizar frames intermedios.

- **Contras:** costosísimo en CPU (no hay aceleración útil en Vita para esto), artefactos
  (halo, "soap opera"), y **corrompe la UI/HUD** (texto y menús quedarían borrosos). No hay
  depth/motion vectors (D3D6 sin Z) para reproyección temporal. Precedente: D2DX logra alta
  tasa **prediciendo estado de juego**, no procesando la imagen `[web]`.

### F. Desacoplar presentación con frames repetidos (frame repeat) — línea base

Presentar 60 Hz mostrando 40 Hz (patrón 2,1,2,1…). No mejora fluidez, pero da un pacing estable
y es un **fallback** trivial si la interpolación falla. Es lo que hace hoy el port con
`present()`.

---

## 3. Comparación

| Técnica | Toca lógica | Latencia extra | Artefactos | Esfuerzo | Riesgo | ¿Encaja? |
|---|---|---|---|---|---|---|
| A. Re-timear a 60 | **Sí** | 0 | puede romper física/audio | alto (RE profundo) | **muy alto** | sólo si dt-based |
| B. Estado + interp (Gaffer/sm64/SoH) | No | ~1 paso (40 ms) | bajos | alto (no tenemos estado) | bajo | marco conceptual |
| **C. Draws + interp/pred (replay60/D2DX)** | No | 0 (extrap) / 40 ms (interp) | overshoot en giros; correspondencia | **medio** | **medio-bajo** | **sí, primaria** |
| D. dgVoodoo | No | 0 | ninguno | bajo | — | no (no interpola) |
| E. MEMC/optical flow | No | 0–1 frame | halo, UI borrosa | muy alto | alto | no |
| F. Frame repeat | No | 0 | judder | trivial | nulo | fallback |

---

## 4. Recomendación final

**C (stream de draws) como estrategia primaria**, con este diseño, y **A sólo si HW-023 prueba
que la física es dt-based**:

1. **Desacoplar render (60 Hz) de lógica (40 Hz)** con el patrón de `replay60`: un **hilo
   presentador** pinneado a un core (en Vita, como `replay60`), que conserva los **dos últimos
   frames de draws** y re-emite el actual con `alpha = age/period`.
2. **Correspondencia de primitivas** por clave estable: `(tex_id, prim, vertex_count, orden)` +
   **centroide más cercano**; y **tags por hook** donde exista una función de dibujo por objeto
   (equivalente al `unit` de replay60), para robustez.
3. **Modelo de movimiento rígido en pantalla:** `mundo −= f·Δcam` (offset global del juego,
   que replay60 ya verifica contra el suelo) y `objeto += f·Δobjeto` por primitiva emparejada.
   Nada de rotación/escala en la v1 (los draws son quads/triángulos; rotación de sprites en
   pantalla se puede aproximar interpolando los vértices completos de la primitiva).
4. **Modo por defecto: extrapolación** (racer = el jugador no tolera 40 ms de lag), con
   **clamp** (como `alpha ≤ 1.5` de replay60) y **compensación suave** del error al llegar el
   frame N+1 (el modelo de D2DX `[web]`). Ofrecer **`interp`** como opción estable.
5. **UI/HUD nunca se interpola** (marcador `uiV0` en replay60); se dibuja tal cual el frame
   lógico actual, para evitar texto/menús borrosos.
6. **No-interpolables** (destellos, explosiones, texto del mundo): si no hay match de
   correspondencia, mostrar el frame más reciente sin mover (fallback a F para esa primitiva).
7. **Fallback total a F** (60 Hz repitiendo) si algo falla o el ring no tiene dos frames.

### Cómo medir (reusar replay60)

- `conv-err` (error px entre desplazamiento real y predicho), `reversals` (rewinds de la
  extrapolación), `frozen` (replays clampados), `capped` (límite por frame). Un 60 fps que
  introduce `reversals` visibles es peor que 40 estable.
- Validación por nivel: **qemu-arm** (no mide GPU, sí lógica/métricas del presentador) →
  **Vita3K** (visual, screenshot con `docs/vita3k-harness.md`) → **consola** (pacing real; la
  consola está reservada por AGENTS.md). Nunca presentar un número de qemu/Vita3K como consola.

### Preguntas abiertas para HW-023 (`luna`)

1. ¿El tick a 25 ms usa `dt`/contador o multiplica constantes por paso fijo? (decide A vs C).
2. ¿El orden de draws y las texturas son estables entre frames dentro de una escena? (define la
   calidad de la correspondencia).
3. ¿Hay una función de dibujo **por objeto** que se pueda hookear para taggear (como el
   `UnitAny` de D2)? ¿O sólo hay ~887 draws anónimos?
4. ¿La cámara es un offset global explícito (como `offX/offY` en D2) o hay que inferirlo?
5. ¿Cuánto del frame es UI/HUD fijo vs mundo? (para fijar `uiV0`).
6. ¿El cap de 60 en menús / 40 en juego es el mismo tick o dos? (afecta la cadencia 3:2).

---

## 5. Fuentes

- D2DX (motion prediction, 25 fps de lógica, "no lag, mispredict ≤1/25 s, compensa"):
  - https://github.com/bolrog/d2dx
  - https://github.com/bolrog/d2dx/wiki/Motion-Prediction
  - README: https://raw.githubusercontent.com/bolrog/d2dx/master/README.md
- Gaffer, *Fix Your Timestep!* (alpha de interpolación entre estados):
  https://gafferongames.com/post/fix_your_timestep/
- sm64ex 60fps (interpolación de vectores por nodo):
  https://github.com/sm64pc/sm64ex/blob/nightly/enhancements/60fps_ex.patch
- Ship of Harkinian (60 fps por interpolación; física atada al framerate):
  https://gbatemp.net/threads/ocarina-of-time-pc-port-ship-of-harkinian-details-new-features-including-60fps-mode.612281/
  y https://www.reddit.com/r/shipofharkinian/comments/vgi6gk/how_do_you_enable_60fps/
- OpenLara (motor framerate-independent 30/60/90/120):
  https://docs.libretro.com/library/openlara/
- dgVoodoo2 (wrapper de API, límite de FPS, no interpola):
  https://dege.freeweb.hu/dgVoodoo2/
- (local) `d2vita/D2Vita/src/glide_ring/replay60.{h,cpp}`,
  `d2vita/D2Vita/src/glide_ring/glide_atlas.h` (`Vtx`/`Batch`),
  `hotwheels/specs/003-d3d6-renderer.md`, `hotwheels/docs/render-options.md`
