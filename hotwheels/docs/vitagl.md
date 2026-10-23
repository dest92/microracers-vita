# vitaGL — puesta a punto, sample FFP y prueba en Vita3K (HW-011)

- Autor: `investigador` (opencode deepseek)
- Fecha: 2026-10-02
- Rol: insumo de HW-006 (shims gráficos) y de la decisión de `docs/render-options.md`.
- Estado del sample: **VPK compilado**; **pendiente de ejecutar en Vita3K** (HW-004 no está
  listo; ver §5).

---

## 1. ¿vitaGL viene en vitasdk/vdpm? — No

`vdpm list` en este toolchain **no ofrece vitaGL** (sólo henkaku, kubridge, libk, taihen,
vitasdk-core, zlib). Por eso se compila desde fuente. Tampoco están sus dependencias
(`math-neon`, `vitaShaRK`, `SceShaccCgExt`).

Todo se integra con el script idempotente
[`hotwheels/tools/build_vitagl.sh`](../tools/build_vitagl.sh), que clona en
`hotwheels/third_party/` (gitignored, igual que `shared/` y `d2vita/D2Vita/`) e instala en
`$VITASDK/arm-vita-eabi/`.

### Repos, commits y licencias

| Componente | Repo | Commit probado | Licencia | Instala |
|---|---|---|---|---|
| **vitaGL** | Rinnegatamante/vitaGL | `c707ff00` | **LGPL-3.0** (`COPYING`) | `libvitaGL.a`, `vitaGL.h` |
| **vitaShaRK** | Rinnegatamante/vitaShaRK | `df24065e` | **MIT** | `libvitashark.a`, `vitashark.h` |
| **math-neon** | Rinnegatamante/math-neon | `0faab814` | **MIT** (expat, según README) | `libmathneon.a`, `math_neon.h` |
| **SceShaccCgExt** | bythos14/SceShaccCgExt | `fb0e9d33` | **GPL-3.0** | `libSceShaccCgExt.a`, `shacccg_ext.h` |

> Nota de licencia: además de la **LGPL-3.0** de vitaGL, la cadena de compilación de shaders
> suma **GPL-3.0** (`SceShaccCgExt`). Ambas se enlazan estáticamente. El orquestador aceptó
> LGPL para un proyecto local; conviene dejar registrado que SceShaccCgExt es copyleft más
> fuerte, por si el release cambia de licencia.

---

## 2. Cómo se construye (y el gotcha de GCC 15)

`vitaGL.h` incluye `<vitashark.h>` **incondicionalmente**, y `source/vitaShaRK.c` incluye
`<shacccg_ext.h>`: la cadena de dependencias es obligatoria, no opcional. Orden real de build:

1. `math-neon` → `make install`
2. `SceShaccCgExt` → CMake (`libSceShaccCgExt.a`)
3. `vitaShaRK` → `make install` (necesita el header del paso 2)
4. `vitaGL` → `make install HAVE_VITA3K_SUPPORT=1 HAVE_FFP_SHADER_SUPPORT=1`

**Gotcha `[estático]`:** `SceShaccCgExt` y `vitaShaRK` no compilan con el GCC 15.2 del
toolchain (`vitasdk 2026.08`): GCC ≥ 15 usa **C23** por defecto, donde una lista de parámetros
vacía `()` significa `(void)`, y el macro `TAI_CONTINUE(type, hook, args...)` de taiHEN
(que castea a `type(*)()`) falla con
`error: too many arguments to function '(void * (*)(void))cur->old'`. Solución: compilar con
`-std=gnu17` (es el mismo motivo por el que `winx86/build.sh` ya usa `-std=gnu17` para Box86).

Flags con los que se construyó vitaGL:

```
make install -j4 HAVE_VITA3K_SUPPORT=1 HAVE_FFP_SHADER_SUPPORT=1
```

- `HAVE_VITA3K_SUPPORT=1` en el Makefile de vitaGL sólo agrega `-DHAVE_VITA3K_SUPPORT`
  (que **no se usa en el código**) y `-DDISABLE_HW_ETC1` (no usar ETC1 por hardware, útil en
  emulador). Es decir: es un alias práctico de DISABLE_HW_ETC1.
- `HAVE_FFP_SHADER_SUPPORT=1` habilita los bindings GLSL 1.20 built-in
  (`gl_ModelViewProjectionMatrix`, etc.), que es lo que usa la fixed-function pipeline.

Artefactos instalados (tamaños medidos):

```
libvitaGL.a        5,955,748 B
libvitashark.a       487,862 B
libSceShaccCgExt.a    66,396 B
libmathneon.a        162,010 B
```

---

## 3. El sample FFP

`hotwheels/samples/vitagl-ffp/`:

- `main.c` — sin assets externos: genera una textura RGBA 64x64 con **borde alpha duro**
  (para que el alpha test descarte las esquinas), dibuja **un triángulo texturizado** en
  immediate-mode FFP con vértices a distinta Z, y activa **alpha test** + **fog lineal** +
  depth test. `START` sale.
- `Makefile` — compila con el toolchain, enlaza `-lvitaGL -lstdc++ -lmathneon -lvitashark
  -lSceShaccCgExt -lSceShaccCg_stub -lSceGxm_stub ...`, empaqueta el VPK. Salidas en `build/`
  (gitignored); el `.vpk`/`.elf` también están en `.gitignore`.

Build:

```
export VITASDK=~/vitasdk; export PATH=$VITASDK/bin:$PATH
hotwheels/tools/build_vitagl.sh          # una vez: instala deps + vitaGL
make -C hotwheels/samples/vitagl-ffp     # -> build/vitagl-ffp.vpk (2,274,610 B)
```

TITLE_ID del sample: `HWFFPSMPL`.

### Por qué este sample importa para el shim D3D6

Cada estado del sample mapea directo a algo del contrato de `docs/d3d6-ddraw-surface.md`:

| Sample (GL FFP) | D3D6 esperado |
|---|---|
| `glEnable(GL_ALPHA_TEST)` + `glAlphaFunc(GL_GREATER, 0.5)` | `D3DRS_ALPHATESTENABLE`/`ALPHAFUNC`/`ALPHAREF` (base del **color key** de sprites) |
| `glEnable(GL_FOG)` + `GL_LINEAR` + start/end/color | `D3DRS_FOGENABLE`/`FOGTABLEMODE`/`FOGSTART`/`FOGEND`/`FOGCOLOR` |
| `glEnable(GL_TEXTURE_2D)` + `glTexImage2D` | `IDirect3DTexture2` / `EnumTextureFormats` (565/1555/4444/8888) |
| `gluPerspective` + `glTranslatef` | `SetTransform(PROJECTION/VIEW/WORLD)` |
| `glBegin/glVertex/glEnd` | `DrawPrimitive` (immediate) |
| `glClear(COLOR|DEPTH)` | `IDirect3DViewport3::Clear` |

Si este triángulo se ve correcto (fog + alpha test) en Vita, el grueso del pipeline que
necesita Hot Wheels está cubierto por vitaGL y el shim se reduce a traducción de estados.

---

## 4. `libshacccg.suprx` — qué es y de qué depende

`[web]` vitaGL compila shaders GLSL en **runtime** usando `libshacccg.suprx`, un módulo legal
extraído del firmware (PSM Runtime) que **no se puede bundlear en el VPK**:
https://github.com/Rinnegatamante/vitaGL
Guía de extracción: https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx

`[estático]` Cadena concreta (leída en el código):

- `vitaGL/source/gxm.c:start_shader_compiler()` → `shark_init(NULL)` y, si falla,
  `shark_init("ur0:data/external/libshacccg.suprx")`.
- `vitaShaRK/source/vitashark.c:shark_init(path)` → `sceKernelLoadStartModule(path ?
  path : "ur0:/data/libshacccg.suprx", ...)`; si el load devuelve `< 0`, `shark_init` falla.
- Si `shark_init` falla, `is_shark_online = false` y vitaGL no puede compilar el shader
  fixed-function → **no arranca el render**.

Es decir: en consola hay que tener `ur0:/data/libshacccg.suprx` (o el path ScePiglet
`ur0:data/external/...`). Sin él, vitaGL no inicializa.

### 4.1 La pregunta de Vita3K

`[emulador]` El sample HW-011 cargó `libshacccg.suprx` desde `ur0:/data/` en Vita3K
(`vitagl-ffp-20261003-025129`) y `vglInit` terminó con `resolution_fallback=0`. Esto valida la
carga del módulo, pero no el compilador HLE: al primer shader FFP, Vita3K reportó
`sceShaccCgInitializeCompileOptions` y `sceShaccCgCompileProgram` como imports no implementados,
y luego leyó address 0. La presencia de nombres/strings del módulo en el binario no demuestra
que esos imports estén implementados por la versión ejecutada.

El log previo decía que bastaba instalar el `.suprx` para que las llamadas compilaran; esa
hipótesis queda refutada para el Vita3K local. Ver BUG-015 y la review HW-011.

### 4.2 Alternativa si Vita3K no puede con vitaGL

Si la hipótesis falla, hay dos caminos (documentados en `render-options.md`):

1. Bundlear **shaders precompilados** y evitar el compilador en runtime (lo que hace D2Vita con
   los `.gxp`), lo que empuja hacia el backend **GXM directo** (B).
2. Validar en **qemu-arm host-side** / consola, y dejar Vita3K sólo como smoke test.

---

## 5. Prueba en Vita3K — estado y blocker

Vita3K ejecuta el VPK y carga el módulo requerido, pero la validación FFP no pasa. El run
`build-vita3k/vitagl-ffp-20261003-025129` registra `vglInit ok (resolution_fallback=0)` y
`entering loop`, y cae al compilar el primer shader porque Vita3K no implementa los imports
ShaccCg indicados en §4.1. `shot_002.png` es negra. La re-review `95397e8` confirma el mismo
límite (`shot_008.png`, negra). Ver [BUG-015](../bugs/BUG-015-vita3k-shacccg-ffp.md).

Estado: `[x] vglInit ok` · `[ ] render FFP visible` · `[ ] START sale sin crash`.

---

## 6. Reproducir

```sh
export VITASDK=~/vitasdk
export PATH=$VITASDK/bin:$PATH
hotwheels/tools/build_vitagl.sh        # clona (commits fijos) + compila + instala
make -C hotwheels/samples/vitagl-ffp   # VPK
```

Si `git` no tiene red, el script falla en el clone; no borra lo ya presente.

---

## 7. Fuentes

- vitaGL (features/FFP, `libshacccg`): https://github.com/Rinnegatamante/vitaGL
- vitaGL licencia LGPL-3.0: `COPYING` (https://github.com/Rinnegatamante/vitaGL/blob/master/COPYING)
- vitaShaRK: https://github.com/Rinnegatamante/vitaShaRK
- SceShaccCgExt (GPL-3.0): https://github.com/bythos14/SceShaccCgExt
- math-neon: https://github.com/Rinnegatamante/math-neon
- Extracción de `libshacccg.suprx`:
  https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx
- (local) `hotwheels/docs/render-options.md`, `docs/d3d6-ddraw-surface.md`,
  `d2vita/D2Vita/src/platform/vita_gxm.cpp` (precedente `.gxp`/Vita3K)
