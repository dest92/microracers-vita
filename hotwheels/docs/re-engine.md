# HW-008 — RE del motor de HotWheels.exe

- Autor: `re` · Fecha: 2026-10-02 · Evidencia: **[estático]** salvo que se indique.
- Binario: `hotwheels/extracted/original/Program_Files/HotWheels.exe` (430.080 B, SHA-256 `5418a6aa…1122`). Versión: el PE **no tiene recurso VERSION** (buscado `VS_VERSION_INFO`/cadenas `4.6.2` en ASCII y UTF-16: ausentes); el `4.6.2.436` de `research.md` sale de la tabla del instalador del repack → dato externo no verificado.
- Ghidra 11.3.2 headless, proyecto `shared/ghidra-proyectos/HotWheels` (auto-análisis completo, 814 funciones decompiladas).
  ~60 funciones renombradas (lista al final). Scripts de volcado: Jython `Dump.py`/`Dis.py`/`Ren.py` (scratchpad, no versionados; reproducibles con `analyzeHeadless … -postScript`).
- Offsets COM: leídos del disassembly (`CALL [reg+off]`) y mapeados a las vtables del SDK DX6. GUIDs leídos de `.rdata` 0x45d700–0x45d77f.

## 0. TL;DR para el port
1. **Monohilo.** No importa `CreateThread`/sincronización. El único hilo extra posible es interno de DSound/DInput. → no hace falta soporte de threads en winx86 para el juego en sí.
2. **Import set chico** (94 símbolos): KERNEL32 (casi todo CRT MSVC estático), USER32 (ventana/mensajes), WINMM (MCI CD-audio, mmio WAV, `timeGetTime`), DDRAW (`DirectDrawCreate`, `DirectDrawEnumerateExA`), DSOUND (ordinal 1 = `DirectSoundCreate`), DINPUT (`DirectInputCreateA`). **No importa D3D, ni registro, ni GDI, ni red.** D3D se obtiene vía `QueryInterface` desde DirectDraw.
3. **El motor dibuja paquetes con layout compatible con PsyQ `POLY_FT3/FT4/GT3/GT4`** (origen PlayStation = inferencia) y los manda a D3D6 o a un rasterizador software 8 bpp. D3D6 mínimo: `XYZRHW|TEX1` sin color, una `DrawPrimitive(TRIANGLESTRIP)` por primitiva, sin Z. Ver §3.
4. **Software 8 bpp es obligatorio**: menús siempre 640×480×8 sin D3D; carrera en SW por default (opción 3D=NO, 320×240×8). Recomendación: M3 = DDraw 8 bpp + blit; D3D en HW-010 (§3.4).
5. **Bloqueantes de arranque a shimear**: (a) chequeo de CD: busca `hwicon.bmp` en una unidad `GetDriveTypeA()==DRIVE_CDROM(5)`; (b) chequeo de instancia única con `CreateToolhelp32Snapshot/Process32First/Next` vía `GetProcAddress` — **si no existen, muestra "Windows NT is not supported." y sale**; (c) audio CD por MCI (`cdaudio`).
6. **Timing fijo a 40 Hz con busy-wait** sobre QPC (§2). En Vita conviene reemplazar el spin por sleep.
7. **IO = CRT** (`fopen/fseek/ftell/fread`) de archivos completos a pools de memoria. Rutas relativas con `\` y **mayúsculas mezcladas** (`data\menu\…` vs `DATA\WAVE\VEHICLES.SFP`; en disco `DATA/MENU/…`) → el FS del port debe ser case-insensitive y traducir `\`.

## 1. Arranque y loop
`entry` (0x452f05, CRT) → `WinMain` (0x4503a0):
1. Carga `data\lang.txt` en 4 slots de 0x400 B en 0x474104 (convierte `\n` literales). Slot0 título, 1 "No CD-ROM detected.", 2 "CD not found…", 3 "Windows NT is not supported.".
2. `isAlreadyRunning` (0x450970): cuenta procesos `hotwheels.exe` con Toolhelp; >1 → sale. Sin Toolhelp → MessageBox(slot3) y sale.
3. Recorre `GetLogicalDriveStringsA`; primera unidad CDROM que contenga `hwicon.bmp` queda en `DAT_0047510c` (letra de CD, también usada por MCI). Si no → MessageBox y sale.
4. `RegisterClassA` (WndProc 0x4507a0), `CreateWindowExA(WS_POPUP, 0,0, SM_CXSCREEN×SM_CYSCREEN)`, `ShowWindow/UpdateWindow`.
5. `initHotWheels` (0x450170): settings (`data\menu\settings.dat`), `dsInit` + `data\sound\silence.wav` en loop, `srand(time)`, pools de memoria, `data\sinus.sin` (tabla seno 16-bit), `inpInit`, joysticks, `cdOpen(letraCD)` + `cdReadTOC`, `ddInit` (enumerar drivers), screenshot. Estado inicial: `DAT_00475108 = state_initial (0x423470)`.
6. Loop:
   ```
   for(;;){ while(PeekMessage(PM_REMOVE)){ if WM_QUIT goto fin; Translate/Dispatch }
            if(!appActive){ WaitMessage(); continue; }
            inpUpdate(); if(!state) break; state(); inpMouseLatch(); }
   ```
   El juego es una **máquina de estados por puntero a función** (`DAT_00475108` = estado actual, `DAT_00475104` = hook de salida). Cada estado llama internamente al timer y al render.
7. Salida: ddShutdown, memPoolFree, cdClose, inpShutdown, settingsSave, dsShutdown.

WndProc: `WM_ACTIVATE` → `appActive` + reanuda (`cdPlayTrack`) / pausa (`cdPause`) el CD; `WM_DESTROY/WM_CLOSE` → `PostQuitMessage`; `WM_MOUSEMOVE/LBUTTON*/RBUTTON*` → posición/deltas/botones de mouse en globals 0x46a348…0x46a360.

## 2. Timing
`timerWaitFrame40Hz` (0x412df0): QPC (fallback `timeGetTime`). Escala a **163840 ticks/s** (constante double en 0x45d1c0 / freq; con `timeGetTime` ×163.84). Hace spin hasta que `dt ≥ 0x1000` (= **25 ms → 40 Hz máx**); si `dt > 0xF000` (375 ms) usa el dt anterior. Resultado en `DAT_00468f8c/90` (dt en 4.12 fixed, 0x1000 = 1 frame). `timeGetTime` también para autorepeat de teclas (300 ms inicial, 50 ms repetición) en `inpUpdate`.

## 3. Render — spec de entrada de HW-010
Método: volcado de **todas** las llamadas indirectas del binario (264 `CALL [reg+off]`) con los `PUSH` previos, y lectura de los `MOV [esp+x],imm` que arman `DDSURFACEDESC2` (dwSize 0x7c, dwFlags +4, alto +8, ancho +0xc, backbuffers +0x14, pixfmt +0x48, `ddsCaps.dwCaps` +0x68, `dwCaps2` +0x6c). **Todas** las llamadas DDraw/D3D están en 0x444a90–0x448220 (más los paquetes de §3.1); fuera de ese rango sólo hay DSound/DInput/CRT y vtables C++ de la UI (0x40fe10, 0x424f90, 0x425fb0, 0x426a00, 0x427470).

### 3.0 Arquitectura: paquetes de primitivas con layout PsyQ
El motor genera **paquetes de primitivas cuyo layout coincide byte a byte con `libgpu` de PsyQ** [estático]. Que el motor provenga de código de PlayStation es una **inferencia** (sólo se verificó compatibilidad de layout, no procedencia) (código en +7, `x,y` s16, `u,v` u8, `clut` u16, `tpage` u16) y los despacha uno por uno a `gpuDrawPacket` (0x41c1e0):
- si `dd.is3D` (0x473c9c) && hay `d3d` (0x473c98) → `d3dDrawPrim` (0x445520);
- si no → **rasterizador software** propio (`swRasterFT3` 0x41d5a0 opaco, `swRasterFT3Semi` 0x420d30 con tablas de mezcla por ABR construidas en `buildShadeTables`), escribiendo en una superficie 8 bpp en memoria de sistema.

| code | primitiva PsyQ | D3D | SW |
|---|---|---|---|
| 0x0f | (propio) área de dibujo: rect 12+12 bit empaquetado en +4/+8 | crea/cachea `IDirect3DViewport3` del rect y `SetCurrentViewport` | fija clip/offset del framebuffer |
| 0x24 / 0x26 | `POLY_FT3` / semitransp. | 3 vért. | sí |
| 0x2c / 0x2e | `POLY_FT4` / semitransp. | 4 vért. | sí |
| 0x34 / 0x36 | `POLY_GT3` / semitransp. | 3 vért., **color Gouraud descartado** | sí (usa el color) |
| 0x3c / 0x3e | `POLY_GT4` / semitransp. | 4 vért., color descartado | sí |
Otros códigos: se ignoran en D3D. Coordenadas de pantalla virtual 320×240 (límites de tamaño de primitiva `w*0x834/0x140`, `h*1000/0xf0`); rechaza vértices con |coord| > 0x1000. Página de textura = `tpage & 0x1f` (máx 32 páginas, "Too many texture pages!!!"); modo semitransparente = `(tpage & 0x60) >> 5` si el bit 1 del código está prendido, si no 3 (opaco).

### 3.1 Llamadas de dibujo por frame (D3D)
`frameBegin` (0x447ca0): D3D → `d3dClear` + `d3dBeginScene`; SW → `ddLockSurfaces`.
| Llamada (vtable) | Argumentos concretos |
|---|---|
| Dev3::SetTextureStageState (+0xa0) | `(0, D3DTSS_COLOROP=1, D3DTOP_SELECTARG1=2)` — **sólo textura, sin modular** (una vez por primitiva) |
| Dev3::SetTexture (+0x98) | `(0, IDirect3DTexture2* de la página tpage&0x1f)` |
| Dev3::SetCurrentViewport (+0x30) | viewport del último paquete 0x0f (sólo si cambió) |
| Dev3::SetRenderState (+0x58), cacheado por modo ABR | ABR0: `SRCBLEND(0x13)=DESTCOLOR(9)`, `DESTBLEND(0x14)=SRCCOLOR(3)`, `ALPHABLENDENABLE(0x1b)=1` · ABR1: `SRC=INVDESTCOLOR(10)`, `DEST=ONE(2)`, blend 1 · ABR2: `SRC=ZERO(1)`, `DEST=INVSRCCOLOR(4)`, blend 1 · ABR3/opaco con textura con alfa: `SRC=SRCALPHA(5)`, `DEST=INVSRCALPHA(6)`, blend 1 · ABR3 con textura sin alfa: `ALPHABLENDENABLE=0` (colorkey hace el recorte) |
| Dev3::DrawPrimitive (+0x70) | `(D3DPT_TRIANGLESTRIP=5, FVF 0x104 = XYZRHW|TEX1, verts, 3 ó 4, D3DDP_DONOTLIGHT=0x10)`. Vértice 24 B: `x,y` (float de s16), `z` (no se escribe), `rhw=1.0`, `u,v = byte * k` (tabla 0x4727ec, k en 0x45d60c). **Única** llamada DrawPrimitive del exe. Sin Z-buffer: orden = orden de paquetes (painter / ordering table). |
| Dev3::BeginScene (+0x24) / EndScene (+0x28) | sin args |
| Dev3::SetCurrentViewport + Viewport3::Clear (+0x30) | `Clear(0, NULL, D3DCLEAR_TARGET=1)` (`d3dClear` 0x445340) |
| Surface4::Flip (+0x2c) primaria | `(NULL, DDFLIP_WAIT=1)` (`ddFlip` 0x447db0) |
| Surface4::Restore (+0x6c) | primaria y back si `DDERR_SURFACELOST` (`ddRestoreSurfaces`, y tras BltFast) |
| DD4::WaitForVerticalBlank (+0x58) | `(DDWAITVB_BLOCKBEGIN=1, 0)` — 32 veces seguidas en transiciones (`ddWaitVBlank`) |

### 3.2 Creación y estados fijos
| Llamada | Argumentos concretos | Dónde |
|---|---|---|
| `DirectDrawEnumerateExA` | `(ddEnumCallback, ctx, DDENUM_ATTACHEDSECONDARYDEVICES=4)` | ddInit |
| `DirectDrawCreate` | `(guid del driver o NULL, &dd, NULL)` | ddProbeDriver, ddCreateDevice |
| IDirectDraw::QI (+0) | `IID_IDirectDraw4 {9c59509a-39bd-11d1-8c4a-00c04fd930c5}`; luego Release del IDirectDraw | idem |
| DD4::QI | `IID_IDirect3D3 {bb223240-e72b-11d0-a9b4-00aa00c0993e}` | d3dQueryD3D3, ddProbeDriver |
| DD4::SetCooperativeLevel (+0x50) | `(hwnd, DDSCL_FULLSCREEN|ALLOWREBOOT|EXCLUSIVE = 0x13)` | ddCreateDevice, ddProbeDriver |
| DD4::EnumDisplayModes (+0x20) | `(0, NULL, ctx, ddEnumModesCallback 0x446b00)` | ddProbeDriver |
| DD4::SetDisplayMode (+0x54) | probe: `(640, 480, 16, 0, 0)`; real: `(w, h, bpp, 0, 0)` con modos de §3.3 | ddProbeDriver, ddSetDisplay |
| DD4::CreateSurface (+0x18) probe | `dwFlags=DDSD_CAPS`, `caps=PRIMARYSURFACE|3DDEVICE (0x2200)` | ddProbeDriver |
| DD4::CreateSurface primaria | `dwFlags=CAPS|BACKBUFFERCOUNT (0x21)`, `backbuffers=1`, `caps=PRIMARYSURFACE|FLIP|COMPLEX (0x218)`, `|3DDEVICE (0x2218)` si D3D | ddSetDisplay |
| Surface4::GetAttachedSurface (+0x30) | `caps=DDSCAPS_BACKBUFFER (4)` | ddSetDisplay |
| Surface4::GetPixelFormat (+0x54) | del backbuffer → copia el pixfmt a las offscreen | ddSetDisplay |
| DD4::CreateSurface offscreen ×2 ("OffScreen", "Temporary") | `dwFlags=CAPS|HEIGHT|WIDTH|PIXELFORMAT (0x1007)`, `caps=OFFSCREENPLAIN|SYSTEMMEMORY (0x840)`, w×h del modo, pixfmt del back | ddSetDisplay |
| Surface4::BltFast (+0x1c) | `(0, 0, offscreen|temporal, &rect{0,0,w,h}, DDBLTFAST_NOCOLORKEY=0)` → back | ddBltOffscreenToBack, ddBltTempToBack, ddSetDisplay |
| Surface4::Lock (+0x64) back/offscreen | `(NULL, &ddsd, 0, NULL)` (flags 0 = SURFACEMEMORYPTR, espera implícita); Unlock (+0x80) `(NULL)` | ddLockSurfaces / ddUnlockSurfaces / ddUnlockAll |
| DD4::CreatePalette (+0x14) | `(DDPCAPS_8BIT|ALLOW256 = 0x44, entries[256], &pal, NULL)`; `Palette::SetEntries (+0x18)(0, 0, 256, entries)`; `Surface4::SetPalette (+0x7c)(pal)` sobre la primaria | ddCreatePalette (sólo modos 8 bpp) |
| D3D3::CreateDevice (+0x20) | `(IID_IDirect3DHALDevice {84e63de0-46aa-11cf-816f-0000c020156e}, backbuffer, &dev3, NULL)` (también en probe) | d3dInitDevice |
| Dev3::EnumTextureFormats (+0x20) | `(d3dEnumTexFmtCallback 0x444fe0, lista)` → `d3dPickTextureFormat` | d3dInitDevice |
| DD4::GetAvailableVidMem (+0x5c) | `caps=TEXTURE|3DDEVICE (0x3000)`; si **free < 4 MB** → texturas 8 bpp palettizadas | d3dPickTextureFormat |
| Dev3::SetRenderState (+0x58) fijos | `CULLMODE(0x16)=NONE(1)`; `COLORKEYENABLE(0x29)=TRUE` si el formato elegido no tiene alfa; `TEXTUREMAG(0x11)=LINEAR(2)`; `TEXTUREMIN(0x12)=LINEAR(2)`. **Nunca** toca Z, fog, shade, dither, specular. | d3dInitDevice |
| D3D3::CreateViewport (+0x18) / Dev3::AddViewport (+0x14) / Viewport3::SetViewport2 (+0x44) | `D3DVIEWPORT2{dwSize 0x2c, X,Y,W,H del paquete 0x0f, dvClipX=-1, dvClipY=aspect, dvClipW=2, dvClipH=2·aspect, MinZ 0, MaxZ 1}` | d3dViewportCreate (uno por rect distinto, cacheados en lista) |
| Dev3::DeleteViewport (+0x18) + Release | destrucción | d3dViewportDestroy, d3dDtor |

Orden de preferencia de formato de textura (`d3dPickTextureFormat`, campos del registro: R/G/B/A bits en +0x28..+0x2b, bpp +0x2c, palettizado +0x41):
0) **8 bpp palettizado** (sólo si free < 4 MB) → 1) **ARGB 1555** → 2) ARGB 8888 → 3) ARGB 4444 → 4) cualquier 16 bpp con alfa → 5) cualquier 32 bpp con alfa → 6) RGB 565 → 7) "16 bpp 8/8/8" (nunca matchea; bug del original).
→ **Para el shim Vita: anunciar ARGB1555 (y reportar ≥ 4 MB libres)**; con alfa no se activa colorkey y el opaco usa `SRCALPHA/INVSRCALPHA`.

Texturas (`dxSurfCreateTexture` 0x446110, una por página de 256×256):
| Llamada | Argumentos |
|---|---|
| DD4::CreateSurface | `dwFlags=CAPS|HEIGHT|WIDTH|PIXELFORMAT|CKSRCBLT (0x11007)`, 256×256, `caps=TEXTURE (0x1000)`, `caps2=DDSCAPS2_TEXTUREMANAGE (0x10)`, colorkey src = {0,0}, pixfmt = el elegido |
| Surface4::Lock | `(NULL, &ddsd, DDLOCK_WAIT|WRITEONLY|NOSYSLOCK = 0x821, NULL)`; conversión índice→formato por software; `Unlock(NULL)` |
| (8 bpp) DD4::CreatePalette + Surface4::SetPalette | `0x44`, paleta global; al destruir `SetPalette(NULL)` |
| Surface4::QI | `IID_IDirect3DTexture2 {93281502-8cf8-11d0-89ab-00a0c9054129}` → se usa sólo en `SetTexture`; **ningún otro método de Texture2** (ni GetHandle ni Load) |

Métodos **no usados** (el shim puede devolver `E_NOTIMPL`): Blt, GetDC, IsLost, SetColorKey, todo Texture2 salvo QI/Release, DrawIndexedPrimitive, VB, SetTransform, luces, materiales, Z-buffer.

### 3.3 Modos de video (`ddFindMode` 0x4472e0)
| índice (opción "resolución") | sin 3D | con 3D |
|---|---|---|
| 0 (GFX_320, **default**) | 320×240×8 | 320×240×16 |
| 1 (GFX_640) | 640×480×8 | 640×480×16 |
| 2 (GFX_800) | 800×600×8 | 800×600×16 |
El **menú siempre** usa `ddCreateDevice(0)` + modo 1 → **640×480×8 palettizado, sin D3D**, dibujado con Lock + BltFast de TGAs 8 bpp. La carrera usa la opción 3D (`DAT_004720a8`, default 0) y la resolución (`DAT_004720ac`, default 0); si D3D falla en `SetDisplayMode`, `videoSetResolution` cae a software solo (pone el flag en 0).

### 3.4 Recomendación: software 8 bpp primero (M3), D3D después (HW-010)
**Cuándo se elige SW**: siempre en menús/intro/carga; en carrera si la opción 3D está en NO (**default de fábrica**: `settings.dat` no existe en el install; defaults en `0x426a80`) o si el probe/creación D3D falla. Se puede forzar D3D escribiendo `settings.dat` (texto, primer entero = 3D) o con el menú, pero **no se puede evitar el camino 8 bpp**: los menús lo usan siempre.
Por eso:
1. **M3 rápido = DDraw 8 bpp**: implementar DD4 + Surface4 (primaria flip + back, offscreen en RAM, Lock/Unlock, BltFast, Flip, Restore, SetDisplayMode, paleta) y presentar con un blit 8bpp+paleta → sceGxm (o CPU→framebuffer RGBA). 320×240 = 76.800 B/frame; 640×480 = 307.200 B. Ese único path cubre intro, menús y carrera (con 3D=NO). Cero D3D.
2. **HW-010 (D3D3/Device3)** después, para carrera nítida: el subconjunto de §3.1–3.2 es diminuto (1 DrawPrimitive, ≤4 vértices, sin Z, sin transformaciones). Alternativa más eficiente a evaluar: hook a nivel `gpuDrawPacket` (paquetes PsyQ) y batchear en GXM, porque D3D emite **una DrawPrimitive por triángulo/quad**.
3. Riesgo del SW: costo de CPU del rasterizador corriendo bajo el dynarec en el A9 a 444 MHz; a 320×240 y 40 Hz debería entrar, a 640×480 hay que medir (Vita3K/consola). El path D3D además pierde el color Gouraud (GT3/GT4) que el SW sí aplica: visualmente el SW es el "de referencia".

## 4. Audio
- **DirectSound** (`dsInit` 0x44efd0): `DirectSoundCreate(NULL)`, `SetCooperativeLevel(DSSCL_EXCLUSIVE=3)`, buffer primario con `SetFormat(PCM 22050 Hz, 16 bit, estéreo; 88200 B/s)`. Efectos = buffers secundarios estáticos por WAV (`dsLoadWav`: mmio `RIFF/WAVE` → `CreateSoundBuffer` → `Lock/memcpy/Unlock`), voces vía `DuplicateSoundBuffer`; control `Play/Stop/SetCurrentPosition/SetFrequency/SetVolume/SetPan/GetStatus`. Sin DirectSound3D, sin streaming. ~45 WAV en `data\sound\{game0,game1,vehicles0,vehicles1}`, `data\menu\sound`.
- **Música = CD audio por MCI** (`cdOpen` 0x44e690): `MCI_OPEN` type `"cdaudio"` element = letra del CD; `MCI_SET` time format MSF; `cdReadTOC` con `MCI_STATUS` (nº pistas, posición/largo por pista); `cdPlayTrack` `MCI_PLAY FROM|TO`; `cdPause` `MCI_PAUSE`; `MCI_CLOSE`. → En Vita: reemplazar por pistas OGG/WAV (como `_inmm.dll` del repack) detrás de un shim de `mciSendCommandA`.

## 5. Input (DirectInput 6/5)
`inpInit` (0x41b6a0): `DirectInputCreateA`, `CreateDevice` teclado y mouse, `SetDataFormat`, `SetCooperativeLevel`, `Acquire`. `inpEnumJoysticks` (`EnumDevices`) → `inpInitJoystick`: `CreateDevice` + `QI(IID_IDirectInputDevice2A)`, `SetDataFormat`, `SetCooperativeLevel`, `SetProperty`×4 (rangos/deadzone ejes X/Y/Z), `Acquire`; lee `dwFFMinTimeResolution` (caps force-feedback, sólo log). `inpUpdate` por frame: teclado `GetDeviceState(256)` (re-`Acquire` si falla), joystick `Poll`+`GetDeviceState`. Mouse además por mensajes Win32. Cheats tipeados (`allcars`, `nocars`, `alldiff`, `nodiff`, `nokeys`, `onelap`, `manylaps`) en `cheatTypeBuffer`.
→ En Vita: shim DInput teclado+joystick alimentado por `sceCtrl` (2 jugadores: split-screen).

## 6. Archivos y formatos
- `fileLoadWhole` (0x410c60): `fopen` rb, `fseek/ftell` tamaño, `memPoolAlloc(pool, size)`, `fread` todo. Pools con nombre (`memPoolCreate`: "SINUS", "MOONMEM", "MENUPIC", "TEXDATA", "MENUSPRITES"…; máx 256).
- Escrituras: `settings.dat`, `highscore.dat`, screenshots `%s%d.tga` (`FindFirstFile`/`DeleteFile`), `C:\DEBUG.TXT` (debug).
- Formatos (headers inspeccionados con xxd):
  | Ext | Uso | Observación |
  |---|---|---|
  | `.SFP` | pistas, autos, objetos de menú | Árbol de chunks IFF-like, ver §10 y `hotwheels/tools/sfp_dump.py`. |
  | `.TAB` | `shade.tab` por pista/menú | 360.448 B = 0x58000: tablas de sombreado/mezcla de paleta. |
  | `.SIN` | `sinus.sin` (32.770 B, s16 binario), `ASIN.SIN` (texto) | tablas trigonométricas. |
  | `.TGA` | HUD, menús, loading, intro | TGA estándar (308.012 B ≈ 640×480 8 bit + paleta). |
  | `.DAT` | `acc.dat`, `physics.dat` (texto floats), `Ai.dat` (bin), settings/highscore | |
  | `.TXT` | `hw_small/medium.txt` (fuentes/métricas?), `lang.txt` | |
  | `.BIN` | `shock.bin` (shockwave) | |
- **`.FRM`/`.FSS` no son del juego**: están sólo en `Program_Files/EREG/` (registro electrónico de Mattel). Fuera de alcance.

## 7. Funciones renombradas (Ghidra)
WinMain 4503a0, WndProc 4507a0, isAlreadyRunning 450970, initHotWheels 450170, state_initial 423470 (creada), timerWaitFrame40Hz 412df0, inpUpdate 41bb80, inpMouseLatch 41bb50, inpInit 41b6a0, inpEnumJoysticks 41b890, inpInitJoystick 41b930, inpReleaseJoystick 41bb10, inpShutdown 41bfe0, ddInit 446eb0, ddCreateDevice 446f40, ddEnumCallback 446d50, ddProbeDriver 446910, ddSetDisplay 4473a0, ddCreatePalette 447a30, ddFlip 447db0, ddRestoreSurfaces 4472a0, ddShutdown 4471c0, dxLogHResult 4483f0, d3dCtor/Dtor 444bc0/444c00, d3dQueryD3D3 444cc0, d3dInitDevice 444d00, d3dViewportCreate 444a90, d3dPickTextureFormat 445010, d3dBeginScene 4452e0, d3dEndScene 445310, d3dClear 445340, d3dDrawPrim 445520, dxSurfCreateTexture 446110, dsInit 44efd0, dsLoadWav 44f0f0, dsShutdown 44f0d0, dsPlayVoice 44fa00, dsGetBuffer 44fbf0, wavOpenMmio 44ffb0, wavReadMmio 44fc60, cdOpen 44e690, cdClose 44e710, cdReadTOC 44ea00, cdPlayTrack 44e750, cdPause 44e8d0, cheatTypeBuffer 44ecf0, fileLoadWhole 410c60, memPoolCreate 423020, memPoolAlloc 4230d0, memPoolFree 423340, buildShadeTables 404cf0, settingsLoad 426bf0, settingsSave 426d60, screenshotInit/Save 44b990/44b9b0, shockwaveLoad 449140, vehicleLoad 441bf0, nop_checkpoint 448510.

## 8. Funciones renombradas (2ª tanda)
gpuDrawPacket 41c1e0, swRasterFT3 41d5a0, swRasterFT3Semi 420d30, ddFindMode 4472e0, ddFindModeWHB 446b70, ddEnumModesCallback 446b00, d3dEnumTexFmtCallback 444fe0, videoModeMake 404660, videoSetResolution 404700, menuShutdown 424550, state_menuEnter 4253b0, state_menuLoop 424900, menuAttractTimer 424740, state_intro0/1/2 443ab0/443b30/443c90, state_raceLoad 4111b0, state_raceLoop 411d70, state_raceTeardown 4123d0, sfpLoadDefault 43b650, sfpLoad 43b670, sfpChunkBegin 43cc30, sfpChunkHasMore 43cc90, sfpChunkSkip 43cd00, texPageCreate 4048b0, d3dSetTexPage 4453b0, ddLockSurfaces 447b60, ddUnlockSurfaces 447c40, ddBltOffscreenToBack 447e10, ddBltTempToBack 4480d0, ddWaitVBlank 447d90, ddUnlockAll 448180, frameBegin 447ca0, d3dViewportDestroy 444b80, picLoadSlot 427800, objFindByName 43e150, menuBindVar 426970, menuAddItem 426a00.

## 9. Máquina de estados
`DAT_00475108` = estado actual (lo llama el loop de WinMain), `DAT_00475104` = hook de salida. Escrituras encontradas buscando `mov [imm32], imm32` en el exe crudo (17 sitios):
```
initHotWheels ──> state_initial 0x423470   carga sonidos/TGA/SFP de menú, shade.tab, arma menús,
                  │                         ddCreateDevice(0) + modo 640x480x8; exit=menuShutdown
                  ├─> state_intro0 0x443ab0 ─> state_intro1 0x443b30 ─> state_intro2 0x443c90 ─┐  (mattel/uds/intro.tga)
                  └─────────────────────────────────────────────────────────────────────────────┤
state_menuEnter 0x4253b0 <──────────────────────────────────────────────────────────────────────┘
   │  arma escena 3D del menú (autos girando), elige gráficos según input
   └─> state_menuLoop 0x424900 (CD-audio, navegación, attract mode, screenshot) ⇄ state_menuEnter
         ├─ EXIT  → state = menuShutdown 0x424550 → libera, recrea DD según opción 3D, guarda settings, state=0 → fin del loop
         └─ START → menuShutdown() directo, state = state_raceLoad
state_raceLoad 0x4111b0   pista (wave/slumber/office): hud, shade.tab, hw_*.txt, SFP, WAV; videoSetResolution(modo opción); exit=state_raceTeardown
   └─> state_raceLoop 0x411d70 ─> state_raceTeardown 0x4123d0 ─> state_initial (exit=0)
```
Opciones (bindings `menuBindVar` en state_initial): `0x4720b0` dificultad (EASY/MEDIUM/HARD), `0x4720ac` resolución (320/640/800), `0x4720a8` 3D (NO/YES), `0x4722bc/0x4722c0` selección de auto/pista, controles por jugador (KEYBOARD/MOUSE/JOYPAD). Teclas por defecto (DIK): J1 ↑↓←→ + RShift + RCtrl; J2 T G F H + LShift + LCtrl.

## 10. Formato SFP [estático, validado]
Árbol de chunks `{u32 size (incluye los 8 B de header), u32 id}` (lector: `sfpChunkBegin`/`sfpChunkHasMore`/`sfpChunkSkip`). Contenedores: **1** raíz (size = tamaño del archivo), **2** texturas, **4** objetos. `tools/sfp_dump.py` recorre los **9 SFP** del juego y todos cierran exacto (`OK`).
| id | padre | contenido | evidencia |
|---|---|---|---|
| 3 | 2 | **página de textura**: u32 flags (siempre 0x00010003) + 256×256 índices 8 bpp + paleta 256×{R,G,B,pad(0xcd)} = 0x10404 B. Sólo la **primera** página cargada fija la **paleta global** (0x473cda) y el índice del negro (colorkey) | texPageCreate 0x4048b0 + bytes |
| 14 | 2 | bloque; se conserva sólo si `sfpLoad` recibió un descriptor `.dat`/`.DAT` (texto con clave `SFP` que nombra el .sfp); si se abrió el .sfp directo, se saltea | sfpLoad + strings 0x462410–0x462420 |
| 15 | 2 | registros de 12 B; misma condición que 14 | sfpLoad |
| 5 | 4 | bloque grande (geometría/polígonos), en pool del llamador | sfpLoad |
| 6 | 4 | registros de 0x30 B (objetos con nombre `GFX_*`/`NUL_*`/`APA_*`/`ANM_*`, buscados con `objFindByName`) | sfpLoad + strings |
| 7 | 4 | bloque | sfpLoad |
| 8, 9, 10, 12 | 4 | registros de 0x2c / bloque / registros de 0x28 / bloque | sfpLoad |
| 11 | 4 | malla de física ("Physics, polys: %i, verts: %i", FUN_00430090); sólo en pistas | sfpLoad |
| 13 | 4 | registros de 0x1c; sólo en VEHICLES.SFP | sfpLoad |
Páginas por archivo: OFFICE 24, WAVE 25, SLUMBER 18, menú OBJECTS 12, CARS 8, VEHICLES 4, loading OBJECTS 1.
Layout interno de 5/6/7/11 y el **layout del paquete de primitiva (PsyQ)**: ver **`docs/re-chunks.md`** (2026-10-22).

## 11. Pendientes
- ~~Layout de los chunks 5/6/7/11~~ → **hecho** (parcial): `docs/re-chunks.md`. Chunk 11 (física) **decodificado por
  completo y verificado** contra los datos (cabecera de 12 u32 con 6 offsets relativos + counts; arrays de registros de
  16 B = {s32 x, s32 y, s32 z} en punto fijo 1/16). Chunk 5 = grafo de escena con punteros relativos (fixup en
  `0x43bd40`…`0x43bf60`); chunk 6 = registros de 0x30 B. **Abierto**: semántica de cada puntero del chunk 5 y el offset
  del nombre dentro del registro de chunk 6. El transform 3D→2D (paquetes PsyQ) es del motor, en software.
- Confirmar con Wine (HW-002): que la carrera por defecto corre 320×240×8 SW y la secuencia de llamadas de §3.
- DSound/DInput con argumentos completos (no pedidos en esta tanda).
