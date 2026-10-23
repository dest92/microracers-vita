# Patrones de ports reales de PS Vita → aplicables a hotwheels

- Agente: `ports` (opencode/deepseek-flash). Fecha: 2026-10-02.
- Nivel de evidencia: `[estático]` = lectura de fuente clonada; `[web]` = fuente citada por URL.
- Los repos se clonaron shallow (`--depth 1`) en `hotwheels/research/ports/` (gitignoreado, no se commitea).
  Lista y URLs al final (§5).

Objetivo: extraer patrones concretos de ports/runtimes reales para el shim DDraw4/D3D6 y el runtime
winx86 de hotwheels. Contexto del port: el juego usa **DirectDraw 8 bpp palettizado por software**
para intro/menús (ver `FINDINGS.md` #13) y **D3D6** para carreras; el camino crítico M3 es
`paleta 8bpp → textura → pantalla`. Por eso este doc pone el foco en (1) cómo se cargan/hookean
binarios ajenos, (2) cómo se sube un framebuffer palettizado por frame, y (3) cómo se escala
640×480 → 960×544.

---

## 0. Resumen ejecutivo (lo que hay que copiar)

1. **so-loader no es el modelo de hotwheels** (hotwheels carga un PE32/x86, no un `.so` ARM), pero sí
   son el modelo **el patrón de hooks quirúrgicos + tabla de imports + shims de libc** (cada llamada
   del guest se redirige a una implementación nativa, no se reescribe el juego).
2. **kubridge ≠ obligatorio**: TheFloW lo usa para memoria RX de usuario; los emuladores (DaedalusX64)
   usan `sceKernelAllocMemBlockForVM` + `sceKernelOpenVMDomain`/`sceKernelSyncVMDomain`, que **no
   requieren kernel plugin** (D2Vita lo documenta como fallback de 16 MiB).
3. **Paleta 8bpp**: el patrón local probado (D2Vita) es **LUT escalar 4:1** en CPU para el present
   final, y **shader con lectura dependiente** (`tex2D(page)` → `tex2D(palette)`) cuando se dibuja
   geometría palettizada. La variante "hardware palette" (`P8_ABGR` + `sceGxmTextureSetPalette`) gana
   porque **elimina la lectura dependiente** — es la que hay que priorizar en Vita.
4. **vitaGL ya soporta textura palettizada** (`GL_COLOR_INDEX8_EXT` → `SCE_GXM_TEXTURE_FORMAT_P8_ABGR`
   + `glColorTable`) y tiene **escritura directa sin copia** del buffer de textura
   (`vglGetTexDataPointer`) — ideal para streaming por frame de un framebuffer 8bpp.
5. **Audio = `sceAudioOut` en hilo propio**, grano bloqueante (~23 ms), no en el hilo del guest. Hay
   implementaciones de referencia completas (D2Vita winx86, RetroArch `psp2_audio.c`).
6. **Escalado con aspect**: `fit_rect()` (16.16 fixed-point, nearest, duplicación de filas,
   letterbox centrado). Para 640×480 → 960×544 da **725×544** con barras laterales de 117 px.
7. **VPK/sce_sys/livearea** es boilerplate: `vita_create_vpk` + `sce_sys/livearea/contents/` y el
   "startup image" que lanza a config.

---

## 1. so-loader (Cargar y hookear un binario ajeno)

Repos: `TheOfficialFloW/gtasa_vita`, `fgsfdsfgs/max_vita`, `v-atamanenko/soloader-boilerplate`.

### 1.1 Modelo general

El `.so` ARM de Android se ejecuta nativo; sólo se interceptan las llamadas al SO. `so_util` hace
load + relocate + resolve + init array, y luego el loader emparcha direcciones conocidas.

`gtasa_vita/loader/main.c` (flujo, líneas 1327-1345) `[estático]`:
```c
if (so_load(&gtasa_mod, SO_PATH, LOAD_ADDRESS) < 0) fatal_error(...);
so_relocate(&gtasa_mod);
so_resolve(&gtasa_mod, default_dynlib, sizeof(default_dynlib), 0);
patch_mpg123(); patch_openal(); patch_opengl(); patch_game(); patch_gfx(); patch_scripts();
so_flush_caches(&gtasa_mod);
so_initialize(&gtasa_mod);
```

La resolución de imports tiene **dos vías** (`gtasa_vita/loader/so_util.c:288`, `so_resolve`):
- símbolos entre los propios `.so` (`so_resolve_link`),
- tabla `so_default_dynlib` con `{ "nombre", (uintptr_t)&func }` para todo lo que aporta el loader.

URL: https://github.com/TheOfficialFloW/gtasa_vita/blob/master/loader/so_util.c

### 1.2 Hooks ARM/Thumb (kubridge)

El hook escribe un salto absoluto en la primera instrucción. Requiere memoria de código y
`kuKernelCpuUnrestrictedMemcpy` (kubridge) para escribir y `kuKernelFlushCaches` para I-cache.

`gtasa_vita/loader/so_util.c:26-61` `[estático]`:
```c
void hook_thumb(uintptr_t addr, uintptr_t dst) {
  addr &= ~1;
  if (addr & 2) { uint16_t nop = 0xbf00;
    kuKernelCpuUnrestrictedMemcpy((void *)addr, &nop, 2); addr += 2; }
  uint32_t hook[2] = { 0xf000f8df /* LDR PC,[PC] */, dst };
  kuKernelCpuUnrestrictedMemcpy((void *)addr, hook, sizeof(hook));
}
void hook_arm(uintptr_t addr, uintptr_t dst) {
  uint32_t hook[2] = { 0xe51ff004 /* LDR PC,[PC,#-4] */, dst };
  kuKernelCpuUnrestrictedMemcpy((void *)addr, hook, sizeof(hook));
}
void so_flush_caches(so_module *mod) {
  kuKernelFlushCaches((void *)mod->text_base, mod->text_size);
}
```

**Qué aplicar en hotwheels**: el equivalente no es hook ARM (el guest es x86 traducido), pero el
`winx86` ya tiene su mecanismo de "native hooks" (ver `D2Vita/src/runtime/native_hooks_*`). Para el
shim DDraw lo relevante es la **tabla de imports** y los stubs que loguean, más los parches puntuales
a direcciones del PE (los ports usan offsets fijos: `gtasa_mod.text_base + 0x1C1382`).

### 1.3 Memoria ejecutable: kubridge vs VM

- so-loader: `kuKernelAllocMemBlock(..., SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, ...)` + `hook_*` +
  `kuKernelCpuUnrestrictedMemcpy` → **requiere kubridge** y lo chequea al arrancar
  (`main.c:1281`, `_vshKernelSearchModuleByName("kubridge", ...)`).
- DaedalusX64 (dynarec): usa VM sin kubridge (ver §3.1).

**Qué aplicar en hotwheels**: el dynarec x86→ARMv7 necesita RWX. Seguir el patrón de DaedalusX64
(`sceKernelAllocMemBlockForVM` + `sceKernelOpenVMDomain` + `sceKernelSyncVMDomain` en cada bloque
generado) permite **correr en Vita3K sin kubridge**; kubridge queda como fast-path (más de 16 MiB,
`kuKernelFlushCaches`).

### 1.4 libc: puente SceLibcBridge y fakes biónicas

- `gtasa_vita/libc_bridge/` genera `libSceLibcBridge_stub.a` desde `nids.yml` (`vita-libs-gen`) y
  expone `sceLibcBridge_malloc/fopen/fread/...`. El loader los mapea en su tabla de imports y además
  wrappea el `memcpy/memset` del guest (`-Wl,--wrap,memcpy`).
  URL: https://github.com/TheOfficialFloW/gtasa_vita/tree/master/libc_bridge
- `max_vita/imports.c` implementa **fakes de pthread/semáforos biónicos** (los `pthread_mutex_t*`
  de Android tienen otro layout): `pthread_mutex_init_fake`, `pthread_cond_wait_fake`,
  `pthread_create_fake`, y stubs `AAssetManager_open → ret0`.
  URL: https://github.com/fgsfdsfgs/max_vita/blob/master/imports.c

**Qué aplicar en hotwheels**: análogo directo. El gap-list (`docs/gap-list.md`) ya lista KERNEL32/USER32
faltantes; el patrón es tabla de stubs + fakes para tipos con layout distinto. El FS case-insensitive
del guest (`FINDINGS.md` #10) se implementa en la capa de shims, no en el loader.

### 1.5 Input: sceCtrl + touch → controles del guest

Ambos ports exponen la API del juego ("gamepad") y la alimentan con `sceCtrlPeekBufferPositiveExt2`.

`max_vita/hooks/game.c:204-260` `[estático]`:
```c
if (sceCtrlPeekBufferPositiveExt2(port == 0 ? 0 : 2, &pad, 1) < 0) return -1;
if (port == 0) sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch_front, 1);
...
if (pad.buttons & SCE_CTRL_CROSS) mask |= 0x1;
if (pad.buttons & SCE_CTRL_L2)     mask |= 0x40; // L1
...
val = ((float)pad.lx - 128.0f) / 128.0f;   // eje analógico
```
`gtasa_vita/loader/main.c:1307` inicializa muestreo `SCE_CTRL_MODE_ANALOG_WIDE` y touch; los gatillos
se pueden mapear a **toca atrás** (`config.front_touch_triggers`). El mapeo botón→acción es
configurable por archivo (`gtasa_vita/loader/config.c`, `ButtonMapping`).

**Qué aplicar en hotwheels**: el juego original (FINDINGS #10) usa **DInput teclado+mouse** y joystick
opcional. El shim DInput debe traducir `sceCtrlPeekBuffer*` a `GetDeviceState` de teclado/joystick, con
un archivo de config usuario (estilo `config.txt`) para remapear. El mouse se emula con stick o touch.

### 1.6 Audio: dos escuelas

- **so-loader**: los juegos Android usan OpenAL; el loader **puentea las 100+ entradas de OpenAL**
  (`gtasa_vita/loader/openal_patch.c`, `max_vita/hooks/openal.c`) contra una lib OpenAL portada a Vita
  (linkea `openal`), y sólo parchea casos borde (`alcCreateContextHook` fuerza 44100;
  `alDeleteBuffersHook` desencola buffers antes de borrar).
  URL: https://github.com/TheOfficialFloW/gtasa_vita/blob/master/loader/openal_patch.c
- **Motor nativo/emuladores**: `sceAudioOut` directo (ver §3).

**Qué aplicar en hotwheels**: el juego usa **DirectSound** (FINDINGS #10: primario 22050/16/estéreo +
6 secundarios estáticos). No hay OpenAL que puentear: hay que implementar el shim DirectSound sobre
`sceAudioOut` con un mixer propio (patrón D2Vita `ds_emul.cpp` + `vita_audio.cpp`). El primario
22050/16/estéreo cae exacto en las tasas que `sceAudioOutOpenPort` acepta (ver §3.4).

### 1.7 I/O de archivos y rendimiento: FIOS2 + RAM cache

`max_vita/fios.c` / `gtasa_vita/loader/fios.c` `[estático]`:
```c
SceFiosParams params = SCE_FIOS_PARAMS_INITIALIZER;
...
params.threadAffinity[SCE_FIOS_IO_THREAD] = 0x20000;
params.threadPriority[SCE_FIOS_IO_THREAD] = 64 + 2;
res = sceFiosInitialize(&params);
...
g_RamCacheContext.pPath = DATA_PATH;
res = sceFiosIOFilterAdd(0, sceFiosIOFilterCache, &g_RamCacheContext);
```
- `gtasa`: cache 512 bloques × 128 KiB (64 MiB), hilos FIOS con afinidad.
- `max_vita`: cache dimensionable por config (`config.io_cache_block_num * size`).

**Qué aplicar en hotwheels**: el juego lee archivos completos con `CreateFileA/ReadFile/SetFilePointer`
(FINDINGS #10) y en el arranque abre su propio directorio. FIOS2 es **sólo para `ux0:`**; hay que
shimear la API Win32 de archivos a `sceIo*`/FIOS, con cache de lectura y **case-insensitive**. Para el
overlay/instalador de 222 MB no se lee nunca (FINDINGS #15), así que el costo de I/O real es bajo.

### 1.8 Empaquetado VPK, sce_sys/livearea y config usuario

`gtasa_vita/CMakeLists.txt` `[estático]`:
```cmake
set(VITA_APP_NAME "GTA: SA")
set(VITA_TITLEID  "GTASA0000")
vita_create_self(eboot.bin GTASA.elf UNSAFE)
vita_create_vpk(GTASA.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
       sce_sys/livearea/contents/startup.png ...
       sce_sys/livearea/contents/template.xml ...)
```
`template.xml` puede agregar un ítem de LiveArea que lanza la app con `-config`
(`<target>psla:-config</target>`). El `.elf` se firma `UNSAFE` para que el loader pueda hacer JIT/RWX.

**Qué aplicar en hotwheels**: el esqueleto `hotwheels-vita` (HW-005/HW-009) debe usar
`vita_create_self(... UNSAFE)` y `VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12"` (memoria extra). El layout
de datos del usuario va bajo `ux0:data/hotwheels/` con un `config.txt` estilo gtasa para remapeo de
botones y calidad (equivalente a `settings.dat` texto del juego, FINDINGS #10).

---

## 2. Render 2D con framebuffers palettizados

### 2.1 Conversión 8bpp→RGBA por CPU (LUT + escalado)

Es el camino que hotwheels necesita para el **present** de las superficies DDraw 8bpp. Referencia local
probada: D2Vita / winx86 `scale_blit`.

`d2vita/D2Vita/third_party/winx86/src/platform/present_scale.cpp:18-77` `[estático]`:
```c
inline void expand_palette(const uint8_t* palette, uint32_t out[256]) {
    for (int i = 0; i < 256; ++i) {            // Windows DIB B,G,R,0
        const uint8_t* p = palette + i * 4;
        out[i] = 0xFF000000u | (p[0]<<16) | (p[1]<<8) | p[2];
    }
}
const uint32_t sx = (srcW << 16) / r.w;        // 16.16 fixed point
for (int y = 0; y < r.h; ++y) {
    int srcy = (y * sy) >> 16;
    uint32_t* out = dst + (r.y + y) * dstPitch + r.x;
    if (srcy == prev_srcy) { memcpy(out, out - dstPitch, r.w*4); continue; } // fila duplicada
    ...
    for (; x + 4 <= r.w; x += 4) {             // unroll 4:1
        out[x+0] = pal32[row[xacc>>16]]; xacc += sx;
        ...
    }
}
```
Detalles que importan (comentados en el header): la paleta se recomputa **por frame** (256 iteraciones,
costo despreciable); el escalado es **nearest 16.16**; las filas repetidas se **memcpy** en vez de
remuestrear; el orden de operaciones es intencional y está validado con un oráculo de identidad de
píxeles (`present_scale_selftest.cpp`, con inyección de fallos bit-flip).

**NEON**: en los repos relevados **no encontramos conversión de paleta NEON** (búsqueda de
`vld1/vst1/vqtbl/__ARM_NEON` sin resultados en los ports; en winx86 aparece NEON sólo en el dynarec y
en `math_neon` para funciones trigonométricas). El camino probado es LUT escalar 4:1. Optimizar con
NEON (`vld1_u8` + `vqtbl1_u8` sobre una tabla de 256 como 16 vecinos) es una **propuesta a medir**, no
un patrón validado en estos repos.

### 2.2 Shader de paleta (lectura dependiente) vs hardware palette

D2Vita dibuja geometría palettizada con un fragment shader que hace **dos reads dependientes** (índice
→ paleta). En SGX esto serializa la unidad de textura; de ahí la variante hardware.

`d2vita/D2Vita/src/platform/vita_gxm.cpp:145-162` `[estático]`:
```glsl
float idx   = tex2D(uPage, vTex).x;
float4 texel = tex2D(uPalette, float2(idx * 0.99609375f + 0.001953125f, vPal));
if (uMode.y > 0.5f && d < 0.002f) discard;   // chromakey DESPUÉS del lookup
```
Variante hardware (`vita_gxm.cpp:215-241`): la página es `SCE_GXM_TEXTURE_FORMAT_P8_ABGR`; la paleta
viaja en el **descriptor de textura** (`sceGxmTextureSetPalette`), así que el shader hace **un solo
`tex2D`** — se elimina la lectura dependiente. Limitación anotada: **un batch no puede mezclar dos
paletas** (hay `Builder::splitByPalette`) y hay un descriptor scratch por (page, palette).

**Qué aplicar en hotwheels**: para el camino DDraw 8bpp, la página del juego (superficie 640×480×8) es
una textura `P8_ABGR`; la paleta del `IDirectDrawPalette` se sube como paleta de textura. Vitamina:
el hardware palette evita el costo del read dependiente en cada fragmento de pantalla completa.

### 2.3 vitaGL: textura palettizada nativa + escritura directa por frame

vitaGL ya implementa lo anterior (útil porque hotwheels decidió renderer **vitaGL**, PROGRESS root):

- `GL_COLOR_INDEX8_EXT` → `SCE_GXM_TEXTURE_FORMAT_P8_ABGR` (`vitaGL/source/textures.c:842`).
- `glColorTable(...)` asigna la paleta (`textures.c:2049`, `gpu_alloc_palette` en `utils/gpu_utils.c:323`).
- Al bindear, `vglSetTexPalette(&tex->gxm_tex, tex->palette_data)` (`textures.c:941`).
- `vglGetTexDataPointer(GL_TEXTURE_2D)` devuelve el **buffer mapeado de la textura** para escribir
  directo sin copia (`textures.c:2678`), y `vglOverloadTexDataPointer` para redirigirlo.
  Existe `vglGetTexPaletteDataPointer` para la paleta.

URLs:
- https://github.com/Rinnegatamante/vitaGL/blob/master/source/textures.c
- https://github.com/Rinnegatamante/vitaGL/blob/master/source/utils/gpu_utils.c

**Streaming por frame**: en vez de recrear la textura, se sube sólo la región sucia con
`glTexSubImage2D`; o mejor, se escribe directo en `vglGetTexDataPointer`. Ejemplo real de upload por
ventana: PCSX-ReARMed sube la VRAM como una textura grande y refresca sólo el rectángulo que cambió
(`pcsx_rearmed/plugins/gpu-gles/gpuTexture.c:1948` y `:3460`):
```c
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, texturebuffer);
...
glTexSubImage2D(GL_TEXTURE_2D, 0, XTexS<<1, YTexS<<1, ..., GL_RGBA, GL_UNSIGNED_SHORT, ...);
```
URL: https://github.com/libretro/pcsx_rearmed/blob/master/plugins/gpu-gles/gpuTexture.c

**Conversión al vuelo en el driver**: DaedalusX64 API-ea texturas palettizadas (CI4/CI8) convirtiendo
a RGBA **sólo cuando cambia la textura** (no cada frame) y usa `vglGetTexDataPointer` para quedarse con
el puntero y evitar re-upload (`DaedalusX64-vitaGL/Source/SysVita/Graphics/NativeTextureVita.cpp:200-264`):
```cpp
case TexFmt_CI8_8888:
  // Convert palletised texture to non-palletised...
  for (y...) for (x...) *out_ptr++ = pal_ptr[ pix_ptr[x].Bits ];
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
  mpData = vglGetTexDataPointer(GL_TEXTURE_2D);
```
URL: https://github.com/Rinnegatamante/DaedalusX64-vitaGL/blob/master/Source/SysVita/Graphics/NativeTextureVita.cpp

### 2.4 Qué aplicar en hotwheels (render 8bpp)

1. Presentar la superficie primaria/secundaria 8bpp (FINDINGS #13) como **textura `P8_ABGR`** con la
   paleta DDraw; un quad full-screen con blending off. Preferir **hardware palette** sobre shader de
   paleta dependiente.
2. Como la paleta del juego **cambia por frame** (`Palette::SetEntries`, FINDINGS #13), reusar el mismo
   slot de paleta vitaGL y re-subir 1 KB; **no** recrear la textura.
3. Como la superficie se escribe por `Lock/Unlock`/`BltFast` (software), tras el `Unlock`/`Flip` marcar
   la región sucia y hacer `glTexSubImage2D` de esa región (o escribir directo vía
   `vglGetTexDataPointer`). Empezar simple: subir 640×480×1 = 300 KB por frame y medir en Vita3K;
   sólo después optimizar a dirty-rect.
4. Tener un **fallback CPU LUT** (patrón `scale_blit`) para cuando la paleta no se pueda modelar como
   textura (p. ej. si un frame usa dos paletas distintas). En DDraw el `IDirectDrawPalette` es único
   por superficie, así que el caso normal es una sola paleta.
5. NEON: dejar como TODO medible, no asumir ganancia.

---

## 3. Emuladores/runtimes en Vita (kubridge/JIT y Vita3K)

### 3.1 DaedalusX64-vitaGL (dynarec + vitaGL)

- **JIT sin kubridge**: `CodeBufferManagerARM::Initialise()` reserva 16 MiB de VM y abre el dominio:
  `dynarec_memblock = sceKernelAllocMemBlockForVM("code", CODE_BUFFER_SIZE*2);`
  `sceKernelGetMemBlockBase(...); sceKernelOpenVMDomain();`
  y en cada bloque generado: `_DaedalusICacheInvalidate` → `sceKernelSyncVMDomain(...)`; bajo `#if 0`
  está el camino kubridge (`kuKernelAllocMemBlock` RW + `kuKernelFlushCaches` + `kuKernelMemProtect`).
  URL: https://github.com/Rinnegatamante/DaedalusX64-vitaGL/blob/master/Source/SysVita/DynaRec/CodeBufferManagerARM.cpp
- **Init gráfico** (`Source/SysVita/main.cpp:457`): `vglSetSemanticBindingMode(VGL_MODE_SHADER_PAIR)`,
  `vglInitExtended(0, SCR_WIDTH, SCR_HEIGHT, 0x1800000, msaa)`, `vglWaitVblankStart(vsync)`, y una
  proyección `glOrtho(0, 960, 544, 0, -1, 1)` para UI 2D (útil para overlays).
- Clocks: `scePowerSetArmClockFrequency(444)` etc.

### 3.2 PCSX-ReARMed (`plugins/gpu-gles`)

Modelo de render muy cercano a lo que necesita un emulador de consola: mantiene la **VRAM del guest en
memoria** y la sube a texturas OpenGL, refrescando por rectángulos con `glTexSubImage2D`; usa una
textura "windowed" (`DefineTextureWnd`/`LoadWndTexturePage`) y varias de staging (`512×512`). No usa
paleta de GPU: **ensancha 16bpp→RGBA en CPU** y sube RGBA (ver el `GL_UNSIGNED_BYTE`/`GL_RGBA` en
`gpuTexture.c`). Es el mismo trade-off que la conversión CPU `scale_blit`: simple, pero paga ancho de
banda.

### 3.3 RetroArch en Vita

- **Audio** `audio/drivers/psp2_audio.c`: driver completo `sceAudioOut` con hilo worker, ring SPSC,
  `AUDIO_OUT_COUNT 512` frames por período, `SCE_AUDIO_OUT_PORT_TYPE_MAIN`, **siempre 48000 Hz** (el
  main port rechaza otras tasas y el frontend remuestrea), `sceAudioOutOutput` bloqueante como reloj
  del hilo, y contadores de underrun. El comentario clave: *"sceAudioOut takes 16-bit PCM only; there
  is no float output"*.
  URL: https://github.com/libretro/RetroArch/blob/master/audio/drivers/psp2_audio.c
- **Video ctx** `gfx/drivers_context/vita_ctx.c`: `vglSwapBuffers(GL_FALSE)` y `vglWaitVblankStart`
  para vsync; tamaño nativo 960×544.
- **GXM viewport/aspect** `gfx/drivers/gxm_gfx.c`: `gxm_set_viewport(x,y,w,h)` con `sceGxmSetViewport`
  y escala `(sw, -sh)` (Y invertida), región de recorte por `sceGxmSetRegionClip`. Útil si se va por
  GXM directo en vez de vitaGL.

### 3.4 D2Vita / winx86 (referencia local, mismo problema que hotwheels)

- PE32/x86 real ejecutado por un runtime Win32 (loader + import bridge + dynarec derivado de Box86),
  audio `sceAudioOut` y present palettizado — el paralelo más cercano a hotwheels.
- **Audio** (`third_party/winx86/src/platform/vita_audio.cpp`): 
  - `sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM|MAIN, len, rate, SCE_AUDIO_OUT_MODE_STEREO)`.
  - Valida tasas soportadas (8000…48000) y **no remuestrea si coincide**; fallback a 22050 con
    interpolación lineal.
  - `sceAudioOutOutput` **bloquea ~23 ms** por lo que **vive en un hilo propio**, pineado (afinidad
    re-afirmada dentro del hilo), nunca en el core de los runners del guest.
  - Re-afirma el volumen cada ~430 granos; publica `rest_samples` (detección de starvation) y
    `write_errors` (si el driver rechaza, el loop pierde su único reloj y se dispara).
  URL local: `d2vita/D2Vita/third_party/winx86/src/platform/vita_audio.cpp`
- **kubridge**: D2Vita README lo recomienda (v0.3+, `ur0:tai/kubridge.skprx`) para que el pool JIT
  crezca a 32 MiB en vez del cupo VM de 16 MiB; **sin kubridge corre igual** con 16 MiB.

### 3.5 Compatibilidad con Vita3K (`[web]`)

- Vita3K **sí carga plugins taiHEN**, pero *"Plugins that use functions Vita3K does not implement
  likely will not work"* — y kubridge es justamente un plugin kernel con funciones no estándar
  (`kuKernelAllocMemBlock`, `kuKernelCpuUnrestrictedMemcpy`, …). Fuente: wiki "Using taiHEN plugins"
  de Vita3K (`[web]`, https://lavaforge.org/preserve-emulation/Vita3K/wiki/Using-taiHEN-plugins.md;
  el fetch directo devolvió 403, la cita viene del resultado de búsqueda).
- Conclusión operativa: **los so-loader ports que dependen de kubridge no son validables en Vita3K**;
  los emuladores que usan VM estándar (DaedalusX64, D2Vita sin kubridge) **sí bootean** en Vita3K.
- Para hotwheels esto refuerza la decisión de HW-009: la **validación temprana en Vita3K exige el
  camino VM sin kubridge**; kubridge se reserva para consola real (prohibida salvo orden explícita,
  AGENTS.md).

### 3.6 Qué aplicar en hotwheels

1. **JIT/RWX**: usar `sceKernelAllocMemBlockForVM` + `sceKernelOpenVMDomain`/`SyncVMDomain` (patrón
   DaedalusX64) para poder bootear en Vita3K; kubridge como fast-path opcional en consola.
2. **Audio**: copiar el diseño de hilo worker + reloj bloqueante de `vita_audio.cpp`/`psp2_audio.c`,
   no llamar `sceAudioOutOutput` desde el hilo del guest. El mixer DirectSound del juego
   (`ds_emul`) ya separa "cuánto DirectSound" de "cómo sale" — mantener esa costura.
3. **vitaGL init**: `vglInitExtended(0, 960, 544, MEMORY_VITAGL_THRESHOLD, msaa)` +
   `vglWaitVblankStart(vsync)`; reservar `glOrtho(0,960,544,0,-1,1)` para UI.
4. **Present palettizado**: ver §2.

---

## 4. Escalado 640×480 → 960×544 (aspect, filtros)

La pantalla Vita es 960×544 (16:8.8 ≈ 1.765); 640×480 es 4:3 (1.333). **No hay encaje perfecto**:
o se estira (rompe aspecto) o se hace pillarbox/letterbox.

`fit_rect` (winx86, `present_scale.cpp:80`) `[estático]`:
```c
DstRect fit_rect(int srcW, int srcH, int dstW, int dstH, bool stretch) {
    if (stretch || srcW<=0 || srcH<=0) return {0,0,dstW,dstH};
    int w = (int)(((long long)srcW * dstH) / srcH);   // probar altura completa
    int h = dstH;
    if (w > dstW) { w = dstW; h = (int)(((long long)srcH * dstW) / srcW); }
    return DstRect{(dstW - w) / 2, (dstH - h) / 2, w, h};
}
```
Para 640×480 → 960×544: `w = 640*544/480 = 725`, `h = 544`; `725 < 960` ⇒ **725×544 centrado en
x=117**, con **barras negras laterales de 117 px**. El sampler es **nearest** (16.16) con duplicación
de filas; no hay filtro lineal en este camino.

Filtros:
- D2Vita usa **POINT por default** en ambos modos de paleta; el código advierte que con *hardware
  palette* el filtro **LINEAR es correcto** (el GPU filtra colores ya resueltos), mientras que con
  *software palette* LINEAR produciría mezcla de índices (incorrecta) — y que LINEAR sobre una textura
  `P8_ABGR` **no está validado en hardware**. (`vita_gxm.cpp:507-516`).
- Como el present actual escribe las barras con `memset` y sólo escala la región del `fit_rect`
  (`vita_present.cpp:641-651`), conviene replicar: **limpiar todo el FB y escalar al rect** (si el
  rect no cubre pantalla).

**Qué aplicar en hotwheels**: para el menú DDraw 640×480, decidir política de usuario:
- default **aspect-fit 725×544** (barras laterales) — coincide con el camino probado de D2Vita;
- opción "stretch a 960×544" (llena pantalla, deforma) y opción "4:3 con barras arriba/abajo"
  (`w=960, h=720` no entra; en realidad `h = 544`, `w=725` es el fit; 4:3 exacto sería `w=725,h=544`).
- Usar `glViewport`/`vgl` con el rect y **POINT** o **LINEAR** según validación; para el juego, el
  arte original es nearest/8bpp, así que **POINT** es fiel. El upscale 640→725 es no entero (1.133×),
  así que POINT introduce aliasing; **LINEAR** puede verse mejor y acá no hay paleta dependiente en el
  present (la paleta se resuelve antes de escalar en el camino CPU). Medir A/B en Vita3K.

---

## 5. Repos clonados (shallow, `hotwheels/research/ports/`, gitignoreado)

| repo | URL | uso |
|---|---|---|
| `gtasa_vita` | https://github.com/TheOfficialFloW/gtasa_vita | so-loader + kubridge + hooks + FIOS + VPK |
| `max_vita` | https://github.com/fgsfdsfgs/max_vita | so-loader + imports biónicos + OpenAL + input |
| `soloader-boilerplate` | https://github.com/v-atamanenko/soloader-boilerplate | boilerplate CMake/VPK/config + FalsoJNI |
| `kubridge` | https://github.com/TheOfficialFloW/kubridge | API de kernel-user (hooks/RX/cache) |
| `vitaGL` | https://github.com/Rinnegatamante/vitaGL | texturas palettizadas + escritura directa |
| `DaedalusX64-vitaGL` | https://github.com/Rinnegatamante/DaedalusX64-vitaGL | dynarec + JIT VM sin kubridge + texturas |
| `pcsx_rearmed` | https://github.com/libretro/pcsx_rearmed | VRAM→textura, upload por rect, gpu-gles |
| `RetroArch` (sparse) | https://github.com/libretro/RetroArch | audio psp2 (`audio/drivers/psp2_audio.c`), ctx GXM/vitaGL |

Referencia local (no clonada): `d2vita/D2Vita` (motor + `third_party/winx86`).

---

## 6. Pendientes / evidencia faltante

- `[ ]` Medir en Vita3K: costo de subir 300 KB/frame (640×480×1) vs dirty-rect, y POINT vs LINEAR en
  el upscale 640→725. (Bloqueado por HW-009/HW-010; no hay consola.)
- `[ ]` Confirmar en fuente que `glColorTable` de vitaGL se integra sin recrear la textura al cambiar
  la paleta por frame (leer `_glTexSubImage2D`/bind path completo; hoy leído sólo el alloc/palette).
- `[ ]` NEON para `scale_blit`: propuesta, sin evidencia de ganancia en estos repos.
- `[ ]` Ver si Vita3K implementa `sceGxmTextureSetPalette`/P8 (para validar hardware palette en
  emulador); si no, el camino GPU-paleta sólo se prueba en consola.
- `[ ]` `soloader-boilerplate`: leer submodules `lib/so_util`, `lib/falso_jni`, `source/reimpl/*`
  (no clonados con `--depth 1`; requieren `--recurse-submodules`) si se necesita el detalle de
  FalsoNDK/OpenSLES.
