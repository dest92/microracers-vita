# Decisión de arquitectura — renderer D3D6/DirectDraw en PS Vita

- Autor: `investigador` (opencode deepseek)
- Fecha: 2026-10-02
- Insumo de: **HW-006** (shims gráficos). Complementa `docs/d3d6-ddraw-surface.md` (contrato ABI).
- Niveles de evidencia: `[estático]` (código/headers locales), `[web]` (fuente citada).

---

## 0. Resumen ejecutivo

Hot Wheels usa **DirectDraw 4 + Direct3D 6 immediate** (no D3D9, no Glide). Hay dos formas de
llevarlo a la GPU de la Vita:

- **(A) shim DDraw/D3D6 → vitaGL** (OpenGL fixed-function sobre sceGxm).
- **(B) shim DDraw/D3D6 → sceGxm directo**, reutilizando `src/glide_ring/` y el renderer GXM
  de D2Vita.

**Recomendación: (A) vitaGL como backend primario**, con el shim organizado como **grabador en
anillo guest-side** (patrón `glide_ring`) para que el backend sea intercambiable. Motivo
principal: D3D6 fixed-function es exactamente la superficie que vitaGL ya implementa y que está
probada en ports 3D reales (Quake, Doom 3, Tomb Raider, Xash3D); el renderer GXM de D2Vita es
**2D, painter-order y paletizado**, y no aporta nada del pipeline 3D (matrices, T&L, depth,
perspectiva, fog, texture stages). (B) queda como **fallback** documentado si la licencia LGPL-3
de vitaGL, el rendimiento o la validación en Vita3K lo exigen.

---

## 1. Qué hay que renderizar (el target)

`[estático]` Del `HotWheels.exe` real y de la doc del shim:

- **Juego:** racing 3D top-down, cámara fija/oscilante, 3 pistas, split-screen a 2 jugadores.
- **Resolución:** default **800x600** (PCGamingWiki documenta un hex-edit para cambiarla);
  el readme recomienda **640x480 16-bit**. `[web]`
- **FPS:** cap **60 en menús / 40 en juego** (PCGamingWiki). Es un juego liviano: poca geometría,
  texturas chicas (`.TGA`, `.SFP`), HUD 2D.
- **API:** `DDRAW` (`DirectDrawCreate`) + **D3D6 por COM** (`IDirectDraw4`, `IDirectDrawSurface4`,
  `IDirect3D3`, `IDirect3DDevice3`, `IDirect3DViewport3`). `[estático]`
- **Pipeline de D3D6:** fixed-function (matrices WORLD/VIEW/PROJECTION, `SetRenderState`,
  texture-stage states, `DrawPrimitive`/`DrawIndexedPrimitive`). Sin shaders de usuario (D3D6 no
  los tiene). Un solo stage de textura alcanza.
- **Presentación:** superficies DDraw con `Flip`/`Blt` (a confirmar por traza cuál exactamente).
- **Superficies 16-bit:** `[estático]` los assets son TGA/formatos propios; la resolución 16-bit
  sugiere render **RGB565** (o ARGB1555) + UI 8-bit paletizada. Confirmar con traza Wine.
- **Destino:** pantalla Vita **960x544**, GPU **PowerVR SGX543MP4+** (TBDR), 128 MB VRAM.

**Decisión de presentación (común a A y B):** renderizar a un **FBO al tamaño del juego**
(800x600 / 640x480) y presentar escalado a 960x544 con **aspecto 4:3 respetado
(pillarbox)**, porque el HUD es pixel-based (lo dice PCGamingWiki). Alternativa: 2x del
ancho (1600x1200) → 960x720 y recortar; el punto es no deformar el 4:3.

---

## 2. Alternativa A — shim D3D6/DirectDraw → vitaGL

### 2.1 Qué ofrece vitaGL `[web]`

vitaGL es un wrapper **OpenGL → sceGxm** con aceleración por hardware
(https://github.com/Rinnegatamante/vitaGL). Para este caso lo relevante es que soporta
**fixed-function pipeline (FFP)** y su entorno:

| Necesidad D3D6 | Soporte vitaGL |
|---|---|
| Matrices (MODELVIEW/PROJECTION/TEXTURE) | stack GL clásico; `HAVE_WVP_ON_GPU=1` mueve el cálculo a GPU |
| Multitextura / texture stages | FFP con **más de 2 texunits** (`HAVE_HIGH_FFP_TEXUNITS`); `GL_COMBINE` (`NO_TEX_COMBINER` lo desactiva) |
| Alpha test (color key) | `glAlphaFunc` estándar |
| Fog | `GL_FOG` fixed-function |
| Depth test/write, blend, cull | estados GL estándar |
| Texture wrap/address | `glTexParameter` |
| Display lists | `HAVE_DISPLAY_LISTS=1` |
| Pipeline legacy | `ENABLE_LEGACY_PIPELINE=1` (`vglDrawObjects`) |

También trae caché de shaders (`HAVE_SHADER_CACHE`), caché de texturas (`HAVE_TEXTURE_CACHE`),
FBOs compartidos (`SHARED_RENDERTARGETS`) y speedhacks. Está usado por decenas de ports 3D
(Quake I/II/III, Doom 3, RTCW, Tomb Raider 1&2, Fade to Black, RVGL, Lugaru, etc.).

### 2.2 Licencia — **LGPL-3.0** `[web]`

`[web]` El archivo `COPYING` del repo es **GNU LGPL v3** (verificado vía API de GitHub:
`license.spdx_id = LGPL-3.0`). Implicancias:
- Enlazado **estático** (lo normal en un VPK) obliga a permitir la **religación**: hay que
  publicar el código y/o los `.o` + scripts de build capaces de reconstruir el binario con una
  versión modificada de vitaGL. Para un proyecto ya abierto es cumplible, pero **cambia el
  perfil del repo** (D2Vita/winx86 son MIT).
- Alternativa de mitigación: enlazado **dinámico** (vitaGL no se distribuye como `.suprx`
  de sistema, así que no es directo) o vendorizar en `third_party/` preservando licencia
  (regla 5 de D2Vita) y publicar los `.o`.
- Acción: **decisión del usuario** (política de licencia), no técnica. Si el port debe quedar
  MIT, esto empuja a (B).

### 2.3 Enlace con vitasdk y dependencia de runtime `[web]`

- Se compila con `make install` dentro del ecosistema vitasdk y se enlaza como `libvitaGL.a`.
- **Requiere `libshacccg.suprx`** (compilador de shaders, extraído del firmware y puesto en
  `ur0:data/` del dispositivo) https://github.com/Rinnegatamante/vitaGL y
  https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx.
  Sin él, el homebrew no arranca. **No se puede bundlear** (material de firmware).
- En **consola real (henkaku)**: camino estándar y probado. En **Vita3K**: hay fricción
  conocida con `SceShaccCg` (ver §2.6), a validar temprano (HW-004).

### 2.4 Convivencia con el dynarec/winx86 `[estático]`/`[web]`

- vitaGL y **kubridge** (expansión del pool JIT) son ortogonales: kubridge es un plugin de
  kernel; vitaGL es una librería de espacio de usuario. No compiten.
- Hilos: vitaGL corre un **GC thread** propio y usa su heap; winx86 ya usa varios hilos
  (scheduler del guest, audio `vita_audio`, posible flush thread). La Vita tiene 4 cores; hay
  que **fijar afinidades** (D2Vita ya lo hace: el sink de audio y el replay se pinnean).
- Memoria: vitaGL usa memoria **física contigua/CDRAM** (`PHYCONT_ON_DEMAND`) y el guest arena
  de winx86 vive en RAM principal. 512 MB + 128 MB VRAM alcanzan para un juego de 2000, pero
  **medir** (HW-009 define el layout de arena).

### 2.5 Costo

- **Bajo en shaders/estado**: la FFP ya está. El trabajo del shim es de **traducción**
  (`SetRenderState`→`glEnable/glBlendFunc/glDepthFunc/...`, `SetTransform`→`glLoadMatrixf`,
  `SetTextureStageState`→`glTexEnvi`, `DrawPrimitive`→`glDrawArrays`).
- **Medio en superficies DDraw**: `CreateSurface`/`Lock`/`Unlock`/`Blt`/`Flip`/color key hay que
  mapearlos a texturas/FBOs/`glBlit`/swap. Es trabajo de integración, no de pipeline.
- **Sin control fino** de la GPU (el wrapper decide); overhead CPU de la traducción GL→GXM por
  draw call.

### 2.6 Riesgo de validación en Vita3K

`[estático]` D2Vita documenta (`src/platform/vita_gxm.cpp:346`) que en **Vita3K** el módulo de
shaders falla (`0xc05a1000`) y deja NIDs de `SceShaccCg` sin resolver; su mitigación es
**bundlear los `.gxp` precompilados** en el VPK (`app0:shaders/`) y evitar compilar en runtime.
vitaGL **compila GLSL en runtime**, así que su dependencia de `libshacccg` es más dura que la de
un GXM con `.gxp` ya horneados. Consecuencia: **si el gate M3 es "primer frame en Vita3K",
vitaGL puede no pasar ese gate**, mientras que un backend GXM con `.gxp` bundleados tiene una
chance mejor. Esto es un punto real a favor de (B) para la ladder de validación del proyecto.

---

## 3. Alternativa B — sceGxm directo reutilizando D2Vita

### 3.1 Qué ofrece hoy el renderer GXM de D2Vita (leído) `[estático]`

`src/platform/vita_gxm.{h,cpp}` + `src/glide_ring/`:

- **API host** (`vita_gxm.h`): `d2gxm_init(gameW,gameH)`, `d2gxm_set_window`,
  `d2gxm_page_create/page_upload`, `d2gxm_palette(slot, argb256)`,
  `d2gxm_submit(v,nv,idx,ni,batches,nb,clearARGB,frame)`, modo **async**
  (`d2gxm_busy_from()`/`d2gxm_drain()`), `d2gxm_shutdown()`.
- **Modelo de vértice** (`glide_atlas.h:40`): `struct Vtx { float x, y; float u, v;
  uint32_t argb; float pal; }` → **2D**, sin z, sin w, sin normales.
- **Batch** (`glide_atlas.h:43`): `{first,count,page,blend,flags,cst,pal}` con
  `blend` ∈ {opaque, alpha, additive, multiplicative}, `flags` = chromakey / bilinear /
  color=texture×vertex / alpha constante. **Orden painter, sin depth buffer.**
- **Shaders** (`shaders/*.gxp`): vertex + fragment paletizado, variantes `rgba`, `flat`, `clear`;
  cargados con `SceGxmShaderPatcher`, con fallback a compilar vía `libshacccg` y **`.gxp`
  bundleados en el VPK** (`app0:shaders/`).
- **Ring** (`glide_ring.h`): header con `head/tail` monotónicos, opcodes
  `(op<<24|words)`, grabado **guest-side** por un `glide3x.dll` x86 traducido por el dynarec →
  **3 cruces guest→host por frame** (`Init`, `Flush`, `TexUpload`), no uno por draw.
- **Threads/async**: `replay60` (hilo de replay 60 Hz), `r60_gxm_lock()`, submit asíncrono con
  barrera de atlas; el hilo se pinnea.

### 3.2 Cuánto se puede reutilizar (estimación honesta) `[estático]`

**Reutilizable tal cual (plomería GXM, la parte difícil):**
1. Inicialización de sceGxm, contexto, colas, `SceGxmShaderPatcher`.
2. Carga de `libshacccg` + bundling/lectura de `.gxp`.
3. Formato del **ring** (header, `head/tail`, opcodes extensibles, `frame_end`, dedup).
4. El patrón **grabador guest-side** (DLL x86 mínima traducida por el dynarec).
5. Submit **asíncrono** (`busy_from`/`drain`) y barreras de atlas.
6. Upload de páginas/paletas y manejo de formato 8-bit.
7. Pinneo de hilos y contadores de diagnóstico.

**NO reutilizable (hay que escribirlo para 3D):**
1. El modelo `Vtx`/`Batch` 2D → hace falta posición 3D + w + matrices + UV + color.
2. Shaders: el actual es paletizado 2D → hace falta un vertex shader con matrices/T&L/fog y un
   fragment shader con muestreo 565/1555/4444/8888, texture stage combine, alpha test y fog.
3. **Depth buffer** (D2Vita no lo usa) y render-to-texture para `SetRenderTarget` y superficies
   `DDSCAPS_3DDEVICE`.
4. Mapeo de **render states / texture-stage states / transforms** de D3D6.
5. Presentación 3D y el viewport/scissor de `IDirect3DViewport3`.

En resumen: se reutiliza **la infraestructura** (que es lo caro de depurar en GXM), **no el
pipeline**. Para un juego 2D como Diablo II el backend actual es completo; para un racer 3D es un
esqueleto al que hay que agregarle todo el fixed-function.

### 3.3 Licencia y runtime

- D2Vita y winx86 son **MIT** `[estático]` (`third_party/winx86/LICENSE`): reutilizar mantiene el
  perfil permisivo.
- La ruta GXM **también** importa `SceShaccCg` y busca `libshacccg.suprx`, pero **puede evitar la
  compilación en runtime** bundleando los `.gxp` ya generados (así lo hace D2Vita). Eso da una
  ventaja concreta de validación en Vita3K (§2.6).

### 3.4 Costo

- **Alto en pipeline**: escribir shaders 3D fixed-function + depth + RTT + mapeo de estados.
- **Bajo en integración**: el ring, el submit async y la plomería ya existen y están probados.

---

## 4. La pieza común: grabador en anillo (backend-agnóstico)

Independientemente de A o B, conviene **replicar la arquitectura de `glide_ring` para D3D6**:
un **`DDRAW.dll` x86 guest-side** (MinGW, traducido por el dynarec) que implementa los objetos
COM (`IDirectDraw4`, `IDirectDrawSurface4`, `IDirect3D3`, `IDirect3DDevice3`, …), **graba las
llamadas en el ring** en memoria guest, y sólo cruza al host en `Init`/`Flush`/`TexUpload`
(igual que `glide3x_ring.c`). El host reproduce el ring contra el backend elegido.

Ventajas:
- Elimina el costo de un cruce guest→host por draw/estado (el doc de `glide_ring.h` estima
  ~1000 cruces/frame; a varios µs cada uno serían ms de overhead puro).
- Deja el backend **intercambiable**: el ring es el mismo para A (replay→GL) y B (replay→GXM).
- Encaja con el criterio del proyecto "correr el exe, no reescribirlo" y con el precedente
  D2Vita.

Costo extra: hay que construir la DLL guest-side y el formato de ring para D3D6 (opcodes de
estado, draws, uploads de textura y present).

---

## 5. Comparación

| Criterio | (A) vitaGL | (B) GXM directo |
|---|---|---|
| Cobertura D3D6 FFP | **Completa y probada** (matrices, fog, alpha test, multitex, combine) | Hay que implementarla (shaders 3D + depth + RTT) |
| Esfuerzo inicial | **Bajo/medio** (traducción de estados + superficies) | **Alto** (pipeline 3D desde cero) |
| Reutilización D2Vita | Sólo el patrón del ring | **Plomería GXM + ring + async** (no el pipeline) |
| Licencia | **LGPL-3.0** (obliga religación/estática) | **MIT** |
| Dependencia runtime | `libshacccg` + compilación GLSL en runtime | `libshacccg` opcional si se bundlean `.gxp` |
| Vita3K (gate M3) | Riesgo (shader compiler runtime) | Mejor (`.gxp` precompilados) |
| Control/optimización | Limitado (wrapper decide) | Total |
| Hilos/memoria extra | GC thread + heap propio | Ya integrado con winx86 |
| Riesgo técnico | Bajo en render, medio en superficies | Medio-alto en render, bajo en integración |
| Ports 3D previos | Muchísimos (Quake, TR, Doom3…) | Casos puntuales 2D/Glide (D2Vita) |

---

## 6. Recomendación final

1. **Adoptar (A) vitaGL como backend primario**, detrás del **grabador en anillo** de §4. Es el
   camino de menor tiempo a un frame visible (M3) y a menú jugable (M4) porque la FFP de D3D6 ya
   existe y está probada en 3D sobre Vita; el shim se reduce a traducción.
2. **Aceptar y documentar los costos de (A):** (i) licencia LGPL-3.0 → vendorizar vitaGL en
   `third_party/` con su `COPYING`, y plan de religación en el release (publicar `.o`/build);
   decisión de política para el usuario. (ii) dependencia de `libshacccg.suprx` (igual que la
   ruta GXM). (iii) GC thread + heap → pinneo de afinidad y medición de memoria.
3. **Mantener (B) como fallback explícito**, disparado por: bloqueo de licencia LGPL-3,
   rendimiento insuficiente de la traducción, o fallo de validación en Vita3K. Gracias al ring,
   cambiar de backend no toca el shim guest-side.
4. **Validar primero el shader-compiler path** (HW-004/B): confirmar si `libshacccg`/`.gxp`/vitaGL
   corren en Vita3K; de eso depende qué backend puede pasar el gate M3. Si Vita3K no acepta
   vitaGL, (B) con `.gxp` bundleados pasa a ser el primario *por necesidad de validación*.
5. **Resolver el render target y el present** (común): FBO interno al tamaño del juego
   (800x600/640x480) + present escalado 4:3 a 960x544; `Flip`/`Blt` de la primaria mapean a swap.

Regla de decisión corta: **si el gate M3 debe pasar en Vita3K → (B); si el gate puede pasar en
consola/qemu y la licencia lo permite → (A).**

---

## 7. Audio

### 7.1 DirectSound (efectos) — ya resuelto en winx86 `[estático]`

El inventario de winx86 (`docs/winx86-inventory.md`) confirma que **`src/runtime/ds_emul.cpp`
emula la capa COM de DirectSound** (vtable sintetizada por `Bridge::shim_trap`), y que el host
sólo aporta `HostOps`:

```
struct HostOps {
    uint32_t (*va_alloc)(uint32_t n);  // arena guest para buffers PCM
    uint32_t (*tick_ms)(Cpu&);         // reloj guest (timeGetTime)
    const char* write_root;            // carpeta de salida (sink wav)
    int mix_rate;                      // Hz del stream mezclado (0 = no arma)
};
void install(Bridge&, Cpu&, const HostOps&);
void frame_pump(Cpu&);   // sin hilo, para sinks wav/null
```

Los **sinks** ya existen (`audio_sink.h`): `wav` (evidencia offline), `null`, y **`vita`**
(`sceAudioOut`, `src/platform/vita_audio.cpp`). D2Vita lo engancha en `tools/rt_boot.cpp:3838`
con `mix_rate = 22050`. Para Hot Wheels:
- Registrar `DirectSoundCreate` y el shim de `ds_emul` con un `HostOps` propio.
- **`mix_rate`:** los efectos son **22050 Hz mono 16-bit** `[estático]` (medido en
  `DATA/SOUND/*.wav`); el mixer debería correr a **22050** salvo que la traza diga otra cosa.
- Dependencia de winx86 a completar para HW (gap de HW-003): `timeGetTime` no está en el
  inventario genérico (D2 lo registra aparte); hay que registrarlo para `HostOps::tick_ms`.

### 7.2 Música (CD audio Red Book → `.wav`) — hay que escribir el shim WINMM

El juego **importa `WINMM.dll`: `mciSendCommandA` + `mmio*`** `[estático]`; la música del CD
(Red Book) se toca con MCI. El repack original lo resolvía con **`_inmm.dll`** (de irori), que
intercepta MCI y reproduce WAV/MP3 (`_inmm.ini`), más las pistas `MUSIC/Track{1..4}.wav`.
`[web]` https://community.pcgamingwiki.com/files/file/2656-_inmm-238/

Diseño del shim nativo (equivalente a `_inmm`):

- **`mciSendCommandA(wDevice, uMsg, p1, p2)`** genérico (`MCI_OPEN/SET/PLAY/STOP/PAUSE/
  RESUME/STATUS/GETDEVCAPS/CLOSE`). Para `MCI_OPEN` con `lpstrDeviceType = "cdaudio"`:
  crear un **CD virtual** que lista `MUSIC/Track 1..4.wav`. Para `MCI_PLAY` con posición TMSF:
  mapear track→archivo y arrancar el reproductor. `MCI_STATUS` responde
  `NUMBER_OF_TRACKS/CURRENT_TRACK/LENGTH/MODE` desde esa lista.
- **`mmio*`** (`mmioOpenA/Read/Descend/Ascend/Seek/SetInfo/GetInfo/Advance/Close`): implementar
  un lector **RIFF/WAVE** mínimo (o mapear a IO de archivos del guest). El juego los importa;
  probablemente lee WAV por acá.
- **Reproducción:** los `MUSIC/Track *.wav` son **44100 Hz stereo 16-bit** `[estático]` (medido).
  Opciones:
  1. **Puerto sceAudioOut dedicado** (`vita_audio.cpp` ya soporta varios ports; usa el de BGM y
     cae al MAIN). Un segundo sink/hilo para música **se mezcla en hardware** con el de efectos
     → sin resampling (44100 nativo) y sin competir por el mixer de DirectSound.
  2. Inyectar la música en el mixer de `ds_emul` como un voice más — acopla el shim MCI al
     guest; no recomendado.
  Recomendado: **(1)**, un `audio::Sink` extra para música a 44100, con su hilo pinneado.
- **Timing/loop:** MCI en el juego pide track por número (cada pista es un loop de ~5 min). El
  player debe respetar `MCI_PLAY` desde TMSF y loopear/parar según `MCI_STOP`/`MCI_CLOSE`.

### 7.3 Gaps de audio para HW-003/HW-006

- `WINMM.dll`: `mciSendCommandA` (+ `mciGetErrorStringA`), `mmio*`. **No existen** en el
  inventario genérico de winx86 `[estático]` → son shims nuevos.
- `timeGetTime`/`timeBeginPeriod`/`timeEndPeriod`: registrar (reloj de `HostOps`).
- Mezcla final: arrancar `ds_emul` (22050) + player de música (44100) y verificar que ambos
  suenan (sink wav como evidencia offline, igual que D2Vita).
- El **chequeo de CD** (protección "simple disc check", ver `research.md`) también pasa por
  MCI/`GetDriveTypeA`; el shim de MCI debe responder como si el CD estuviera presente.

---

## 8. Preguntas abiertas / validación

- ¿vitaGL y/o GXM con `.gxp` corren en **Vita3K**? (HW-004; decide qué backend pasa M3).
- Formato real de superficie que pide el juego (16-bit 565 vs 32-bit) y modo de presentación
  (Flip vs Blt) → traza Wine del juego (HW-002/HW-008).
- `mix_rate` real del device DirectSound del juego (traza) — hoy asumimos 22050 por los SFX.
- Presupuesto de memoria: guest arena (HW-009) + vitaGL heap + VRAM de texturas; medir en consola.
- Política de licencia: ¿el release de hotwheels puede incluir LGPL-3 (vitaGL) o debe quedar MIT?

---

## 9. Fuentes

- vitaGL (features/FFP, `libshacccg`, build): https://github.com/Rinnegatamante/vitaGL
- vitaGL licencia **LGPL-3.0**: `COPYING` vía https://api.github.com/repos/Rinnegatamante/vitaGL/license
- `libshacccg.suprx` (extracción): https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx
- PCGamingWiki (D3D6, 800x600, FPS cap, disc check), vía Wayback:
  https://web.archive.org/web/20251112212909/https://www.pcgamingwiki.com/wiki/Hot_Wheels:_Micro_Racers
- `_inmm` (CD audio→WAV): https://community.pcgamingwiki.com/files/file/2656-_inmm-238/
- D2Vita (renderer GXM + ring Glide): https://github.com/Franckrst/D2Vita
- (local) `d2vita/D2Vita/src/glide_ring/`, `src/platform/vita_gxm.{h,cpp}`, `shaders/`,
  `third_party/winx86/src/runtime/{ds_emul.cpp,ds_emul.h,audio_sink.h}`,
  `third_party/winx86/src/platform/vita_audio.cpp`
- (local) `hotwheels/docs/{research.md,d3d6-ddraw-surface.md,winx86-inventory.md}`
