# Hot Wheels: Micro Racers → PS Vita — Investigación de antecedentes

- Autor: `investigador` (opencode deepseek)
- Fecha: 2026-10-02
- Método: búsqueda web (Brave Search) + corroboración estática sobre el binario local
- Niveles de evidencia: `[estático]` (medido acá con `file`/`strings`/parseo PE), `[web]` (fuente citada)
- Alcance: (1) el juego, (2) precedentes de ports a Vita de exes Win32, (3) límites de Vita3K.
  No incluye trabajo de código ni extracción del instalador (va a HW-001/HW-002).

---

## 0. TL;DR — dos hallazgos que cambian el punto de partida

1. **El archivo `Hot Wheels® - Micro Racers.exe` (224 MB) NO es el juego: es un bootstrapper/repack `[estático]`.**
   Es PE32 GUI i386 de 5 secciones (`SizeOfImage = 0x204000`, `end of sections ≈ 0x1FFE00`, o sea ~2 MB)
   con **~222 MB de overlay** (`222,334,376` bytes = `filesize − 0x1FFE00`) que contienen el payload
   del instalador. Medido parseando las cabeceras PE `[estático]`:
   - Tabla de import del PE externo: **solo `KERNEL32.dll`, 165 funciones** (p. ej. `FindResourceW`,
     `LoadResource`, `CreateProcessW`, `CreateNamedPipeW`, `GetPrivateProfileStringW`, `LoadLibraryExW`).
     Es el perfil típico de un **stub instalador**; **no** es el import set del juego.
   - Es un **Advanced Installer** embebido: strings de tablas MSI (`InstallExecuteSequence`, `Feature`,
     `Component`, `File`, `Signature`…), acciones `AI_*`, `lzmaextractor.dll`, `Prereq.dll`, `aicustact.dll`,
     `Uninst.isu`. Producto `1.0.0.1`, manufacturer *"Mattel Interactive; Modern Installer by Bladez1992"*.
   - **El juego real viaja como imagen de CD: `HWMR.iso`.** El instalador trae un *"CD Launcher by
     Bladez1992"*: `HWMR.bat` y `HWMR.ps1` hacen `Mount-DiskImage -ImagePath "[APPDIR]ISO\HWMR.iso"`
     y luego `Start-Process "[APPDIR]HotWheels.exe" -Wait`. O sea: **el `.exe` del juego a analizar es
     `HotWheels.exe` (dentro del ISO)**, versión de archivo `4.6.2.436`.
   - **Wrappers/parches modernos** que acompañan al juego: `dgVoodooCpl.exe` (v2.8.1.3) + `dgVoodoo.conf`,
     `D3D8.dll`, `D3D9.dll`, `DDraw.dll`, y el parche de audio CD `_inmm.dll` (v2.3.0.8) + `_inmm.ini`.
   - Assets listados en la tabla de features (formato de datos del juego): `*.TGA`, `*.SFP`, `*.FSS`,
     `*.SIN`, `*.FRM`, `*.BMP`, `*.WAV`, `*.DAT` (`PHYSICS.DAT`, `Ai.dat`, `highscore.dat`, `ACC.DAT`,
     `settings.dat`), `*.TAB` (`SHADE.TAB`), `LANG.TXT`, `TRANSLAT.FRM`, `HotWheels.exe`, `HWMR.exe`.
   → Consecuencia: el triage `HW-001` analizó un **instalador**, no el ejecutable del juego.
   Ruta de extracción: desempaquetar el bootstrapper → obtener `ISO/HWMR.iso` → extraer/montar el ISO
   (es un CD) → `HotWheels.exe` + sus datos. La fecha 2023 no es la del juego: es la del **repack**
   (mtime del archivo 2023-10-26).

2. **El juego original es de 2000 y usa Direct3D 6 / DirectDraw, no Unity ni un motor licenciado.**
   `[web]` PCGamingWiki: *"the game loads d3dim.dll… dgVoodoo2's traces point to Direct3D 6
   (e.g: 'Direct3DDevice3' and 'DirectDrawSurface4')"*. El repack incluye justamente dgVoodoo2
   `[estático]`, lo que corrobora el dato. Para el port esto es **buena noticia relativa**: D3D6
   *immediate mode* (`IDirect3DDevice3` / `DrawPrimitive`) es una superficie mucho más chica que
   Glide3x o D3D9; el shim D3D6→sceGxm es factible.

---

## 1. El juego (ficha e ingeniería)

### 1.1 Identidad
- **Desarrollador:** Unique Development Studios (UDS), estudio sueco **fundado en 1993**,
  con sedes en Norrköping y Gotemburgo, **quebrado en 2004**. Otros títulos: *Ignition* (1997),
  *Airfix: Dogfighter* (2000).
- **Publishers:** Mattel Interactive; también THQ.
- **Fecha:** 28 de abril de 2000 (NA). Anunciado en el Toy Fair de febrero de 2000.
  Productor: Jonathan Correa; música: Christian Björklund.
- **Plataforma:** **solo Microsoft Windows** (Windows-only). No hay versión de consola ni mobile.
- **Género:** racing arcade top-down, 1–2 jugadores (split-screen), 3 pistas (oficina, cuarto, playa).
- Fuentes:
  - https://en.wikipedia.org/wiki/Hot_Wheels_Micro_Racers
  - https://vgtimes.com/games/hot-wheels-micro-racers/
  - https://oldgamesdownload.com/hot-wheels-micro-racers/
  - https://archive.org/details/HotWheelsMicroRacersUSA

### 1.2 Motor (engine)
- **Motor propio de UDS.** No es Unity, Unreal ni middleware licenciado.
- `[web]` Para el antecesor **Ignition (1997)**, MobyGames documenta un motor propio llamado **LISA**
  (nombrado por la gata de un integrante del equipo): https://www.mobygames.com/game/807/ignition/
- Hot Wheels: Micro Racers se describe repetidamente como "similar a *Ignition*, el juego anterior de UDS".
  Es **probable** que use una evolución del motor LISA, pero **no encontré fuente que lo confirme para HWM:R**.
  Nivel: inferencia `[web]` — a verificar con RE (strings de `SHADE.TAB`, `LANG.TXT`, `TRANSLAT.FRM` que
  aparecen en el repack `[estático]`).
- Fuente: https://archive.org/details/HotWheelsMicroRacersUSA

### 1.3 API gráfica / audio / input / red
- **Gráficos:** Direct3D **6** en modo immediate (`d3dim.dll`) + DirectDraw (`DDraw.dll`).
  `[web]` https://www.pcgamingwiki.com/wiki/Hot_Wheels:_Micro_Racers
- **Repack `[estático]`**: además trae dlls de compatibilidad `D3D8.dll`, `D3D9.dll`, `DDraw.dll`
  y `dgVoodooCpl.exe` → dgVoodoo2 (D3D3/5/6/7 + DDraw → D3D11/12 en Windows).
  `[web]` https://dege.freeweb.hu/dgVoodoo2/
- **Audio:** DirectSound (`DSETUP.DLL`, `DSETUP16.DLL`, `DSETUP32.DLL`) y **audio de CD** (pistas
  `Track1.wav`), con parche `_inmm` para redirigir CD-audio a WAV/MP3.
  `[web]` https://community.pcgamingwiki.com/files/file/2656-_inmm-238/
- **Input/red `[estático]` (corrección):** los nombres `WS2_32.dll`, `IPHLPAPI.DLL`, `GDI32.dll`,
  `USER32.dll`, `OLEAUT32.dll`, `COMCTL32.dll`, `MSVCRTD.DLL`, `lzmaextractor.dll` aparecen como
  **strings/componentes del instalador y su payload**, **no** como import table del PE externo.
  La import table real del `.exe` de 224 MB es **solo `KERNEL32.dll` (165 funciones)**.
  El set de imports del **juego** (red, audio, input, gráficos) hay que leerlo de `HotWheels.exe`
  una vez extraído del ISO. Históricamente cabe esperar `DDRAW`/`D3DIM`, `DSOUND`, `USER32`/`GDI32`,
  y — para el multijugador — `WS2_32`/`IPHLPAPI`.
- **Asset formats conocidos `[web]`:** TCRF menciona archivos no comprimidos dentro de los datos
  (`SILENCE.WAV`, `SETUP.CFG`). En la tabla de features/strings del repack `[estático]` aparecen
  además: imágenes `*.TGA` (`CREDITS0/1/2.TGA`, `INTRO.TGA`, `OFFICE.TGA`, `MAINMENU.TGA`, `WAVE.TGA`,
  `MATTEL.TGA`, `UDS.TGA`, `curtain_*.tga`, `split_*.tga`) y `*.BMP`; audio `*.WAV` (efectos y pistas
  `Track1..4.wav`); datos binarios `*.DAT` (`PHYSICS.DAT`, `Ai.dat`, `highscore.dat`, `ACC.DAT`,
  `settings.dat`, `SHOCK.BIN`); **formatos propios del engine**: `*.SFP` (`CARS.SFP`, `VEHICLES.SFP`,
  `OBJECTS.SFP`, `SLUMBER.SFP`, `WAVE.SFP`), `*.FSS` (slideshows `SLID*.FSS`), `*.SIN` (`ASIN.SIN`,
  `SINUS.SIN`), `*.FRM` (diálogos/UI localizados `ENGREG*.FRM`, `PRINT.FRM`, `FTP.FRM`, `TRANSLAT.FRM`),
  y tablas de texto `SHADE.TAB`, `LANG.TXT`, `manual.txt`, `hw_medium.txt`. **No hay formato de modding
  documentado públicamente.**
  Fuente: https://tcrf.net/Hot_Wheels_Micro_Racers

### 1.4 Requisitos
- Original 2000: PC clase Pentium II, Windows 95/98, GPU con aceleración 3D (D3D).
  Ficha general: https://www.pcgamingwiki.com/wiki/Hot_Wheels:_Micro_Racers (bloqueó el fetch directo;
  datos vía snippet de búsqueda). Reseña contemporánea: https://www.gamespot.com/reviews/hot-wheels-micro-racers-review/1900-2574984/
- El repack moderno apunta a Windows 7+ (usa dgVoodoo2 + `_inmm`).

### 1.5 Versiones de consola / mobile
- **No existen para Micro Racers.** La franquicia Hot Wheels sí tuvo ports (Turbo Racing en PS1/N64,
  Extreme Racing en GBA, etc.), pero **no este título**. Micro Racers quedó Windows-only y hoy es
  abandonware (no se vende; hay una entrada de "Dreamlist" en GOG sin materializar).
  - https://www.gog.com/dreamlist/game/hot-wheels-micro-racers-2000
  - https://oldgamesdownload.com/hot-wheels-micro-racers/

### 1.6 Distinción importante sobre "2023"
- El juego es **2000**. Lo que el pedido etiqueta como "2023" es la **fecha del archivo/repack**
  (`[estático]` mtime 2023-10-26) o de una subida a archive.org `[web]`
  (https://archive.org/details/26413-0919_202205). No hay un HWM:R edición 2023.
- Cuidado con confundirlo con **Hot Wheels Unleashed** (Unreal Engine, 2021) — ese sí es moderno y de otro estudio.

---

## 2. Precedentes de ports a PS Vita (exe Win32 / binarios ajenos)

### 2.1 Correr el exe original Win32/x86 (la estrategia del proyecto)
- **D2Vita (Diablo II: LoD 1.14d en Vita)** — referencia directa. Estrategia: *"run the genuine
  Diablo II LoD 1.14d `Game.exe` through a Win32/x86 runtime (PE loader + import bridge + an x86→ARMv7
  dynarec derived from Box86), shimming only the OS layer"*. Target 960×544.
  - https://github.com/Franckrst/D2Vita
  - https://www.gamebrew.org/wiki/D2Vita
  - FPS reportados ~10–25 al inicio de partida mientras se llena caché del dynarec:
    https://www.reddit.com/r/VitaPiracy/comments/1wj8dvz/d2vita_is_here_diablo_ii_lord_of_destruction/
- **WinVita** — extracción genérica del runtime D2Vita (PE32 loader en `src/runtime/pe_image.*`,
  dynarec x86→ARMv7 derivado de Box86 en `third_party/box86-dynarec/`, capa de integración en
  `src/dynarec86/`). Es exactamente el "winx86" genérico que este monorepo ya usa.
  - https://github.com/Franckrst/WinVita
  - (local) `d2vita/D2Vita/third_party/winx86/`
- **kubridge** — plugin kernel que agranda el pool JIT (16 MiB → 32 MiB) y arma fault handler.
  D2Vita lo recomienda (opcional, con fallback a 16 MiB). Relevante para cualquier runtime con JIT.
  - https://github.com/TheOfficialFloW/kubridge
  - https://www.gamebrew.org/wiki/Kubridge_Vita

### 2.2 box86 / box64 (x86 en ARM por dynarec)
- **box86** (ptitSeb): emulador de userspace x86 para ARM Linux, con DynaRec "5–10× más rápido que
  interp`. **No hay port oficial de box86 a Vita** (Vita no corre Linux con glibc de forma práctica);
  el puente es que D2Vita/WinVita **derivaron su dynarec de Box86** para ARMv7.
  - https://github.com/ptitSeb/box86
  - https://ptitseb.github.io/box86/
  - https://en.wikipedia.org/wiki/Box86

### 2.3 `.so` loader / Android→Vita (binarios ARM, no x86 — técnica distinta pero análoga)
- **TheOfficialFloW / so-loader**: carga `.so` ARMv7 de Android, resuelve imports y parchea;
  usado para GTA:SA, Max Payne, Fahrenheit, This War of Mine, Crazy Taxi, etc.
  - https://github.com/TheOfficialFloW/gtasa_vita
  - https://github.com/TheOfficialFloW/fahrenheit_vita
  - Lista de candidatos/requisitos: https://github.com/Rinnegatamante/Android2Vita-Candidate-Ports-List
- **soloader-boilerplate** (v-atamanenko): plantilla base para ports Android→Vita, usada por decenas
  de ports (Zenonia 2/3, Inotia, Asphalt 5, HOMM3 HD, LEGO SW…).
  - https://github.com/v-atamanenko/soloader-boilerplate
- **Diferencia clave:** so-loader = binarios **ARM** ya compilados para Android (casi nativo);
  nuestro caso = binarios **x86** que necesitan traducción (dynarec), mucho más caro. El único
  precedente realmente x86→ARM en Vita es D2Vita/WinVita.

### 2.4 Wrappers GL / D3D
- **vitaGL** (Rinnegatamante): wrapper OpenGL (subconjunto) → `sceGxm` con aceleración por hardware.
  Es la capa de presentación que D2Vita usa para su ring Glide.
  - https://github.com/Rinnegatamante/vitaGL
  - https://www.gamebrew.org/wiki/VitaGL
- **gl4es4vita** (SonicMastr): traducción OpenGL 2.1/1.5 → GL ES 2.0/1.1 (Pvr).
  - https://github.com/SonicMastr/gl4es4vita
- **D3D→GL/GXM:** **no existe un wrapper D3D nativo para Vita**. Lo disponible es de escritorio:
  - WineD3D (D3D8–11 → OpenGL), **DXVK** (D3D9/10/11 → Vulkan; **inútil en Vita**, no hay Vulkan)
    https://github.com/doitsujin/dxvk
  - **dgVoodoo2** (D3D1–9 + DDraw + Glide → D3D11/12; solo Windows)
    https://dege.freeweb.hu/dgVoodoo2/
  - **D7VK / variantes** (D3D6/7 → Vulkan; escritorio): https://emulation.gametechwiki.com/index.php/Wrappers
  - **DXGL** (DirectDraw → OpenGL): ver la misma página de wrappers.
- **Implicación:** para HWM:R hay que **escribir un shim D3D6 immediate-mode → sceGxm/vitaGL**
  (análogo a lo que D2Vita hizo con Glide3x→GXM). No hay atajo listo para Vita. Lo bueno: la
  superficie usada por un juego D3D6 de 2000 suele ser chica y fixed-function.

### 2.5 Stack de dependencias típico
- `libshacccg.suprx` + `vitaGL` + `kubridge` + `pthread-embedded` + `mathneon` (todos vía `vdpm`).
  Ejemplos: https://github.com/Northfear/homm3hd-vita , https://github.com/vpan20/ssx-vita
- PS Vita HW de referencia: CPU 4× Cortex-A9 ~444 MHz (hasta ~500 con overclock), GPU
  PowerVR SGX543MP4+, **512 MB RAM + 128 MB VRAM**.
  - https://www.psdevwiki.com/vita/index.php/Graphics
  - https://gaming.stackexchange.com/questions/24170/playstation-vita-hardware-specs

---

## 3. Límites prácticos de Vita3K para homebrew pesado

- Proyecto **experimental** ("Don't forget… Expect crashes, glitches, low compatibility and poor
  performance"): https://github.com/Vita3K/Vita3K
- **Compatibilidad desigual**, con tuning por juego; el backend GPU cambia seguido y las listas de
  compatibilidad quedan desactualizadas: https://vita3k.org/compatibility
- **CPU:** Vita3K usa **Dynarmic** (recompilador ARM) en hosts x86; el issue que propuso el cambio
  señalaba que Unicorn era "notoriamente lento" y Dynarmic "far better performance".
  https://github.com/Vita3K/Vita3K/issues/68
  - Consecuencia para nosotros: si el homebrew **ya es un dynarec/jit** (p. ej. D2Vita, o el
    runtime winx86 de HWM:R), en Vita3K se produce **doble traducción** (x86→ARM en el guest,
    ARM→x86 en el emulador). El rendimiento será muy bajo y posiblemente no arranque.
- **Plugins taiHEN:** soporte **parcial**. La wiki de Vita3K advierte que los plugins que usan
  funciones no implementadas no funcionarán; menciona explícitamente **kubridge** como caso.
  https://lavaforge.org/preserve-emulation/Vita3K/wiki/Using-taiHEN-plugins.md (contenido vía snippet)
  - Si kubridge no funciona, D2Vita/WinVita caen al pool JIT de 16 MiB → riesgo de OOM/fallo
    con juegos grandes.
- **Base de compatibilidad de homebrew:** https://github.com/Vita3K/homebrew-compatibility
  (útil para chequear qué expectativas tener; no encontré entrada explícita de D2Vita).
- **Formatos:** `.vpk` (homebrew) / `.pkg` (retail); el emulador pide BIOS/firmware y un GPU driver
  Vulkan/GL moderno; la calidad depende mucho del host.
  https://vita3k.org/quickstart.html · https://vita3k.org/faq
- **Conclusión operativa:** Vita3K sirve como primer smoke test, pero **no es una referencia de
  performance ni de compatibilidad** para un port con JIT pesado. La validación de rendimiento debe
  ser consola real (reservada por AGENTS.md) o qemu-arm host-side (como hace D2Vita con
  `build_oracle_arm.sh`).

---

## 4. Implicaciones para el proyecto hotwheels

1. **Re-scopear HW-001:** el binario de `game/` es un **instalador**. Primer paso real: extraer
   `HotWheels.exe` y los datos (o determinar si el overlay es autoextraíble). Recién después importa
   la API real del juego.
2. **La API a shimear es D3D6 immediate + DirectDraw**, no D3D9. Conviene instrumentar con Wine
   (`WINEDLLOVERRIDES`, dgVoodoo2 ya viene en el repack) para capturar el **set exacto de llamadas**
   (`IDirect3D3`, `IDirect3DDevice3`, `IDirect3DViewport3`, `IDirectDraw4`, etc.). Eso alimenta
   HW-002/HW-006.
3. **Reutilizar winx86/WinVita** (PE loader + dynarec Box86) como hizo D2Vita, pero **el shim
   gráfico es nuevo**: D3D6→sceGxm. El modelo Glide3x de D2Vita (`src/glide_ring/`) es la mejor
   plantilla arquitectónica.
4. **Audio:** DirectSound + CD-audio (`Track1.wav`). Se puede redirigir el CD-audio a WAV igual que
   `_inmm`, sin depender de CD.
5. **Red multijugador:** `WS2_32`/`IPHLPAPI`; probablemente fuera del alcance de un MVP.
6. **Legal:** no commitear `HotWheels.exe`, el ISO ni assets (ya cubierto por `.gitignore`).

## 5. Preguntas abiertas / próxima investigación
- ~~¿El overlay es un instalador autoextraíble o hay que invocar el flujo InstallShield/MSI?~~
  **Resuelto `[estático]`:** es un bootstrapper **Advanced Installer**; el juego va dentro de
  `ISO/HWMR.iso` y el launcher (`HWMR.bat`/`HWMR.ps1`) lo monta y ejecuta `HotWheels.exe`.
  Falta implementar/confirmar la extracción no interactiva (ver HW-001).
- Set exacto de exports/llamadas D3D6 usadas por `HotWheels.exe` (traza Wine/API monitor).
- Confirmar/descartar motor LISA para HWM:R (strings de formato `SHADE.TAB`, `TRANSLAT.FRM`).
- ¿Existe algún intento previo de port de HWM:R o del motor UDS a otra plataforma? (no encontrado).
- Detalle de compatibilidad real de D2Vita bajo Vita3K (probar cuando HW-004 esté listo).

---

## Fuentes (URLs)
- Juego:
  - https://en.wikipedia.org/wiki/Hot_Wheels_Micro_Racers
  - https://www.pcgamingwiki.com/wiki/Hot_Wheels:_Micro_Racers
  - https://tcrf.net/Hot_Wheels_Micro_Racers
  - https://www.mobygames.com/game/807/ignition/ (motor LISA, nombrado por la gata de Kalle; UDS)
  - https://archive.org/details/HotWheelsMicroRacersUSA
  - https://www.igdb.com/games/hot-wheels-micro-racers
  - https://www.giantbomb.com/unique-development-studios-sweden/3010-1203/ (UDS 1993–2004)
  - https://www.myabandonware.com/game/hot-wheels-micro-racers-dhq
  - https://vgtimes.com/games/hot-wheels-micro-racers/
  - https://oldgamesdownload.com/hot-wheels-micro-racers/
  - https://www.gog.com/dreamlist/game/hot-wheels-micro-racers-2000
- Precedentes Vita:
  - https://github.com/Franckrst/D2Vita
  - https://github.com/Franckrst/WinVita
  - https://www.gamebrew.org/wiki/D2Vita
  - https://github.com/ptitSeb/box86 · https://ptitseb.github.io/box86/
  - https://github.com/TheOfficialFloW/gtasa_vita
  - https://github.com/v-atamanenko/soloader-boilerplate
  - https://github.com/Rinnegatamante/Android2Vita-Candidate-Ports-List
  - https://github.com/Rinnegatamante/vitaGL
  - https://github.com/SonicMastr/gl4es4vita
  - https://github.com/doitsujin/dxvk
  - https://dege.freeweb.hu/dgVoodoo2/
  - https://emulation.gametechwiki.com/index.php/Wrappers
  - https://github.com/TheOfficialFloW/kubridge
- Vita3K / hardware:
  - https://github.com/Vita3K/Vita3K
  - https://vita3k.org/compatibility · https://vita3k.org/faq
  - https://github.com/Vita3K/Vita3K/issues/68
  - https://github.com/Vita3K/homebrew-compatibility
  - https://lavaforge.org/preserve-emulation/Vita3K/wiki/Using-taiHEN-plugins.md
  - https://www.psdevwiki.com/vita/index.php/Graphics
  - https://gaming.stackexchange.com/questions/24170/playstation-vita-hardware-specs
