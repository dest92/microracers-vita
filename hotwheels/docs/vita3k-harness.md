# Harness automático de Vita3K (HW-014)

- Autor: `investigador` (opencode, deepseek-flash)
- Fecha: 2026-10-02
- Feature: `HW-014` (M1). Se apoya en `HW-004` (Vita3K + firmware, de `deepseek`).
- Niveles de evidencia: `[emulador]` (corridas reales en este host), `[estático]` (CLI/strings).

---

## 0. TL;DR

`hotwheels/tools/vita3k_run.sh <vpk> [segundos]` corre un `.vpk` en Vita3K **sin humano y sin
tocar el escritorio**: lo instala, lo corre bajo un **Xvfb :98 dentro del contenedor**, captura
**stdout del emulador** (incluye `sceClibPrintf`) y **screenshots** periódicos, y mata todo al
timeout. Artefactos en `hotwheels/build-vita3k/<vpk>-<fecha>/` (gitignored por `build*/`).

Validado con **tres** VPKs:
- `hotwheels/tools/vita3k/hello/build/hello.vpk` → boot + `sceClibPrintf` + screenshots. `[emulador]`
- `hotwheels/build-vita/hotwheels-vita.vpk` (runtime de `impl`, HW-009) → boot + `trap #1`
  `KERNEL32.dll!GetVersion` (§4.1). `[emulador]`
- `hotwheels/samples/vitagl-ffp/build/vitagl-ffp.vpk` → **bootea pero `vglInit FAILED`**: Vita3K
  no tiene el compilador de shaders de vitaGL (§5). `[emulador]`

---

## 1. CLI real de Vita3K (v0.2.1 4115-a366df69)

Verificada con `--help` (hay que darle un display; con `QT_QPA_PLATFORM=offscreen` no arranca
porque el bundle sólo trae el plugin `xcb`):

```
Vita3K [OPTIONS] [content-path]
  [content-path]              .vpk/.zip o carpeta  -> instalar & correr
  -r, --installed-path TEXT   correr app instalada por TitleID
  --firmware TEXT             instalar firmware (.pup)
  -B, --backend-renderer {OpenGL,Vulkan}
  -l, --log-level 0..6        (0=TRACE .. 6=OFF)
  -z, --console [0]           modo consola
  -f, --load-config PATH      -c, --config-location PATH
  -A, --archive-log           duplica el log con TITLE_ID
  -S, --self PATH             correr un self dentro del TitleID
  --pkg/--zrif, -d/--deleted-id, -s/--recompile-shader, -v/--version, -h/--help
```

**No existen `--installer/-i` ni `--no-gui`.** Para instalar se usa el **content-path
posicional**; para correr, `-r TITLEID`. Quirk de deepseek confirmado: un content-path
**absoluto** se descarta (CLI11 `allow_windows_style_options`), por eso el harness pasa el vpk
**relativo** a `/work`.

---

## 2. Diseño

Dos capas Docker (no se toca la imagen ni el wrapper de deepseek):

1. `vita3k-runtime:26.04` — la de `deepseek` (`hotwheels/tools/vita3k/Dockerfile`), sobre
   `ubuntu:26.04` para tener **glibc 2.43** (el binario oficial no corre nativo en el host: 2.39).
2. `vita3k-headless:26.04` — capa propia (`hotwheels/tools/Dockerfile.vita3k-headless`) que sólo
   agrega **`xvfb`, `x11-utils`, `x11-apps`, `imagemagick`, `procps`**.

Flujo de `vita3k_run.sh`:
1. Deriva `TITLEID` del `sce_sys/param.sfo` del VPK (parser SFO en Python; o `TITLEID=` env).
2. Construye la capa headless si falta; exige la imagen base (si no, pide `vita3k.sh setup`).
3. Escribe `inner.sh` en la carpeta de salida y lanza un contenedor **detached** con
   `VDIR:/vita` (rw), `repo:/work:ro`, `out:/out`.
4. `inner.sh` (dentro del contenedor): arranca `Xvfb :98 -screen 0 1280x720x24`, hace una
   **pasada de instalación** (content-path posicional) hasta ver `installed successfully`, y una
   **pasada de ejecución** `-r TITLEID` que saca un screenshot por segundo con
   `import -window root`.
5. El host espera el contenedor y hace `docker rm -f` **garantizado** (trap + deadline
   `segundos+90`), así no queda nada vivo.

Detalles que fueron necesarios:
- **`stdbuf -oL -eL`**: el log de Vita3K va a stdout, pero con stdout redirigido a archivo queda
  *block-buffered*; sin `stdbuf` se pierden `boot_game_once`/`sceClibPrintf` al matar el proceso.
- **Dos pasadas**: en headless, el `content-path` posicional **instala pero no auto-bootea**; el
  boot determinista es `-r TITLEID`.
- **Renderer**: `backend-renderer: OpenGL` en el `config.yml`, servido por **llvmpipe**
  (Mesa 26.0.8, GL 4.6). Sin GPU dedicada; no es referencia de performance.
- **Fix de layout AppImage**: Vita3K busca `shaders-builtin/` junto al binario
  (`.../usr/bin/`), pero el AppImage lo trae en `.../usr/share/Vita3K/`. El harness crea el
  symlink idempotente. Sin esto: `Couldn't compile essential shaders for rendering. Exiting`.

---

## 3. Uso

```bash
# instala + corre 20 s, saca screenshots y guarda el log
hotwheels/tools/vita3k_run.sh hotwheels/tools/vita3k/hello/build/hello.vpk 20

# con un módulo libshacccg.suprx real, provisto por el usuario (no se distribuye)
SHACCCG=/ruta/libshacccg.suprx hotwheels/tools/vita3k_run.sh <vpk> 30

# TitleID forzado (si el SFO no lo trae)
TITLEID=HWFFPSMPL hotwheels/tools/vita3k_run.sh <vpk> 30

# con datos propios del homebrew: STAGE=<dir> se copia a ux0:data/ antes de correr
mkdir -p /tmp/stage/hotwheels && cp HotWheels.exe /tmp/stage/hotwheels/
STAGE=/tmp/stage hotwheels/tools/vita3k_run.sh hotwheels/build-vita/hotwheels-vita.vpk 30
```

Salida (`hotwheels/build-vita3k/<vpk>-<timestamp>/`):
- `install.stdout` — log de la pasada de instalación (`installed successfully`).
- `run.stdout` — log de la pasada de ejecución: `boot_game_once`, `sceClibPrintf`, errores `|E|`.
- `shot_000.png`, `shot_001.png`, … — un screenshot por segundo del display virtual.
- `inner.sh`, `xvfb.log`, `vita3k.log` (copia; suele estar vacío, stdout es la fuente).

El script imprime un resumen: líneas de boot, líneas de `sceClibPrintf`, cantidad de errores y
de screenshots, y el tail de `boot/print`.

---

## 4. Validación con el homebrew trivial `[emulador]`

```
== corriendo HWMR00001 (hotwheels/tools/vita3k/hello/build/hello.vpk) por 14s
== boot:  1 líneas
== print: 1 líneas
== screenshots: 15
[..] |I| [boot_game_once]: Game started: HW Hello Vita3K (HWMR00001)
[..] |I| [export_sceClibPrintf]: [hello_vita3k] escrito ux0:data/hello_vita3k.txt (fd=6)
```

El screenshot muestra la ventana de juego (área negra 960x544) junto a la GUI de Vita3K, y el
panel de log con la línea de `sceClibPrintf`. El app además escribe
`ux0:data/hello_vita3k.txt` (evidencia en el FS virtual).

### 4.1 Runtime de `impl` (HW-009) `[emulador]`

El VPK `hotwheels/build-vita/hotwheels-vita.vpk` (TITLEID `HWMR00002`) espera
`ux0:data/hotwheels/HotWheels.exe`; se estageó con `STAGE=`:

```bash
mkdir -p /tmp/stage/hotwheels && cp hotwheels/extracted/original/Program_Files/HotWheels.exe /tmp/stage/hotwheels/
STAGE=/tmp/stage hotwheels/tools/vita3k_run.sh hotwheels/build-vita/hotwheels-vita.vpk 30
```

Resultado: `[boot_game_once]: Game started: HotWheels Vita (HWMR00002)` y, en
`ux0:data/hotwheels/boot.log`, exactamente lo que reportó `impl`:

```
[hw] layout exe 0x00400000..0x00479000 span=0x0c400000 arena=0x0d400000
[hw] kubridge: AUSENTE (piscina JIT 16 MiB, sin handler de faltas)
[hw] base=0x00400000 entry=0x00452f05 imports=94 sin_shim=94 stack_top=0x0b200000
[hw] trap #1 KERNEL32.dll!GetVersion (ret=0x00452f31)
[hw] parada controlada: import sin shim
[hw] fin: shutdown requested, traps=1, exit=0x00000000
```

Nota: este VPK loguea a **sus propios archivos** (`progress.txt`/`boot.log` en `ux0:data/`),
no por `sceClibPrintf`, así que el resumen del harness muestra `print: 0`; la evidencia se lee
de esos archivos. Los `|E|` que sí aparecen en `run.stdout` (falta `env.txt`,
`/proc/self/maps`, `_vshKernelSearchModuleByName`) son ruido no fatal de Vita3K.

---

## 5. Validación con el sample vitaGL FFP — hallazgo `libshacccg` `[emulador]`

```
== corriendo HWFFPSMPL (hotwheels/samples/vitagl-ffp/build/vitagl-ffp.vpk) por 20s
== boot:  1 líneas   == print: 3 líneas   == errores: 3
[..] |I| [boot_game_once]: Game started: vitagl-ffp (HWFFPSMPL)
[..] |I| [export_sceClibPrintf]: [vitagl-ffp] start
[..] |I| [load_module]: Loading module "ur0:/data/libshacccg.suprx"
[..] |E| [load_module]: Missing file at data/libshacccg.suprx ...
[..] |I| [load_module]: Loading module "ur0:data/external/libshacccg.suprx"
[..] |E| [load_module]: Missing file at data/external/libshacccg.suprx ...
[..] |I| [export_sceClibPrintf]: [vitagl-ffp] vglInit FAILED
[..] |E| [operator()]: _sceKernelLockLwMutex returned SCE_KERNEL_ERROR_UNKNOWN_LW_MUTEX_ID
```

Conclusiones (todas medidas):
1. **vitaGL no inicializa en Vita3K sin `libshacccg.suprx`.** `vitaShaRK::shark_init` carga
   `ur0:/data/libshacccg.suprx`; si falta, `shark_init` falla y `vglInit` devuelve false (y el
   proceso además crashea con un mutex inválido en el camino de fallo).
2. Un **archivo dummy vacío** en `ur0:/data/libshacccg.suprx` **no alcanza**: Vita3K responde
   `Failed to read module file` (no es un SELF válido).
3. **La HLE de `SceShaccCg` de Vita3K está incompleta.** Parcheé *experimentalmente* nuestro
   `vitaShaRK` vendoreado para tolerar el módulo ausente; entonces Vita3K imprimió
   `Unimplemented sceShaccCgSetDefaultAllocator import called` /
   `Unimplemented sceShaccCgInitializeCallbackList import called` y `vglInit` igual falló.
   El parche se revirtió (no aporta y cambiaría el comportamiento en consola real).

**Implicación para la decisión de renderer (`docs/render-options.md`):** si el gate M3 (primer
frame) tiene que pasar en **Vita3K**, vitaGL **no** sirve en este emulador (sin el módulo real de
shacccg del firmware, que no se puede bundlear). El backend **GXM con shaders `.gxp`
precompilados** (opción B) evita el compilador en runtime y es el camino para ese gate. Con un
`libshacccg.suprx` real provisto por el usuario (`SHACCCG=`), vitaGL podría probarse igual.

---

## 6. Bugs/quirks encontrados (para deepseek / HW-004)

1. **`shaders-builtin` mal ubicado en el AppImage**: el binario busca
   `<exe_dir>/shaders-builtin/`, el AppImage lo pone en `<appdir>/usr/share/Vita3K/`. Sin el
   symlink, el screen renderer no compila y `Vita3K` sale con
   `Couldn't compile essential shaders for rendering. Exiting`. **Sugerencia**: agregarlo a
   `vita3k.sh setup` (o al Dockerfile base) con `ln -sfn ../share/Vita3K/shaders-builtin
   appdir/usr/bin/shaders-builtin`. El harness lo hace por su cuenta, pero conviene arreglarlo
   en la base.
2. **El auto-boot headless no dispara**: `content-path` posicional instala y loguea
   `will auto-boot`, pero no arranca el juego sin interacción. Usar `-r TITLEID` (dos pasadas).
3. **Falta Xvfb/ImageMagick** en la imagen base; la capa headless los agrega.
4. **`vita3k.log` queda vacío**; el log real va a **stdout** (usar `stdbuf`).

---

## 7. Fuentes

- `hotwheels/tools/vita3k/` (base de deepseek: `vita3k.sh`, `Dockerfile`, firmware) y
  `hotwheels/docs/vita3k.md`.
- `hotwheels/tools/vita3k_run.sh`, `hotwheels/tools/Dockerfile.vita3k-headless` (este harness).
- CLI de Vita3K: `Vita3K --help` (v0.2.1 4115-a366df69).
- `docs/vitagl.md` §4 (cadena `libshacccg`/vitaShaRK/vitaGL) y `docs/render-options.md`.
