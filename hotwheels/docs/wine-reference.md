# HW-002 — Referencia x86 en Wine (agente `wine`, 2026-10-02)

Nivel de evidencia de todo lo de este documento: **[wine]** (traza de ejecución real de `HotWheels.exe`), salvo donde diga otra cosa.
Los logs crudos están en `hotwheels/docs/wine-logs/` (gitignored, ~30 MB); los resúmenes chicos están en `hotwheels/docs/wine-ref/`.

## 1. Entorno y cómo reproducir

| Item | Valor |
|---|---|
| Wine | `shared/opt/wine-11.18-amd64-wow64` (WoW64, prefijo win64; el exe corre como proceso 32-bit vía `wow64cpu`) |
| Prefijo | `hotwheels/wineprefix` (nuevo, gitignored) |
| Render | **wined3d sobre OpenGL** (Mesa Iris Xe, GL 4.6 core). `ddraw`, `d3d8`, `d3d9` forzados a *builtin* |
| Red | ninguna: `unshare -Un --map-user=$(id -u) --map-group=$(id -g)` (netns vacío) |
| Display | `:0` (XWayland); el juego abre una ventana 1920×1080 y dibuja el framebuffer 640×480 en la esquina sup. izquierda |

Receta (idempotente):

```bash
export WINEPREFIX=~/vita-ports/hotwheels/wineprefix PATH=~/vita-ports/shared/opt/wine-11.18-amd64-wow64/bin:$PATH
export WINEDLLOVERRIDES="mscoree,mshtml=d;ddraw,d3d8,d3d9=b"
wineboot -i                                   # crea el prefijo
# 1) instalar el repack (sólo hace falta para tener C:\Games\Hot Wheels - Micro Racers\; ver §2)
cd hotwheels/game && wine "Hot Wheels® - Micro Racers.exe" /qn      # NO matar con timeout a mitad: deja msiexec colgado
# 2) unidad D: = contenido del ISO, tipo CD-ROM
7z x -owineprefix/cd extracted/HWMR.iso
ln -s ../cd wineprefix/dosdevices/d:
wine reg add 'HKLM\Software\Wine\Drives' /v 'd:' /d cdrom /f           # HKLM, no HKCU
# 3) mover aparte los wrappers dgVoodoo del repack (DDraw/D3D8/D3D9.dll) para que no tapen a Wine
# 4) correr
cd "wineprefix/drive_c/Games/Hot Wheels - Micro Racers"
WINEDEBUG=+loaddll,+file,+ddraw,+d3d7,+dsound,+dinput,+winmm,+mmio,+volume unshare -Un --map-user=$(id -u) --map-group=$(id -g) wine HotWheels.exe
```

Trampas encontradas (todas medidas):

1. **`unshare -rn` (uid 0 simulado) rompe el tipo de unidad**: con root mapeado `GetDriveTypeW("D:\")` devuelve 3 aunque el registro diga `cdrom`; con `--map-user=<uid real>` devuelve 5. Con 3 el juego muestra el MessageBox **"No CD-ROM detected."** y sale.
2. El tipo de unidad se lee de **`HKLM\Software\Wine\Drives`**; `HKCU\...\Drives` se ignora.
3. El exe de 224 MB del repack es un Advanced Installer: al ejecutarlo lanza `msiexec /i HWMR.msi` que instala en `C:\Games\Hot Wheels - Micro Racers\`. Matarlo con `timeout` deja el custom-action server roto (`err:msiexec:custom_action_server ... pipe: 109`) y la instalación queda a medias. Sin matar, termina sola en <45 s con `/qn`.
4. `HotWheels.exe` instalado = el de `extracted/original/Program_Files/HotWheels.exe` (SHA-1 `fde8ccf6299e73d524ae7ab0d7c05536d2a703ad`, 430.080 B). Los datos (`DATA\`) del repack coinciden en layout con los extraídos del InstallShield.
5. Screenshot: en Wayland `import -window root` falla; funciona `xwd -id <id de la ventana "Hot Wheels® Micro Racers"> | convert` (ver §6).

## 2. ¿Lee el exe su propio overlay? — **No**

- `HotWheels.exe` (juego) **no tiene overlay** (sol, estático) y en la traza **nunca abre ni lee su propio archivo**: lo único que aparece sobre su nombre es el loader de Wine y el intento de `HotWheels.exe.manifest` (no existe → `c0000034`, inofensivo).
- El overlay de 222 MB pertenece sólo al **instalador** (`Hot Wheels® - Micro Racers.exe`), que lo usa para extraer MSI+CAB (consistente con FINDINGS #1/#2). Las lecturas de ese exe se vieron en la fase de instalación (lee su propia cola en bloques de 512 B vía `NtReadFile`); **no se midieron offsets** de esa fase porque no hace falta para el port (el instalador no se porta; sol ya extrajo MSI/CAB/ISO).
- Para el port, entonces: **cero trabajo de overlay**. El loader sólo necesita mapear las 3 secciones del PE de 430 KB.

## 3. DLLs cargadas por el proceso del juego (orden de carga) [wine]

Sólo Wine **builtin** salvo el propio exe (todas en `syswow64`/`system32` del prefijo):

`ntdll`, `wow64*`, `kernelbase`, `kernel32`, `msvcrt`, `ucrtbase` (arrastrados por Wine, el exe **no** importa CRT: está linkeado estático), `sechost`, `advapi32`, `gdi32`/`win32u`, `user32`, `msacm32`, `combase/ole32/rpcrt4`, **`WINMM`**, **`DSOUND`**, `opengl32` + **`wined3d`** (por ddraw), **`DDRAW`**, `comctl32`, `hid`, **`DINPUT`**, `imm32`, `winex11.drv`, `uxtheme`, `oleaut32`, `version`, `mmdevapi` (backend de audio de Wine), **`mcicda`** (cargada por MCI al abrir `cdaudio`, y descargada al fallar el ioctl).

Imports reales del juego (confirmados con `+relay`, 25 s, filtrando `ret=` dentro del exe; `docs/wine-ref/relay-exe-calls-25s.txt`): KERNEL32 (CreateFileA/ReadFile/SetFilePointer/CloseHandle/GetFileType/FindFirstFileA/VirtualAlloc/Heap*/GetDriveTypeA/GetLogicalDriveStringsA/GetModuleFileNameA/GetLocalTime/**CreateToolhelp32Snapshot+Process32First/Next**), USER32 (RegisterClassA, CreateWindowExA, Show/UpdateWindow, ShowCursor, PeekMessageA, Translate/DispatchMessageA, GetSystemMetrics, LoadIconA/LoadCursorA), WINMM (`timeGetTime`, `mmioOpenA/Descend/Read/Seek/Ascend/Close/...`, **`mciSendCommandA`**), `DirectDrawCreate`, `DirectDrawEnumerateExA`, `DirectSoundCreate`, `DirectInputCreateA`.
No hay ningún otro DLL de gráficos (nada de `d3dim.dll`, `d3d8/9`, `opengl32` desde el exe). `Process32*` confirma FINDINGS #9 (hay que dar una implementación; con `winx86` stub devolviendo una lista con 1 proceso alcanza, ver §7).

## 4. Archivos abiertos [wine]

Rutas relativas al cwd (`C:\Games\Hot Wheels - Micro Racers\`), orden de primera apertura, **todas sólo lectura** salvo las marcadas:

1. `data\menu\highscore.dat`, `data\lang.txt` (153 B)
2. *(chequeo de CD, §5)* — **no se abre ningún archivo en `D:\`**
3. `data\menu\settings.dat` (texto, ~54 B; el juego lo reescribe al salir: `settingsSave`), `data\sound\silence.wav`, `data\sinus.sin`
4. Dos `CreateFile` a `\\?\HID#VID_845E&PID_0001/0002...{4d1e55b2-...}` (son los 2 joysticks virtuales de Wine que enumera DirectInput; en Vita se pueden ocultar → 0 joysticks; el teclado alcanza)
5. `data\menu\sound\00-select.wav` … `04-wrong.wav`
6. Menú/pantallas: `data\loading\{wave,slumber,office,split_wave,split_slumber,split_office}.tga`, `data\menu\{curtain_normal,curtain_shadow,credits0..2,mainmenu,sprites}.tga`, `data\intro\{mattel,uds,intro}.tga`
7. `data\menu\{cars,objects}.sfp`, `data\loading\objects.sfp`, `data\menu\shade.tab`, `data\menu\physics.dat`

Patrón de I/O: `CreateFileA` + `ReadFile` de archivos casi completos + `SetFilePointer` (401 llamadas en 25 s de arranque) — o sea acceso secuencial/seek por chunks (`.sfp`), no mmap. Los WAV se abren por `mmio*` (RIFF) → `IDirectSound::CreateSoundBuffer` + Lock/Unlock. **El juego no abre `MUSIC\Track N.wav` en el menú** (la música va por MCI `cdaudio`, ver §5).
Cualquier archivo que el shim no encuentre debe devolver el error Win32 normal: el juego sondea `HotWheels.exe.manifest` y falla limpio.

## 5. Detección del CD (DRM) [wine]

Secuencia observada en `initHotWheels`:

1. `GetLogicalDriveStringsA` → para cada letra `GetDriveTypeA`/`W` (C:→3, **D:→5**, Z:→3). El juego busca una unidad de tipo CD-ROM (por RE: la primera que contiene `hwicon.bmp`; en esta corrida el ISO sí lo contiene: `wineprefix/cd/HWICON.BMP`).
2. `CreateFileW("\\.\D:", GENERIC_READ, SHARE_READ)` y 3 `DeviceIoControl`: `0x24000` ×2 (`IOCTL_CDROM_READ_TOC`) y `0x24008`. **Wine no los soporta** (`Unsupported ioctl 24000`) → error → MCI `mcicda` se descarga. **El juego sigue de largo igual**: el menú se renderiza y no hay audio de CD.
3. Música: `mciSendCommandA` (6 llamadas en 25 s: OPEN/SET/STATUS, ver triage §57).

Contrato mínimo del shim para pasar el chequeo: `GetDriveTypeA/W(letra)==DRIVE_CDROM(5)` para una letra, y que el `hwicon.bmp` (si lo valida) esté en `X:\`. El resto (IOCTL/MCI) puede fallar sin matar el juego → para música hay que emular `mciSendCommandA` sobre los `Track N.wav` del repack.

## 6. API gráfica usada y hasta dónde llega [wine]

### Hasta dónde llega
- **Llega al menú principal** (anillo naranja con coche, bandera, trofeo, llave; ver `docs/wine-logs/menu-640x480.png` y `menu-main-640x480.png`, **no commiteados**: son arte con copyright). Corre estable ≥60 s; en toda la traza no hay ningún `err:` y sólo 4 `fixme:` (`SYSTEM_PERFORMANCE_INFORMATION`, `DirectDrawEnumerateExA flags 0x4`, `Surface1::Flip Ignoring flags 0x1`, `WaitForVerticalBlank stub`: inocuos). Primera pantalla = intro, después menú; tras simular Enter el menú cambia de pantalla (tecla llegó), pero **no avanzamos hasta una carrera** (no se pudo conducir el menú de forma fiable: sin `xdotool`, sólo XTEST puro y el foco de la ventana fullscreen compartía pantalla con Vita3K; se frenó para no mandar teclas a otras apps).
- **Consecuencia importante:** las §3–§7 son del **camino software DirectDraw 8 bpp** (intro + menús + carrera con 3D=NO). El **camino D3D6** se trazó después, en una carrera real (HW-012): ver **§8**. Resumen: la carrera existe en dos variantes — **software 800×600×8 bpp** (setting `GFX_3DNO`, valor que traía el `settings.dat` instalado por el repack) y **D3D6 800×600×16 bpp** (setting `GFX_3DYES`).

### Secuencia de arranque gráfico observada
1. `DirectDrawEnumerateExA(flags 0x4)` → 1 driver ("DirectDraw HAL").
2. **Sonda 3D** (`ddProbeDriver`): `DirectDrawCreate` → QI `IDirectDraw4` → `SetCooperativeLevel(hwnd, 0x13 = FULLSCREEN|ALLOWREBOOT|EXCLUSIVE)` → `EnumDisplayModes` (flags 0; Wine devuelve 320x200 … 1920x1080, 32 y 16 bpp) → `SetDisplayMode(640,480,**16**)` → `CreateSurface` primaria `DDSCAPS_PRIMARYSURFACE|DDSCAPS_3DDEVICE` (pf RGB565 `F800/07E0/001F`) → `QI IDirect3D3` → **`IDirect3D3::CreateDevice(IID_IDirect3DHALDevice {84e63de0-46aa-11cf-816f-0000c020156e}, surface)`** (device versión 3) → un único `IDirect3DDevice3::SetRenderState(0x15 = TEXTUREMAPBLEND, 2 = MODULATE)` → `Release` del device, `RestoreDisplayMode`. (`EnumTextureFormats`/`CreateViewport`/`DrawPrimitive` **no** aparecen.)
3. **Modo de trabajo real**: `SetDisplayMode(640,480,**8**)`, `CreateSurface` primaria `COMPLEX|FLIP|PRIMARYSURFACE`, `BackBufferCount=1` (8 bpp paletizada, `DDPF_PALETTEINDEXED8|DDPF_RGB`), `GetAttachedSurface` (backbuffer), + **2 offscreen** `OFFSCREENPLAIN|SYSTEMMEMORY` 640×480 8 bpp; `CreatePalette(flags 0x44 = DDPCAPS_8BIT|DDPCAPS_ALLOW256, 256 entradas)` + `SetPalette` en la primaria. (Wine avisa `Failed to create draw texture hr 0x8876086c` para las sysmem: inocuo.)
4. **Bucle por frame** (≈2.319 iteraciones en la corrida de ~60 s): `Surface4::Lock(NULL, desc, flags 0)` / `Unlock(NULL)` sobre un offscreen (el juego dibuja ahí por software, 8 bpp), `Surface4::BltFast(dst 0,0, src offscreen, rect 0,0–640,480, flags 0)` hacia el backbuffer, `Surface1/4::Flip(NULL, flags 0x1 = DDFLIP_WAIT)`, `IDirectDraw4::WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN=1)` (198 veces), `Palette::SetEntries(0, 0, 256, tabla)` (2.509 veces: fades/cambios de paleta), `Surface1::GetAttachedSurface` varias veces por frame (back buffer).

### Lista de métodos COM usados (superficie del shim) [wine]
Conteo en 60 s de intro+menú; `docs/wine-ref/ddraw-methods-counts.txt` tiene el crudo (nombres internos de Wine).

| Interfaz (versión usada realmente) | Métodos observados |
|---|---|
| `DirectDrawCreate` (export) | → `IDirectDraw` (v1) |
| `IDirectDraw` v1 | `QueryInterface`, `Initialize`, `AddRef`, `Release` |
| **`IDirectDraw4`** | `QueryInterface`, `AddRef`, `Release`, `SetCooperativeLevel`, `EnumDisplayModes`, `SetDisplayMode`, `RestoreDisplayMode`, `CreateSurface`, `CreatePalette`, `WaitForVerticalBlank` |
| `IDirectDrawSurface` v1 | `AddRef`, `Release`, `GetAttachedSurface`, `Unlock`, `Flip` (por la vía v1 del primario) |
| **`IDirectDrawSurface4`** | `QueryInterface`, `AddRef`, `Release`, `Lock`, `Unlock`, `BltFast`, `Flip`, `GetAttachedSurface`, `GetPixelFormat`, `SetPalette` |
| `IDirectDrawPalette` | `SetEntries` (+ `AddRef`) |
| `IDirect3D3` | `QueryInterface`, `CreateDevice`, `AddRef`, `Release` |
| `IDirect3DDevice3` | `SetRenderState`, `Release` (sólo en la sonda) |
| `IDirectSound` (`DirectSoundCreate` ordinal 1) | `SetCooperativeLevel(DSSCL_EXCLUSIVE)`, `CreateSoundBuffer`; primario: `SetFormat`; secundarios: `Lock`, `Unlock`, `Play(flags 1 = LOOPING, silence.wav)`; los flags `CTRLFREQUENCY|CTRLPAN|CTRLVOLUME` sugieren `SetVolume/SetPan/SetFrequency`/`Stop` en juego (no observados en el menú) |
| `IDirectInputA`/`IDirectInputDevice2A` (`DirectInputCreateA`) | `CreateDevice` para `GUID_SysKeyboard {6f1d2b61-...}` y `GUID_SysMouse {6f1d2b60-...}`, `SetDataFormat` (teclado: 256 objetos, 256 B), `SetCooperativeLevel(hwnd, 0x6 = NONEXCLUSIVE|FOREGROUND)`, `Acquire`, `GetDeviceState` (**≈96.000 llamadas** en la corrida, polling por frame), `Release` |
| WINMM | `mmioOpenA/Descend/Ascend/Read/Seek/GetInfo/SetInfo/Advance/Close`, `timeGetTime` (35 en 25 s), `mciSendCommandA` |

DirectSound observado: `DSSCL_EXCLUSIVE`, primario creado con `DSBCAPS_PRIMARYBUFFER`, `SetFormat(PCM 22050 Hz, 16 bit, estéreo)`; secundarios `DSBCAPS_STATIC|CTRLFREQUENCY|CTRLPAN|CTRLVOLUME` mono 22050/16 (p.ej. `bufferbytes=11288`), 7 `CreateSoundBuffer` en total: 1 primario + `silence.wav` (en loop) + los 5 efectos de menú `00..04`.

### Detalle que cambia el diseño del shim
- **Menú e intro no usan D3D en absoluto** (software 8 bpp + paleta). Para llegar a "boot hasta menú" (HW-007) alcanza un **DirectDraw 8 bpp con paleta**: Lock devuelve un puntero lineal 640×480 (pitch 640), `BltFast`/`Flip`/`SetEntries` → convertir a RGBA con la paleta en el último paso y subirlo como textura a sceGxm (o vitaGL), 40 Hz.
- `IDirect3D3::CreateDevice` en la sonda **tiene que devolver éxito** para que el juego considere "Direct3D found" (flag +0x1e del driver); el menú es idéntico en ambos modos. **La carrera también corre 100 % por software** (800×600×8 bpp paletizado, mismo patrón Lock/BltFast/Flip) cuando `settings.dat` línea 1 = 0 (default). Para un primer hito se puede fallar la sonda (`DDERR_NOD3D`) y quedar en software en todo el juego; el renderer D3D6 (§8) sólo se agrega si la carrera por software es lenta en Vita (decisión de producto pendiente).
- `SetCooperativeLevel` sólo guarda flags; `SetDisplayMode(640,480,8)` no debe cambiar nada en Vita (framebuffer propio de 960×544 con escalado).

## 7. Notas para el shim / winx86
- `Process32First/Next` + `CreateToolhelp32Snapshot` por `GetProcAddress` (relay: 9 `Process32Next` seguidos) → `isAlreadyRunning`; devolver una única entrada (el propio exe).
- No hay threads propios, ni red, ni registro, ni GDI de dibujo. `user32`: crea 1 ventana (`RegisterClassA`+`CreateWindowExA`), `ShowCursor(FALSE)`, loop `PeekMessageA`.
- Escrituras reales al disco: sólo `settings.dat`/`highscore.dat` (al salir) y, opcional, screenshots/`C:\DEBUG.TXT` (RE, no observado en esta corrida).

## 8. Camino D3D6 en carrera (HW-012) [wine]

### 8.1 Cómo se llegó (reproducible, aislado del escritorio)
- Display propio: `Xephyr :99 -screen 800x600 -ac` (anidado; no toca el foco del escritorio real). Wine con `DISPLAY=:99`. Capturas: `xwd -root -display :99 | convert`. Teclas: XTEST con `ctypes` sobre `libX11/libXtst` (script de 12 líneas, sin dependencias; `XTestFakeKeyEvent` contra `:99`). Todo lo demás igual que §1.
- Navegación (verificada por screenshot): menú de anillo; **Down/Up** rotan la selección, **Return** acepta. Desde el arranque (esperar ~28 s de intro): `Return` (1 jugador) → `Return` (bandera = carrera) → `Return` (pista) → `Return` (coche) → **carrera**.
- **Activar D3D**: el flag 3D es la **línea 1 de `DATA\MENU\settings.dat`** (global `0x4720a8`, 1.ª en el orden de `settingsSave`; menú gráfico `GFX_3DNO`/`GFX_3DYES` = placa con/sin X). Editando esa línea de `0` a `1` el juego usa D3D. El toggle in-game (ícono placa dentro de Opciones) **no logré hacerlo efectivo** por teclas en 5 intentos (4 pulsaciones = vuelve a NO; 1 pulsación no cambió el modo): usar el archivo. (Menú de opciones: llave → Up×4 desde "dificultad" llega a la placa.)
- Traza: `WINEDEBUG=-all,+ddraw,+d3d7`; 20 s de carrera = 634 MB sin filtrar (≈808.000 `DrawPrimitive`): **filtrar** `SetTexture/SetTextureStageState/DrawPrimitive/SetRenderState` con `grep -v` o contar con `uniq -c` en línea. Resúmenes: `docs/wine-ref/d3d6-race-method-counts.txt`. Log filtrado (82 MB→0,6 MB gz): `docs/wine-logs/race-d3d-filtered.log.gz`; screenshot `docs/wine-logs/race-d3d-800x600.png` (no commiteados).

### 8.2 Secuencia de inicio del modo 3D
1. Sonda inicial (idéntica a §6, 640×480×16, CreateDevice + Release; marca que hay 3D).
2. Al empezar la carrera (o al salir de Opciones con 3D cambiado): `DirectDrawCreate` → QI `IDirectDraw4` → `SetCooperativeLevel(0x13)` → QI `IDirect3D3` (IID `bb223240-e72b-11d0-a9b4-00aa00c0993e`) → **`SetDisplayMode(800,600,16)`** (la carrera 3D es 800×600; los menús 640×480).
3. `IDirectDraw4::CreateSurface` primaria `COMPLEX|FLIP|PRIMARYSURFACE|3DDEVICE`, `BackBufferCount=1`, 16 bpp **RGB565** (`F800/07E0/001F`) → `GetAttachedSurface` (backbuffer) → `GetPixelFormat`.
4. **`IDirect3D3::CreateDevice(IID_IDirect3DHALDevice {84e63de0-46aa-11cf-816f-0000c020156e}, backbuffer)`** → `IDirect3DDevice3` (Wine: versión 3; no hay Z-buffer: `GetAttachedSurface(ZBUFFER)` no encuentra nada y Wine setea depth-stencil NULL). **El juego NO usa Z-buffer** (nunca se crea superficie `ZBUFFER`; `ZENABLE` nunca se setea → orden de dibujo = painter's algorithm).
5. `IDirect3DDevice3::EnumTextureFormats(cb, ctx)` (1 vez) y `IDirectDraw4::GetAvailableVidMem(DDSCAPS_TEXTURE|DDSCAPS_3DDEVICE)` (el juego decide formato y nº de páginas; Wine informó total ≈4 GB, libre ≈4 GB → **el shim puede devolver una cifra chica, p.ej. 8–16 MB**).
6. `IDirect3D3::CreateViewport` → `IDirect3DDevice3::AddViewport` → `IDirect3DViewport3::SetViewport2` con `D3DVIEWPORT2{dwSize=44, X=0, Y=0, Width=800, Height=600, dvClipX=-1.0, dvClipY=1.3333, dvClipWidth=2.0, dvClipHeight=2.6667, dvMinZ=0.0, dvMaxZ=1.0}` (variante 799×599 para el viewport secundario) → `GetCurrentViewport` (1.ª vez devuelve `D3DERR_NOCURRENTVIEWPORT`: el juego lo tolera).
7. Render states de init: `TEXTUREMAG(0x11)=LINEAR(2)`, `TEXTUREMIN(0x12)=LINEAR(2)`, `CULLMODE(0x16)=NONE(1)`; `TEXTUREMAPBLEND(0x15)=MODULATE(2)` lo pone Wine internamente en `CreateDevice` (no el juego).

### 8.3 Bucle por frame (≈744–911 escenas en la ventana medida)
`Surface1::GetAttachedSurface` (backbuffer) → **`Surface4::Flip(NULL, DDFLIP_WAIT)`** del frame anterior (Wine ignora el flag) → `IDirect3DDevice3::SetCurrentViewport(vp)` → **`IDirect3DViewport3::Clear(0 rects, NULL, flags=D3DCLEAR_TARGET 0x1)`** (¡dwCount=0! Wine **ignora** el clear; el juego repinta toda la pantalla igual, así que el shim puede ignorarlo o hacer clear de color a negro) → `BeginScene` → **~765 `DrawPrimitive` por escena** (p50 747; ver §8.9) → `EndScene`.
HUD/overlays 2D y pantallas de carga usan el camino DD: `Surface4::Lock(flags 0)/Unlock` + `Surface4::BltFast(0,0, src sysmem 800×600 RGB565, rect 0,0–800,600, 0)` (sup. offscreen `OFFSCREENPLAIN|SYSTEMMEMORY` 800×600 16 bpp RGB565) + `Flip`.

### 8.4 `DrawPrimitive` — **un solo tipo de llamada** (808.469 veces)
| Campo | Valor (100 % de las llamadas) |
|---|---|
| Método | `IDirect3DDevice3::DrawPrimitive` (no hay `DrawIndexedPrimitive`, ni `DrawPrimitiveStrided`, ni VB) |
| `D3DPRIMITIVETYPE` | **5 = `D3DPT_TRIANGLESTRIP`** |
| FVF | **`0x104` = `D3DFVF_XYZRHW | D3DFVF_TEX1`** → vértice de **24 bytes**: `float sx, sy, sz, rhw, tu, tv` (**sin color/specular**; pre-transformado en pantalla; sin luz) |
| `dwVertexCount` | **4** (534.988 = 66 %) o **3** (273.481 = 34 %) → quads y triángulos sueltos |
| `dwFlags` | `0x10 = D3DDP_DONOTLIGHT` |
| Textura | 1 `SetTexture(0, tex)` **antes de cada draw** (824.273 llamadas ≈ 1 por draw); nunca `SetTexture(0, NULL)` |
Es decir: el renderer es **un sprite/quad batcher texturizado 2D**: mapeo directo a un VBO dinámico de 4 vértices de 24 B + `glDrawArrays(GL_TRIANGLE_STRIP)` (sceGxm: `sceGxmDraw` con tira de triángulos o expandir a 2 triángulos).

### 8.5 Render states observados (`SetRenderState(state, value)`; 69.641 llamadas) y su significado D3D6
| Estado | Valor | Significado | Llamadas |
|---|---|---|---|
| `0x1b` ALPHABLENDENABLE | 1 | blend on (**nunca se setea a 0** en el log) | 23.212 |
| `0x13` SRCBLEND / `0x14` DESTBLEND | 6 / 5 | `INVSRCALPHA` / `SRCALPHA` (**invertido** respecto del blend estándar; replicar tal cual) | 11.587 pares |
| `0x13` / `0x14` | 1 / 4 | `ZERO` / `INVSRCCOLOR` (oscurecer: sombras/humo) | 10.475 pares |
| `0x13` / `0x14` | 0xa / 2 | `INVDESTCOLOR` / `ONE` (aditivo con inversión) | 1.150 pares |
| `0x11` TEXTUREMAG, `0x12` TEXTUREMIN | 2 (LINEAR) | filtrado bilineal | 1 c/u |
| `0x16` CULLMODE | 1 (NONE) | sin culling | 1 |
| `0x15` TEXTUREMAPBLEND | 2 (MODULATE) | default de Wine, no del juego | 2 |
Patrón: cada ≈70 draws cambia el trío (`SRC`,`DEST`,`ALPHABLENDENABLE`). **No se tocan**: `ZENABLE/ZWRITEENABLE/ZFUNC`, `ALPHATESTENABLE`, `COLORKEYENABLE`, `SHADEMODE`, `FOGENABLE`, `DITHER`, `WRAPU/V`, `TEXTUREADDRESS`. (Según `re-engine.md` `COLORKEYENABLE` se setearía "si no hay alpha": con las texturas ARGB1555 no se usó.)
`SetTextureStageState(stage 0, D3DTSS_COLOROP 0x1, D3DTOP_SELECTARG1 0x2)`: 826.095 veces, **siempre el mismo valor** → solo se usa el color de la textura (el shading viene pre-horneado en la textura/`SHADE.TAB`; sin modulación por vértice). No se setea ningún otro stage ni `ALPHAOP`.

### 8.6 Texturas y superficies [wine]
- **Formato de textura elegido por el juego (tras `EnumTextureFormats`): ARGB1555** — `DDPF_ALPHAPIXELS|DDPF_RGB`, 16 bit, `R=0x7c00 G=0x03e0 B=0x001f A=0x8000` (alpha de 1 bit). `EnumTextureFormats` de Wine listó 16 formatos (entre ellos paletizados); el juego no tomó el paletizado.
- Cada página: `IDirectDraw4::CreateSurface` con `DDSD_CAPS|HEIGHT|WIDTH|PIXELFORMAT`, caps **`DDSCAPS_TEXTURE`, `DDSCAPS2_TEXTUREMANAGE`**, **256×256** (todas las 32 páginas observadas; 29+2+1 creaciones; límite del juego: "Too many texture pages!!!"), sin mipmaps, sin colorkey (no hay `SetColorKey`). Subida por `Surface4::Lock(NULL, desc, flags 0x821 = WAIT|WRITEONLY|NOSYSLOCK)` + escritura lineal + `Unlock`; luego `Surface4::QueryInterface(IID_IDirect3DTexture2 {93281502-8cf8-11d0-89ab-00a0c9054129})` (34–36 QI) y `Texture2::AddRef/Release` (32 / 2). **No hay `Texture2::GetHandle`/`Load`/`PaletteChanged`** (se usa `SetTexture` por interfaz, DX6).
- Paleta 8 bpp (`CreatePalette`, `Surface4::SetPalette`, 3 veces) sólo para las superficies del camino software/UI.
- `ddraw_find_decl` (Wine) = 645.604 veces "fvf 00000104": traducción interna a vertex declaration de wined3d; irrelevante para el shim.

### 8.7 Superficie D3D6 exacta que debe implementar el shim (para correr la carrera 3D)
| Interfaz (vtable usada) | Métodos |
|---|---|
| `IDirectDraw4` | (§6) + `GetAvailableVidMem` |
| `IDirect3D3` | `CreateDevice`, `CreateViewport`, `QueryInterface`, `AddRef`, `Release` |
| `IDirect3DDevice3` | `EnumTextureFormats`, `AddViewport`, `GetCurrentViewport`, `SetCurrentViewport`, `BeginScene`, `EndScene`, `SetRenderState`, `SetTextureStageState`, `SetTexture`, `DrawPrimitive`, `Release` |
| `IDirect3DViewport3` | `SetViewport2`, `Clear` (flags TARGET, count 0), `AddRef`, `Release` |
| `IDirect3DTexture2` | sólo `AddRef`/`Release` (el puntero es el handle para `SetTexture`) |
| `IDirectDrawSurface4` | (§6) `Lock` (texturas, flags 0x821), `Unlock`, `GetPixelFormat`, `GetAttachedSurface`, `BltFast`, `Flip`, `SetPalette`, `QueryInterface` |
No aparecen: `Execute`, `DrawIndexedPrimitive`, `DrawPrimitiveVB`, `SetTransform`, `SetLight`, `SetMaterial`, `SetClipStatus`, `GetCaps`, `ValidateDevice`, `Begin/Vertex/End`, ZBuffer, `SetColorKey`, mipmaps, `Blt` (sólo `BltFast`).

### 8.8 Impacto en el diseño (síntesis)
1. **Opción A (recomendada para el primer hito): software 8 bpp** también en carrera (`GFX_3DNO`): un solo shim DirectDraw (Lock/BltFast/Flip/SetEntries). Simplifica todo, pero el rasterizador del juego corre en la CPU Vita vía dynarec x86→ARM (rendimiento a medir en emulador/consola real).
2. **Opción B: D3D6 → sceGxm/vitaGL**: la superficie es **diminuta** (un quad textured 2D, XYZRHW+TEX1, 24 B/vértice, blend de 3 combos, filtro bilineal, sin Z, sin culling, sin luces, texturas 256×256 ARGB1555 ×32): el costo está en el volumen (~890 draws y ~890 `SetTexture` por frame; 40 Hz → ≈35.000 draws/s) → hay que **batchear por textura** o usar un único VBO por frame, y convertir ARGB1555→RGBA4444/ABGR1555 (Vita soporta `SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR`).
3. La **segunda variante (`0x104` sin Z)** implica orden de pintado estricto = orden de las llamadas: no reordenar draws.
4. Resolución: menús 640×480, carrera 3D 800×600, carrera software 800×600×8 (`SetDisplayMode` se llama cada vez que cambia el estado) → el shim escala a 960×544.

### 8.9 Presupuesto por escena y capturas de referencia (HW-012, 2.ª corrida) [wine]
Medido sobre **4.598 escenas** (`BeginScene`..`EndScene`, incluye selección de pista 3D y carrera en la grilla de largada; 800×600×16, 3D=1):

| Por escena | min | p50 | p90 | max | prom. |
|---|---|---|---|---|---|
| `DrawPrimitive` | 674 | **747** | 788 | 1.640 | 765 |
| `SetTexture` | 683 | 752 | 793 | 1.658 | 774 |
| `SetRenderState` | 30 | 54 | 78 | 246 | 58 |
(el 90 % de las escenas tiene 700–799 draws; 1 `SetTexture` por draw; ≈ 58 cambios de estado por escena; **0 escenas sin draws**). A 40 Hz máx.: ≈ 30.000 draws/s. Corrige la cifra "≈887" de §8.3/FINDINGS 18, que venía de dividir sobre una ventana distinta (misma conclusión, orden de magnitud).

**Capturas de referencia (golden images, gitignored: arte con copyright)** en `hotwheels/docs/wine-logs/golden/`, PNG 800×600 (pantalla completa de Xephyr; los menús ocupan los 640×480 de arriba a la izquierda), Wine 11.18 wined3d/GL, 3D=1, sin input de conducción:
`g01-menu-players` (selección de jugadores, 1P), `g02-menu-ring-race` (anillo con bandera/trofeo), `g03-track-select` (pista en 3D con ruedas), `g04-race-startgrid` (grilla de largada con la bandera a cuadros), `g05..g09-race-{a..e}` (carrera, coche quieto en el recorrido de arena, HUD completo: cronómetro, ranking de coches, velocímetro). Las capturas **no son bit-exactas entre corridas** (el cronómetro del HUD y el timer a 40 Hz cambian el frame): para un test de golden image enmascarar el HUD (cronómetro arriba-izq., ranking abajo-izq., velocímetro abajo-der.) o comparar con tolerancia (p.ej. SSIM/PSNR). Wine usa filtrado `LINEAR` y wined3d: diferencias de ±1 LSB en el blending respecto de sceGxm son esperables.

## 9. Pendiente
- Medir rendimiento real (tiempo de CPU por frame) en software vs D3D en Wine, para estimar costo en dynarec Vita (no hecho: Wine/Xephyr con GL por software no es representativo).
- Verificar el toggle in-game 3D (placa) — hoy sólo se logró activar D3D vía `settings.dat` línea 1.
- Offsets del overlay del instalador: sólo si alguien lo pide.
