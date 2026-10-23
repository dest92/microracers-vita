# Vita3K en el host de desarrollo (HW-004)

- Autor: `deepseek` (opencode)
- Fecha: 2026-10-02
- Feature: `HW-004` (instalar Vita3K + firmware y validar con homebrew trivial)
- Niveles de evidencia: `[estático]` (medido local con `file`/`ldd`/hashes), `[emulador]` (Vita3K corriendo en este host)
- No-objetivo: performance, compatibilidad de juegos comerciales, shims.

---

## 0. TL;DR

1. El release **`continuous`** de `Vita3K/Vita3K` **no corre nativo** en este host: el binario oficial
   lo construye CI en **Ubuntu 26.04** y exige **glibc 2.43** + Qt 6.11; el host
   (ZorinOS 18.1 = base Ubuntu 24.04) tiene **glibc 2.39** y Qt 6.4. `[estático]`
2. Se instaló el artefacto oficial que **sí es autónomo**: `Vita3K-x86_64.AppImage` (bundlea Qt/FFmpeg),
   corriéndolo dentro de un contenedor **`ubuntu:26.04`** que replica el glibc de CI. `[emulador]`
3. Firmware PS Vita instalado: **PSVUPDAT.PUP 3.74** (main) + **PSP2UPDAT.PUP** (font), ambos oficiales de Sony. `[emulador]`
4. Validación end-to-end: se compiló un homebrew trivial con `shared/vitasdk`, se instaló y se ejecutó
   en Vita3K; el ejecutable escribió `ux0:data/hello_vita3k.txt` (log + archivo como evidencia). `[emulador]`

---

## 1. Por qué un contenedor

Medición del problema `[estático]`:

```
ldd shared/Vita3K/appdir/usr/bin/Vita3K
  ... libQt6Core.so.6: version `Qt_6.11' not found
  ... libm.so.6: version `GLIBC_2.43' not found
  ... libssl.so.3: version `OPENSSL_3.2.0' not found
host: ldd --version -> GLIBC 2.39 ; Qt6 6.4.2 ; OpenSSL 3.0.13
```

El workflow `.github/workflows/c-cpp.yml` de Vita3K confirma `runs_on: ubuntu-26.04` y `QT_VERSION 6.11.0`.
El zip `ubuntu-latest.zip` no alcanza porque necesita la Qt 6.11 del sistema; la **AppImage** es el
artefacto Linux autónomo según la wiki oficial y trae Qt 6.11 + FFmpeg adentro.

Decisión: **AppImage oficial + contenedor `ubuntu:26.04`** (Docker ya está disponible; el usuario
`dest92` está en el grupo `docker`). Ver `hotwheels/tools/vita3k/Dockerfile`.

---

## 2. Layout en disco (todo bajo `shared/`, que está gitignoreado)

```
shared/Vita3K/
  Vita3K-x86_64.AppImage     # release continuous oficial (70.216.184 bytes)
  appdir/                    # AppImage extraída (usr/lib con Qt 6.11 bundleada)
    usr/bin/Vita3K
    usr/lib/...
  firmware/
    PSVUPDAT.PUP             # main firmware 3.74 (133.834.240 bytes)
    PSP2UPDAT.PUP            # font package       (56.768.512 bytes)
  home/                      # HOME de la Vita virtual (gitignoreado, no versionar)
    .config/Vita3K/config.yml
    .local/share/Vita3K/Vita3K/{ux0,vs0,os0,sa0,...}
```

Nada de esto se commitea (firmware y binarios son material con copyright; ver `.gitignore`).

---

## 3. Pasos exactos

Wrapper: `hotwheels/tools/vita3k/vita3k.sh`.

```bash
# 1) Baja AppImage + firmware Sony, extrae, construye la imagen y instala firmware.
hotwheels/tools/vita3k/vita3k.sh setup

# 2) (idempotente) reinstalar sólo el firmware
hotwheels/tools/vita3k/vita3k.sh firmware

# 3) Abrir la GUI de Vita3K
hotwheels/tools/vita3k/vita3k.sh run

# 4) Correr una app ya instalada por TitleID
hotwheels/tools/vita3k/vita3k.sh run -r HWMR00001

# 5) Instalar+correr un vpk (path RELATIVO al repo, ver §6)
hotwheels/tools/vita3k/vita3k.sh run hotwheels/tools/vita3k/hello/build/hello.vpk
```

URLs oficiales usadas (citadas por la wiki de Vita3K / Sony):

- AppImage continuous: `https://github.com/Vita3K/Vita3K/releases/download/continuous/Vita3K-x86_64.AppImage`
- Main firmware 3.74: `http://dus01.psv.update.playstation.net/update/psv/image/2022_0209/rel_f2c7b12fe85496ec88a0391b514d6e3b/PSVUPDAT.PUP`
- Font package: `http://dus01.psp2.update.playstation.net/update/psp2/image/2019_0924/sd_8b5f60b56c3da8365b973dba570c53a5/PSP2UPDAT.PUP?dest=us`

Si se quiere correr sin el wrapper (equivale a lo que hace `run`):

```bash
docker build -t vita3k-runtime:26.04 hotwheels/tools/vita3k
docker run --rm --user "$(id -u):$(id -g)" \
  -e HOME=/vita/home -e LANG=C.UTF-8 -e APPDIR=/vita/appdir \
  -e LD_LIBRARY_PATH=/vita/appdir/usr/lib -e QT_PLUGIN_PATH=/vita/appdir/usr/plugins \
  -e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  -e DISPLAY="$DISPLAY" -e XAUTHORITY=/tmp/.Xauth \
  -v /tmp/.X11-unix:/tmp/.X11-unix -v "$XAUTHORITY":/tmp/.Xauth:ro \
  -v shared/Vita3K:/vita -v "$PWD":/work:ro -w /work \
  vita3k-runtime:26.04 /vita/appdir/usr/bin/Vita3K <args...>
```

---

## 4. Homebrew trivial (validación)

Fuente en `hotwheels/tools/vita3k/hello/` (`main.c` + `CMakeLists.txt`).
Escribe `ux0:data/hello_vita3k.txt`, imprime por `sceClibPrintf`, pinta el framebuffer y sale.

```bash
export VITASDK="$PWD/shared/vitasdk"
export PATH="$VITASDK/bin:$PATH"
cd hotwheels/tools/vita3k/hello
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build        # -> build/hello.vpk (sce_sys/param.sfo + eboot.bin)
```

TitleID de prueba: `HWMR00001`. Se instala y corre con el wrapper (`vita3k.sh run .../hello.vpk`).

### Validación con el VPK de D2Vita (pedido del review HW-004)

La feature pide validar Vita3K con un homebrew trivial **y el VPK de D2Vita**. El VPK de referencia
ya construido es `d2vita/D2Vita/build-vita/d2vita.vpk` (TITLE_ID `DTWO00001`, eboot de ~23 MB):

```bash
# instalar (content-path relativo a /work)
hotwheels/tools/vita3k/vita3k.sh run d2vita/D2Vita/build-vita/d2vita.vpk
# correr la app instalada
hotwheels/tools/vita3k/vita3k.sh run -r DTWO00001 --backend-renderer Vulkan --log-level 1
```

Resultado `[emulador]`: Vita3K **instala y arranca** el VPK. Evidencia:

```
|I| [install_archive_content]: D2Vita (App) [DTWO00001] installed successfully!
|I| [boot_game_once]: Game started: D2Vita (DTWO00001)
```

`rt_boot` corre de verdad (escribe `ux0:/data/d2vita/boot_progress.txt`: `arm444`, hilo `present`,
RTC, arena) y termina a los ~10 s porque **faltan los datos de Diablo II** (copyright, no presentes):

```
install: MANQUANT Game.exe (attendu: ux0:data/d2vita/1.14d/Game.exe)
install: MANQUANT d2data.mpq ... (12 archivos faltantes)
|I| [on_game_closed]: Game closed: D2Vita
```

O sea: la validación de **instalación + arranque** en Vita3K es exitosa; el juego no continúa por
ausencia de datos, que es el comportamiento esperado en este entorno de desarrollo.

---

## 5. Evidencia (Vita3K v0.2.1 4115-a366df69) `[emulador]`

Fragmentos reales del log del último run exitoso:

```
|I| [boot_game_once]: Game started: HW Hello Vita3K (HWMR00001)
|I| [export_sceIoOpen]: Opening file: ux0:data/hello_vita3k.txt
|I| [export_sceClibPrintf]: [hello_vita3k] escrito ux0:data/hello_vita3k.txt (fd=6)
```

Firmware instalado:

```
|I| [extract_pup_files]: Firmware Version: 0x3740000  Build Number: 571880  Number Of Files: 36
|I| [extract_pup_files]: Firmware Version: 0x3730000  Build Number: 571652  Number Of Files: 18
|I| [operator()]: Firmware installation progress: 100%
```

Contenido del archivo escrito (en `shared/Vita3K/home/.local/share/Vita3K/Vita3K/ux0/data/`):

```
hello_vita3k: homebrew ejecutado desde vitasdk
```

Hashes (para reproducibilidad):

| artefacto | sha256 |
|---|---|
| `Vita3K-x86_64.AppImage` | `ffce3720027ff8cee3505e25b6264eda12c6457f7ba31cecae26a90f87caf48e` |
| `PSVUPDAT.PUP` | `6ef6dc8da6db026f28647713e473486d770087a605c52a8d751bfca7478386cf` |
| `PSP2UPDAT.PUP` | `f372d306a7f2711125bea29aa75cbcb1eb19724e9648478fba07b8d655b37344` |

Notas no fatales del entorno: sin `/dev/dri` el render usa **lavapipe** (Vulkan software) y el audio
ALSA/Pulse falla; el homebrew igual corre y renderiza.

---

## 6. Quirks y bugs encontrados (release continuous 4115-a366df69)

1. **glibc/Qt**: ver §1. El binario del zip no sirve con la Qt del sistema; usar la AppImage en `ubuntu:26.04`.
2. **Crash de CLI en primer arranque**: con `config.yml` nuevo, `pref-path` queda vacío y
   `fs::create_directories(cfg.get_vita_fs_path())` tira `filesystem_error: Invalid argument`.
   Workaround: pre-sembrar `pref-path: /vita/home/.local/share/Vita3K/Vita3K` en `config.yml`.
   Lo hace `seed_config` del wrapper.
3. **Modales que bloquean sin usuario**: con `show-welcome: true` el `WelcomeDialog` se abre
   antes del event loop y cuelga. `seed_config` pone `show-welcome: false`,
   `check-for-updates: false`, `warn-missing-firmware: false`.
4. **Content-path posicional roto con rutas absolutas**: `app.allow_windows_style_options()`
   hace que un argumento que empieza con `/` se interprete como opción y se descarte en silencio
   (por eso `allow_extras`). Pasar el vpk como **ruta relativa** a `/work` (el repo).
   Ej: `vita3k.sh run hotwheels/tools/vita3k/hello/build/hello.vpk`.
5. `--backend-renderer OpenGL` requiere GL 4.4; sin GPU se usa Vulkan/lavapipe, que funciona.

---

## 7. Reproducir desde cero

```bash
hotwheels/tools/vita3k/vita3k.sh setup     # descarga, extrae, imagen, firmware
hotwheels/tools/vita3k/vita3k.sh run --version   # smoke test -> "Vita3K v0.2.1 4115-a366df69"
```

`init.sh` verifica la presencia del AppImage y de ambos PUPs.

---

## 8. Referencias

- Wiki oficial (instalación Linux / AppImage y links de firmware): https://github.com/Vita3K/Vita3K/wiki/Usage
- Release continuous: https://github.com/Vita3K/Vita3K/releases/tag/continuous
- Firmware Sony PS Vita (3.74): https://www.playstation.com/en-us/support/hardware/psvita/system-software/
- Vita3K: https://vita3k.org

## 9. Actualización del emulador: continuous 2026-10-03 vs v0.2.1-4115 (qa, 2026-10-04)
- **Instalación**: nuevo AppImage `continuous` (build 2026-10-03T08:25Z, 70 232 568 B) en `shared/Vita3K/Vita3K-x86_64.AppImage` + `appdir/` extraído (default del harness). El anterior se conserva como `Vita3K-x86_64.v0.2.1-4115.AppImage` y `appdir.v0.2.1-4115/`; para correr el viejo hay una instancia aislada `shared/Vita3K-v0.2.1-4115/` (appdir+home+firmware copiados, 3,3 GB): `VITA3K_HOME=$PWD/shared/Vita3K-v0.2.1-4115 tools/vita3k_run.sh …`.
- **glibc**: ambos binarios (viejo y nuevo) requieren `GLIBC_2.43` → sólo corren en el contenedor ubuntu 26.10 (`vita3k-headless:26.10-xtest2`); el host (2.39) no puede ejecutarlos directamente. Verificado con `strings | grep GLIBC` y con el intento de `--help` en el host.
- **Updates**: clave de config `check-for-updates-mode: 0` (nueva; la vieja `check-for-updates: false` ya no aplica) en `shared/Vita3K/home/.config/Vita3K/config.yml` y en la copia vieja; el harness (`vita3k_run.sh`) además la fuerza a 0 en cada corrida. Limpié una línea corrupta literal (`...\ncheck-for-updates-mode: 0\n`) que había quedado al final del YAML. Hay red disponible en el contenedor; no se verificó con captura de tráfico que no haya consulta (nivel: config, no tráfico).
- **Validación con el mismo VPK** (`build-vita/hotwheels-vita.vpk` de 17:11, 1 922 392 B; guion `tools/race_enter.script` por `HW_INPUT_SCRIPT` — **el harness aún no pasa esa variable al contenedor**: se probó con una copia temporal con `-e HW_INPUT_SCRIPT -e HW_QPC_IDLE_US`; render debería incorporarlo): 75 s, boot → menú → carrera D3D (escena ≈1700), `DDraw SetDisplayMode 800x600x16 accepted`, **0 crash** en ambos, 23 capturas c/u (`docs/img/vita3k-{old-v0.2.1-4115,new-continuous-20261003}-race.png`).
- **A/B (n=1 por emulador, nivel Vita3K headless/lavapipe, host cargado por otros agentes → NO concluyente)**, última ventana `fps-window mode=pipeline` de 10 s:

| | guest_d3d_hz | consumer_hz | min_consumer_1s | dropped (acum.) | uploads/frame | interp60 avg_hz |
|---|---|---|---|---|---|---|
| v0.2.1-4115 | 30,3 | 30,1 | 26,0 | 13 | 0 | 1,88 |
| continuous 2026-10-03 | 26,5 | 26,5 | 9,0 | 21 | 0 | 2,24 |

- Lectura: sin regresiones funcionales (arranca, menú, carrera, misma resolución, mismo conteo de uploads); en esta muestra el nuevo da ~13 % menos Hz y mínimos de 1 s más bajos, pero la RAM estaba al límite (swap/contención con otro Vita3K) y n=1; un A/B real requiere ≥3 patas intercaladas con el lock `out/VITA3K_BUSY`. Decisión: **nuevo = default** (no rompe nada); si el A/B repetido confirma una caída, volver al viejo con `mv appdir appdir.new && mv appdir.v0.2.1-4115 appdir`.
- Reglas operativas: un solo Vita3K a la vez (lock `hotwheels/out/VITA3K_BUSY`); al terminar no queda ningún Vita3K/contenedor corriendo (verificado con `pgrep`/`docker ps`).
