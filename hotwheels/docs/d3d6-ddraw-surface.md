# Referencia técnica — shim DirectDraw 4 / Direct3D 6 para Hot Wheels: Micro Racers

- Autor: `investigador` (opencode deepseek)
- Fecha: 2026-10-02
- Objetivo: ser la referencia para escribir el shim `DDRAW.dll` + `D3D6` del port a PS Vita
  (feature HW-006). No es código; es contrato ABI + semántica + dónde mirar.
- Niveles de evidencia:
  - `[estático]` medido acá: cabeceras mingw-w64 locales, parseo del PE real `HotWheels.exe`,
    hexdump de assets instalados por Wine.
  - `[wine]` código/arquitectura de Wine (`dlls/ddraw`, `wined3d`), fuente citada.
  - `[dgvoodoo]` documentación oficial de dgVoodoo2.
  - `[web]` fuentes citadas al pie.
- Cabeceras de referencia (locales, `[estático]`):
  `~/opt/llvm-mingw-.../generic-w64-mingw32/include/{ddraw.h,d3d.h,d3dtypes.h,d3dcaps.h}`
  (equivalentes a los headers de Windows SDK; los índices de vtable de este doc salen de acá,
  contando los 3 métodos de `IUnknown`).

---

## 0. TL;DR — qué tiene que exponer el shim

`[estático]` El `HotWheels.exe` real (430.080 bytes, PE32 GUI i386, 3 secciones, sin overlay,
mtime 2000-01-25) tiene **una import table de solo 6 DLL**:

| DLL | Imports |
|---|---|
| `DDRAW.dll` | `DirectDrawCreate`, `DirectDrawEnumerateExA` |
| `DSOUND.dll` | `DirectSoundCreate` (ordinal #1) |
| `DINPUT.dll` | `DirectInputCreateA` |
| `WINMM.dll` | `mciSendCommandA`, `mmioOpenA/Read/Close/Descend/Ascend/Seek/SetInfo/GetInfo/Advance`, `timeGetTime` |
| `KERNEL32.dll` | 64 funciones (archivos, heaps, hilos, `GetVersion`, `SetUnhandledExceptionFilter`, …) |
| `USER32.dll` | 15 funciones (ventana, `PeekMessageA`, `DispatchMessageA`, …) |

No hay import de `d3dim.dll` ni de `GDI32`. **Direct3D se obtiene por COM**: el juego llama
`DirectDrawCreate` → `IDirectDraw4::QueryInterface(IID_IDirect3D3)` y de ahí
`IDirect3D3::CreateDevice`. Los strings de errores del binario confirman las versiones usadas:
`IDirectDraw4`, `IDirectDrawSurface4`, `IDirect3DDevice3`, `IDirect3DViewport3` `[estático]`.

Consecuencias para el shim:
1. Hay que implementar **COM real** (vtable + `QueryInterface` + refcount) para
   `IDirectDraw4`, `IDirectDrawSurface4`, `IDirect3D3`, `IDirect3DDevice3`,
   `IDirect3DViewport3`, `IDirect3DTexture2`, `IDirect3DMaterial3`, `IDirect3DLight`.
2. El path de render es **Device3 immediate mode**: `SetRenderState`, `SetTextureStageState`,
   `SetTransform`, `SetTexture`, `DrawPrimitive` / `DrawIndexedPrimitive`. (Los
   *execute buffers* de `IDirect3DDevice` v1 no se usan: Device3 ya no los tiene.)
3. La presentación es **DirectDraw puro**: superficie primaria + back buffer + `Flip`, o
   `Blt` desde una superficie de trabajo. Hay que decidir cuál usa el juego (traza Wine).
4. Audio: `DirectSound` (efectos) + **Red Book CD audio vía `mciSendCommandA`** (música),
   que `_inmm` redirige a `MUSIC/Track{1..4}.wav`.

Datos de contexto `[web]` (PCGamingWiki): API **Direct3D 6**; resolución por defecto
**800x600** (hex-edit documentado sobre `HotWheels.exe`); FPS cap 60 en menús / 40 en juego;
audio de CD Red Book; **DRM = "Simple disc check"** (requiere el CD en la unidad).

---

## 1. ABI COM (contrato que hay que respetar)

- Todas las interfaces son **`stdcall`** en x86. En ARMv7 el shim es código nativo del port; el
  guest x86 llama a través del `Bridge`/trampolín de imports, así que la convención la maneja
  el runtime winx86. Lo importante es el **layout de la vtable**.
- Estructura de un objeto COM:
  ```
  struct ICom { void **vtbl; }   // pIface apunta al slot 'vtbl'
  // pIface->lpVtbl[k] es el método k-ésimo (0-based), contando IUnknown.
  ```
- Toda vtable empieza con los 3 de `IUnknown`:
  `0 QueryInterface`, `1 AddRef`, `2 Release`. Los índices de este doc los incluyen.
- `QueryInterface` recibe un `REFIID` (16 bytes, GUID) y un `void**`. El shim debe comparar
  contra los **IID** de la sección 8 y devolver `S_OK`/`E_NOINTERFACE`.
- `AddRef`/`Release` devuelven `ULONG`. `Release` a 0 debe liberar. No implementar refcount real
  suele funcionar para un juego single-thread, pero conviene hacerlo bien (el juego puede
  crear/destruir texturas y viewports varias veces).
- Todos los métodos devuelven `HRESULT`. Los que usan `[out]` retornan `S_OK` y llenan el
  puntero. Un HRESULT negativo (`DDERR_*`/`D3DERR_*`) puede ser normal (el juego prueba cosas
  y reacciona); no asumir crash.

---

## 2. `IDirectDraw4` (creado por `DirectDrawCreate`)

`[estático]` `DirectDrawCreate(GUID *lpGUID, LPDIRECTDRAW *lplpDD, IUnknown *pUnkOuter)`
devuelve un `IDirectDraw` v1; el primer `QueryInterface(IID_IDirectDraw4)` da la v4 (es la que
usa el juego). `DirectDrawEnumerateExA` se usa para elegir driver; el shim puede devolver el
driver por defecto o ninguno.

### 2.1 Vtable `IDirectDraw4` (índice → método)

| # | Método | Relevancia |
|---|---|---|
| 0 | `QueryInterface` | **crítico**: `IID_IDirect3D3`, `IID_IDirectDraw4` |
| 1 | `AddRef` | |
| 2 | `Release` | |
| 3 | `Compact` | stub `S_OK` |
| 4 | `CreateClipper` | probable (fuente de `Blt`) |
| 5 | `CreatePalette` | probable (assets `.TGA`/`.BMP`/UI 8-bit) |
| 6 | `CreateSurface` | **crítico** |
| 7 | `DuplicateSurface` | poco probable |
| 8 | `EnumDisplayModes` | probable (menú de opciones) |
| 9 | `EnumSurfaces` | poco probable |
| 10 | `FlipToGDISurface` | probable (menús GDI) |
| 11 | `GetCaps` | probable (elegir HW/SW) |
| 12 | `GetDisplayMode` | probable |
| 13 | `GetFourCCCodes` | stub |
| 14 | `GetGDISurface` | posible |
| 15 | `GetMonitorFrequency` | stub |
| 16 | `GetScanLine` | posible (sync) |
| 17 | `GetVerticalBlankStatus` | posible |
| 18 | `Initialize` | stub `DDERR_ALREADYINITIALIZED` |
| 19 | `RestoreDisplayMode` | **crítico al salir** |
| 20 | `SetCooperativeLevel` | **crítico** (`DDSCL_FULLSCREEN|EXCLUSIVE`) |
| 21 | `SetDisplayMode` | **crítico** (800x600, 16bpp) |
| 22 | `WaitForVerticalBlank` | probable |
| 23 | `GetAvailableVidMem` | probable (v1.6 MiB) |
| 24 | `GetSurfaceFromDC` | stub |
| 25 | `RestoreAllSurfaces` | `S_OK` |
| 26 | `TestCooperativeLevel` | `S_OK` (no perdemos device) |
| 27 | `GetDeviceIdentifier` | llenar con dummy "dgVoodoo-ish" |

`IDirectDraw` (v1) = mismos índices 0–22; `IDirectDraw2` = + `GetAvailableVidMem` (23);
`IDirectDraw4` agrega 24–27. `IDirectDraw7` agrega más (no la usa el juego). `IDirectDraw3` es
un interface de IE que vive en `ddrawex.dll`, no en `ddraw.dll` `[wine]`
(https://gitlab.winehq.org/wine/wine/-/wikis/DirectDraw).

### 2.2 Semántica clave

- `SetCooperativeLevel(hWnd, flags)`: el juego pedirá `DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
  DDSCL_ALLOWMODEX?`. El shim puede aceptar, guardar el HWND y **no** tocar el display real
  (en Vita el "display" es el framebuffer GXM). Devolver `DD_OK`.
- `SetDisplayMode(w, h, bpp, rate, flags)`: en DirectDraw4 la firma es
  `(DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags)`. Esperar **800x600** (default) o
  **640x480**, **16** o **32** bpp. El shim mapea a tamaño del render target GXM.
- `CreateSurface(LPDDSURFACEDESC2, LPDIRECTDRAWSURFACE*, IUnknown*)`: la superficie primaria
  viene con `ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE` (+ `DDSCAPS_COMPLEX | DDSCAPS_FLIP` si
  pide back buffer y `dwBackBufferCount=1`). El shim crea:
  - una **texture/render target GXM** del tamaño del display (o el que pida `dwWidth/dwHeight`),
  - expone `IDirectDrawSurface4` para primaria y back buffer,
  - el back buffer se obtiene con `IDirectDrawSurface4::GetAttachedSurface(&caps, &surf)` con
    `DDSCAPS_BACKBUFFER`.
- `GetSurfaceDesc` / `SetSurfaceDesc`: `DDSURFACEDESC2` (layout abajo). Importante devolver
  `dwWidth/dwHeight/lPitch/ddpfPixelFormat/ddsCaps` coherentes porque el juego los lee para
  armar sus propios buffers y para `Lock`.

---

## 3. `IDirectDrawSurface4` (la superficie)

Es **la pieza más usada** del shim. Índices y relevancia:

| # | Método | Relevancia |
|---|---|---|
| 0 | `QueryInterface` | **crítico**: `IID_IDirect3D3` (el juego obtiene D3D desde la primaria), `IID_IDirectDrawSurface4` |
| 1 | `AddRef` / 2 `Release` | refcount |
| 3 | `AddAttachedSurface` | Z-buffer / back buffer |
| 4 | `AddOverlayDirtyRect` | stub |
| 5 | `Blt` | **crítico** (blit 2D con color key / stretch) |
| 6 | `BltBatch` | stub/no usado |
| 7 | `BltFast` | **crítico** (blit sin clip) |
| 8 | `DeleteAttachedSurface` | sí (libera Z/back) |
| 9 | `EnumAttachedSurfaces` | poco |
| 10 | `EnumOverlayZOrders` | stub |
| 11 | `Flip` | **crítico** (present) |
| 12 | `GetAttachedSurface` | **crítico** (back buffer, Z-buffer) |
| 13 | `GetBltStatus` | `DD_OK` |
| 14 | `GetCaps` | llenar |
| 15 | `GetClipper` / 28 `SetClipper` | probables |
| 16 | `GetColorKey` / 29 `SetColorKey` | **crítico** (transparencia de sprites) |
| 17 | `GetDC` / 26 `ReleaseDC` | **probable** (menús/GDI o blit por DIB) |
| 18 | `GetFlipStatus` | `DD_OK` |
| 19 | `GetOverlayPosition` / 30 `SetOverlayPosition` | stub |
| 20 | `GetPalette` / 31 `SetPalette` | probable (UI 8-bit) |
| 21 | `GetPixelFormat` | sí |
| 22 | `GetSurfaceDesc` | **crítico** |
| 23 | `Initialize` | stub |
| 24 | `IsLost` | `DD_OK` (nunca perdemos) |
| 25 | `Lock` / 32 `Unlock` | **crítico** (render por software / upload de texturas) |
| 27 | `Restore` | `DD_OK` |
| 33 | `UpdateOverlay` / 34 `UpdateOverlayDisplay` / 35 `UpdateOverlayZOrder` | stub |
| 36 | `GetDDInterface` | devolver el `IDirectDraw4` |
| 37 | `PageLock` / 38 `PageUnlock` | stub `DD_OK` |
| 39 | `SetSurfaceDesc` | posible |
| 40–44 | `Set/Get/FreePrivateData`, `Get/ChangeUniquenessValue` | stub (`D3D` usa private data para asociar texturas) |

### 3.1 `DDSURFACEDESC2` (campos + offsets) `[estático]`

Firma de `DDSURFACEDESC2` (ddraw.h:1052), offsets decimales:

```
 0  DWORD dwSize
 4  DWORD dwFlags          (DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PITCH|DDSD_PIXELFORMAT|DDSD_BACKBUFFERCOUNT|DDSD_LPSURFACE|DDSD_MIPMAPCOUNT)
 8  DWORD dwHeight
12  DWORD dwWidth
16  LONG  lPitch          (o dwLinearSize)
20  DWORD dwBackBufferCount (o dwDepth)
24  DWORD dwMipMapCount / dwRefreshRate / dwSrcVBHandle
28  DWORD dwAlphaBitDepth
32  DWORD dwReserved
36  LPVOID lpSurface       (memoria del lock, si DDSD_LPSURFACE)
40  DDCOLORKEY ddckCKDestOverlay
48  DDCOLORKEY ddckCKDestBlt
56  DDCOLORKEY ddckCKSrcOverlay
64  DDCOLORKEY ddckCKSrcBlt
72  DDPIXELFORMAT ddpfPixelFormat (o DWORD dwFVF para vertex buffers)
104 DDSCAPS2 ddsCaps (dwCaps en 104; dwCaps2 en 108)
120 DWORD dwTextureStage
```

`DDSURFACEDESC` v1 termina con `DDSCAPS` en offset 104; `DDSURFACEDESC2` también empieza
`DDSCAPS2` en 104 (sus campos extendidos siguen hasta offset 116). `CreateSurface` de
`IDirectDraw4` en la práctica recibe `DDSURFACEDESC2` (dwSize discrimina). El shim debe aceptar
**ambos** y mirar `dwSize`.

### 3.2 `DDSCAPS2` flags que el juego probablemente use `[estático]`

`DDSCAPS_PRIMARYSURFACE 0x200`, `DDSCAPS_BACKBUFFER 0x4`, `DDSCAPS_FRONTBUFFER 0x20`,
`DDSCAPS_FLIP 0x10`, `DDSCAPS_COMPLEX 0x8`, `DDSCAPS_OFFSCREENPLAIN 0x40`,
`DDSCAPS_TEXTURE 0x1000`, `DDSCAPS_ZBUFFER 0x20000`, `DDSCAPS_3DDEVICE 0x2000`,
`DDSCAPS_SYSTEMMEMORY 0x800`, `DDSCAPS_VIDEOMEMORY 0x4000`, `DDSCAPS_MIPMAP 0x400000`,
`DDSCAPS_ALPHA 0x2`. (ddraw.h:269-326.)

### 3.3 `Lock` / `Unlock`, `Blt` / `BltFast`, `Flip`

- `Lock(RECT*, DDSURFACEDESC2*, DWORD flags, HANDLE)`: llena `lpSurface` con la memoria
  direccionable y `lPitch` con el stride. En Vita es la vía natural para que el juego escriba
  píxeles (dibujo software de sprites/UI) o suba texturas. Después, un `Unlock` marca la
  región como dirty y se sube a la texture GXM.
  Flags `DDLOCK_*` (ddraw.h:1474): `WAIT 1`, `EVENT 2`, `READONLY 0x10`, `WRITEONLY 0x20`,
  `NOOVERWRITE 0x1000`, `DISCARDCONTENTS/OKTOSWAP 0x2000`, `DONOTWAIT 0x4000`.
  El shim puede ignorar casi todos y solo respetar `READONLY/WRITEONLY` si quiere optimizar.
- `Blt(dest, src, srcrect, dstrect, flags, DDBLTFX*)`: blit con **clipper**, **color key**
  (src/dst), **stretch** y **ROP** (`DDBLT_ROP` con `SRCCOPY`, `SRCAND`, …) y relleno
  (`DDBLT_COLORFILL`). Es lo que usan las hojas de sprites 2D. En Vita: mapear a un quad
  texturizado con blending/alpha-test; `COLORFILL` a `glClear`/quad sólido.
- `BltFast(dx, dy, src, srcrect, flags)`: variante sin clip ni stretch; el flag `DDBLTFAST_SRCCOLORKEY` activa la color key. Mapeo idéntico a `Blt` sin clipper.
- `Flip(surf, flags)`: present. El shim debe hacer swap del back buffer a GXM (o copiar al
  display buffer) y marcar el frame. En Vita el "vsync" se maneja con los swap intervals de
  GXM (`D3DRS_PRESENTINTERVAL`/equivalente vitaGL). Es el punto donde también se bombea el
  input del guest.

### 3.4 Color keys (`DDCOLORKEY`, ddraw.h:742)

```
struct DDCOLORKEY { DWORD dwColorSpaceLowValue, dwColorSpaceHighValue; } // rango inclusivo
```
Flags de `SetColorKey`: `DDCKEY_SRCBLT`, `DDCKEY_DESTBLT`, `DDCKEY_SRCOVERLAY`,
`DDCKEY_DESTOVERLAY`, `DDCKEY_COLORSPACE`. En la práctica: la **source color key** define
píxeles transparentes al blitear. Mapear a **alpha test** en el shader (comparar color y
descartar) o a un upload que ponga alpha=0 en esa clave. Ojo: los formatos 16-bit con color key
son el caso de artefactos típico si se fuerza bilinear filtering (lo advierte dgVoodoo
`[dgvoodoo]`).

### 3.5 Formatos de píxel (`DDPIXELFORMAT`, ddraw.h:775)

```
 0 DWORD dwSize
 4 DWORD dwFlags     (DDPF_RGB 0x40, DDPF_ALPHAPIXELS 0x1, DDPF_PALETTEINDEXED8 0x20, DDPF_ZBUFFER 0x400, ...)
 8 DWORD dwFourCC
12 DWORD dwRGBBitCount
16 DWORD dwRBitMask
20 DWORD dwGBitMask
24 DWORD dwBBitMask
28 DWORD dwRGBAlphaBitMask
```

Formatos que hay que soportar (los "16-bit" de la época):

| Nombre | `dwFlags` | bpp | R | G | B | A |
|---|---|---|---|---|---|---|
| RGB565 | `DDPF_RGB` | 16 | `0xF800` | `0x07E0` | `0x001F` | — |
| XRGB1555 | `DDPF_RGB` | 16 | `0x7C00` | `0x03E0` | `0x001F` | — (bit 15 ignorado) |
| ARGB1555 | `DDPF_RGB\|DDPF_ALPHAPIXELS` | 16 | `0x7C00` | `0x03E0` | `0x001F` | `0x8000` |
| ARGB4444 | `DDPF_RGB\|DDPF_ALPHAPIXELS` | 16 | `0x0F00` | `0x00F0` | `0x000F` | `0xF000` |
| XRGB8888 | `DDPF_RGB` | 32 | `0x00FF0000` | `0x0000FF00` | `0x000000FF` | — |
| ARGB8888 | `DDPF_RGB\|DDPF_ALPHAPIXELS` | 32 | `0x00FF0000` | `0x0000FF00` | `0x000000FF` | `0xFF000000` |
| PAL8 | `DDPF_PALETTEINDEXED8` | 8 | — | — | — | paleta |
| Z16/Z24 | `DDPF_ZBUFFER` | 16/32 | — | — | — | depth |

En Vita/GXM el formato nativo es **RGBA8888 / RGBA4444 / RGB565**; ARGB1555 no es nativo, así
que hay que **convertir** en el upload (o en shader) 1555→4444/8888. El 8-bit paletizado no
existe en GXM: se expande con `IDirectDrawPalette`/`GetPalette`.

---

## 4. Direct3D 6 (`IDirect3D3` / `IDirect3DDevice3`)

`[estático]` Ruta de obtención (COM, sin `d3dim.dll`):
```
DirectDrawCreate(NULL, &dd, NULL)          // IDirectDraw
dd->QueryInterface(IID_IDirectDraw4, &dd4)
dd4->SetCooperativeLevel(...); dd4->SetDisplayMode(800,600,16,...)
dd4->CreateSurface(&desc, &primary, NULL)  // DDSCAPS_PRIMARYSURFACE (+FLIP/COMPLEX)
primary->QueryInterface(IID_IDirect3D3, &d3d)   // <-- acá nace Direct3D
d3d->CreateDevice(IID_IDirect3DHALDevice|IID_IDirect3DRGBDevice, primary, &dev3, NULL)
d3d->CreateViewport(&vp, NULL); vp->SetViewport2(&vpdesc); dev3->AddViewport(vp)
dev3->SetCurrentViewport(vp)
```

`IID` de los device (d3d.h:39-44): `IID_IDirect3DRGBDevice` (software),
`IID_IDirect3DHALDevice` (HW), `IID_IDirect3DMMXDevice`, `IID_IDirect3DRefDevice`,
`IID_IDirect3DNullDevice`. El readme del juego menciona modo "Hardware/Software (icono 3D
Card)" ⇒ esperar que pruebe HAL y caiga a RGB. El shim debería aceptar **ambos** y renderizar
igual (en Vita no hay "HW vs SW" real a nivel API).

### 4.1 Vtable `IDirect3D3`

| # | Método | Nota |
|---|---|---|
| 0-2 | IUnknown | QI a `IID_IDirect3D3`, `IID_IUnknown` |
| 3 | `EnumDevices(cb, ctx)` | enumerar 1 device HAL y 1 RGB (el juego elige) |
| 4 | `CreateLight(**light, outer)` | devolver `IDirect3DLight` |
| 5 | `CreateMaterial(**mat, outer)` | `IDirect3DMaterial3` |
| 6 | `CreateViewport(**vp, outer)` | `IDirect3DViewport3` |
| 7 | `FindDevice(search, result)` | llenar con el HAL |
| 8 | `CreateDevice(rclsid, surface, **dev3, outer)` | **crítico** |
| 9 | `CreateVertexBuffer(desc, **vb, flags, outer)` | probable |
| 10 | `EnumZBufferFormats(iid, cb, ctx)` | puede que no se use (Z16 típico) |
| 11 | `EvictManagedTextures()` | `S_OK` |

`IDirect3D` (v1) = índices 3–8; `IDirect3D2` = 3–10 (agrega `CreateDevice`); `IDirect3D3` = los
de arriba (agrega `CreateVertexBuffer`, `EnumZBufferFormats`, `EvictManagedTextures`).

### 4.2 Vtable `IDirect3DDevice3` (42 métodos)

| # | Método | Nota |
|---|---|---|
| 0-2 | IUnknown | QI a `IID_IDirect3DDevice3` |
| 3 | `GetCaps(desc, desc)` | llenar `D3DDEVICEDESC` (ver §4.7) |
| 4 | `GetStats` | `S_OK` |
| 5 | `AddViewport(vp)` | guardar |
| 6 | `DeleteViewport(vp)` | |
| 7 | `NextViewport(ref, **vp, flags)` | enumerar viewports |
| 8 | `EnumTextureFormats(cb, ctx)` | **crítico**: enumerar 565/1555/4444/8888 |
| 9 | `BeginScene()` | **crítico** (bracket de frame) |
| 10 | `EndScene()` | **crítico** |
| 11 | `GetDirect3D(**d3d)` | devolver el `IDirect3D3` |
| 12 | `SetCurrentViewport(vp)` | **crítico** |
| 13 | `GetCurrentViewport(**vp)` | |
| 14 | `SetRenderTarget(surf, flags)` | **crítico** (cambiar a render-target texture) |
| 15 | `GetRenderTarget(**surf)` | |
| 16 | `Begin(pt, fvf, flags)` | path "Begin/Vertex/End" (draw inmediato) |
| 17 | `BeginIndexed(pt, fvf, verts, n, flags)` | idem |
| 18 | `Vertex(void*)` | verts que llegan uno a uno |
| 19 | `Index(WORD)` | |
| 20 | `End(flags)` | cierra el Begin y dibuja |
| 21 | `GetRenderState(rs, *val)` | |
| 22 | `SetRenderState(rs, val)` | **crítico** (tabla §6) |
| 23 | `GetLightState(ls, *val)` | |
| 24 | `SetLightState(ls, val)` | **crítico** (`MATERIAL`, `AMBIENT`, `FOGMODE`… §6.3) |
| 25 | `SetTransform(state, *matrix)` | **crítico** (WORLD/VIEW/PROJECTION/TEXTUREn) |
| 26 | `GetTransform(state, *matrix)` | |
| 27 | `MultiplyTransform(state, *matrix)` | |
| 28 | `DrawPrimitive(pt, fvf, verts, count, flags)` | **crítico** |
| 29 | `DrawIndexedPrimitive(pt, fvf, verts, nv, idx, ni, flags)` | **crítico** |
| 30 | `SetClipStatus(*cs)` | stub |
| 31 | `GetClipStatus(*cs)` | `S_OK` |
| 32 | `DrawPrimitiveStrided(pt, fvf, strided, n, flags)` | posible |
| 33 | `DrawIndexedPrimitiveStrided(...)` | posible |
| 34 | `DrawPrimitiveVB(pt, vb, start, n, flags)` | posible |
| 35 | `DrawIndexedPrimitiveVB(pt, vb, idx, ni, flags)` | posible |
| 36 | `ComputeSphereVisibility(centers, radii, n, flags, *ret)` | devolver todo visible (`0xFFFFFFFF`) |
| 37 | `GetTexture(stage, **tex)` | |
| 38 | `SetTexture(stage, tex)` | **crítico**; recibe `IDirect3DTexture2*` |
| 39 | `GetTextureStageState(stage, tss, *val)` | |
| 40 | `SetTextureStageState(stage, tss, val)` | **crítico** (tabla §6.2) |
| 41 | `ValidateDevice(*passes)` | `S_OK` con `*passes = 1` |

`IDirect3DDevice` (v1) = índices 3–21 (incluye execute buffers, `CreateMatrix`, etc.);
`IDirect3DDevice2` = 3–32 (agrega `SetCurrentViewport`, `Begin/Vertex/End`, render states,
`DrawPrimitive`); `IDirect3DDevice3` agrega 33–41 (`Strided`, `VB`, `Get/SetTexture`,
texture-stage states, `ValidateDevice`). **El juego usa Device3.**

`DrawPrimitive` firma exacta:
```
HRESULT DrawPrimitive(D3DPRIMITIVETYPE pt, DWORD fvf, void *vertices, DWORD count, DWORD flags)
HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE pt, DWORD fvf, void *vertices, DWORD vcount,
                             WORD *indices, DWORD icount, DWORD flags)
```
`flags` = `D3DDP_*` (d3d.h:191): `WAIT 1`, `OUTOFORDER 2`, `DONOTCLIP 4`,
`DONOTUPDATEEXTENTS 8`, `DONOTLIGHT 0x10`.

`D3DPRIMITIVETYPE` (d3dtypes.h:784): `POINTLIST=1`, `LINELIST=2`, `LINESTRIP=3`,
`TRIANGLELIST=4`, `TRIANGLESTRIP=5`, `TRIANGLEFAN=6`. GXM no tiene fans ni strips nativos
(las strips/fans se expanden a listas de triángulos o se usan primitivas GXM si existen).

### 4.3 Vtable `IDirect3DViewport3` (21 métodos)

| # | Método | Nota |
|---|---|---|
| 0-2 | IUnknown | QI a `IID_IDirect3DViewport3` |
| 3 | `Initialize` | stub |
| 4 | `GetViewport(*vp)` | |
| 5 | `SetViewport(vp)` (v1, `D3DVIEWPORT`) | |
| 6 | `TransformVertices(...)` | puede usarse para T&L por software |
| 7 | `LightElements(...)` | poco |
| 8 | `SetBackground(handle)` | |
| 9 | `GetBackground(*h)` | |
| 10 | `SetBackgroundDepth(surf)` | |
| 11 | `GetBackgroundDepth(**surf)` | |
| 12 | `Clear(flags, *rects, n, flags)` | **crítico** (clear por frame) |
| 13 | `AddLight(light)` | |
| 14 | `DeleteLight(light)` | |
| 15 | `NextLight(...)` | |
| 16 | `GetViewport2(*D3DVIEWPORT2)` | |
| 17 | `SetViewport2(*D3DVIEWPORT2)` | **crítico** (dwX,dwY,dwWidth,dwHeight,dvMinZ,dvMaxZ) |
| 18 | `SetBackgroundDepth2(surf)` | |
| 19 | `GetBackgroundDepth2(**surf)` | |
| 20 | `Clear2(flags, *rects, n, flags, color, z, stencil)` | **crítico** (clear con color) |

`D3DVIEWPORT2`: `dwSize, dwX, dwY, dwWidth, dwHeight, dvClipX, dvClipY, dvClipWidth,
dvClipHeight, dvMinZ, dvMaxZ`. Se mapea al viewport+scissor de GL/GXM.

### 4.4 `IDirect3DTexture2` (la textura)

| # | Método | Nota |
|---|---|---|
| 0-2 | IUnknown | QI a `IID_IDirect3DTexture2` |
| 3 | `GetHandle(**h)` (v1) / `GetHandle(*h)` | devuelve `D3DTEXTUREHANDLE` (DWORD) |
| 4 | `PaletteChanged(...)` | `S_OK` |
| 5 | `Load(D3DTEXTUREHANDLE)` | **crítico**: asocia la textura a la superficie DDraw y la hace residente |

`IDirect3DTexture` (v1) además tenía `Initialize`, `Unload`. La textura se crea **desde una
superficie DirectDraw** con `DDSCAPS_TEXTURE` (`CreateSurface`), y se obtiene el interface con
`surface->QueryInterface(IID_IDirect3DTexture2, &tex)`. Es decir: el shim puede almacenar los
píxeles en la propia `IDirectDrawSurface4` y que `SetTexture`/`Load` suba a GXM. No hay
`LockRect` (eso es D3D8+); el contenido se llena por `IDirectDrawSurface4::Lock`.

### 4.5 `IDirect3DMaterial3` y `IDirect3DLight`

- `IDirect3DMaterial3` (6 métodos): `SetMaterial(D3DMATERIAL*)`, `GetMaterial`, `GetHandle`
  (`D3DMATERIALHANDLE` DWORD). El **handle** se pasa a `SetLightState(D3DLIGHTSTATE_MATERIAL,
  handle)`. `D3DMATERIAL7`: 4 `D3DCOLORVALUE` (diffuse, ambient, specular, emissive) + `power`.
- `IDirect3DLight` (6 métodos): `Initialize`, `SetLight(D3DLIGHT*)`, `GetLight`. `D3DLIGHT`:
  type, diffuse, specular, ambient, position, direction, range, falloff, attenuation, etc.
- Como el juego es racing top-down con sprites/geometría, es probable que use **material
  diffuse/emissive** y poca iluminación dinámica; verificar por traza.

### 4.6 `IDirect3DVertexBuffer` (si usa `DrawPrimitiveVB`)

`IDirect3D3::CreateVertexBuffer(D3DVERTEXBUFFERDESC*, IDirect3DVertexBuffer**, flags, outer)`.
`D3DVERTEXBUFFERDESC { dwSize, dwCaps, dwFVF, dwNumVertices }`. Métodos: `Lock/Unlock`,
`ProcessVertices`, `GetVertexBufferDesc`, `Optimize`. La flag de creación incluye
`D3DVBCAPS_SYSTEMMEMORY`/`WRITEONLY`. Verificar si el juego lo usa (si no, stub).

### 4.7 Caps (`D3DDEVICEDESC7`, `GetCaps`)

`D3DDEVICEDESC7` (d3dcaps.h): `dwDevCaps`, `dpcLineCaps/dpcTriCaps` (`D3DPRIMCAPS`),
`dwDeviceRenderBitDepth`, `dwDeviceZBufferBitDepth`, `dwMin/MaxTextureWidth/Height`,
`dwMaxTextureRepeat`, `dwMaxAnisotropy`, guard bands, `dwStencilCaps`, `dwFVFCaps`,
`dwTextureOpCaps`, `wMaxTextureBlendStages`, `wMaxSimultaneousTextures`,
`dwMaxActiveLights`, `dvMaxVertexW`, `deviceGUID`, `wMaxUserClipPlanes`,
`wMaxVertexBlendMatrices`, `dwVertexProcessingCaps`.

Regla práctica: **mentir poco**. Declarar un device single-texture-stage, sin multitexture
avanzada ni bump, con blend `MODULATE`/`SELECTARG`/`ADD` según lo que soporte el shader del
shim, y FVF con `XYZ/XYZRHW/NORMAL/DIFFUSE/SPECULAR/TEX1`. Si se declaran capacidades que no
se implementan, el juego toma caminos que rompen.

### 4.8 `SetTransform` y matrices

`D3DTRANSFORMSTATETYPE` (d3dtypes.h:797): `WORLD=1`, `VIEW=2`, `PROJECTION=3`, `WORLD1=4`…
`WORLD3=6`, `TEXTURE0=16`…`TEXTURE7=23`. `D3DMATRIX` es 4x4 `float` row-major
(`_11.._44`). El shim necesita: multiplicar matrices para T&L por software, o pasarlas al
shader. Para un juego D3D6 de 2000, lo más simple es **T&L por software** (transformar en CPU
los vértices con `D3DFVF_XYZ` + normals/color) y mandar al GPU vértices ya transformados
(`XYZRHW`, `D3DFVF_TLVERTEX`) — ver §9.

---

## 5. Vertex formats (FVF) y estructuras

`D3DFVF_*` (d3dtypes.h:1246):

| Flag | Valor | Descripción |
|---|---|---|
| `D3DFVF_XYZ` | `0x002` | posición transformada por jets (T&L) |
| `D3DFVF_XYZRHW` | `0x004` | posición ya en screen space + w |
| `D3DFVF_XYZB1..5` | `0x006..0x00e` | blending de hasta 5 matrices |
| `D3DFVF_NORMAL` | `0x010` | normal |
| `D3DFVF_DIFFUSE` | `0x040` | color difuso (D3DCOLOR) |
| `D3DFVF_SPECULAR` | `0x080` | color especular |
| `D3DFVF_TEX0..8` | `0x000..0x800` | cantidad de coords (`TEXCOUNT_MASK 0xf00`) |
| `D3DFVF_TEXCOORDSIZEn(i)` | | tamaño 1–4 por coord (shift en bits 16+) |

Estructuras (todas packed, little-endian):

```
D3DVERTEX (D3DFVF_XYZ|NORMAL|TEX1 = 0x112):  32 bytes
  float x,y,z, nx,ny,nz, tu,tv
D3DTLVERTEX (D3DFVF_TLVERTEX = XYZRHW|DIFFUSE|SPECULAR|TEX1 = 0x144): 32 bytes
  float sx,sy,sz,rhw;  DWORD color; DWORD specular;  float tu,tv
D3DLVERTEX (D3DFVF_LVERTEX): x,y,z, dword color, dword specular, tu,tv
```
`D3DVERTEX` requiere T&L (por HW o por software en el shim); `D3DTLVERTEX` ya trae screen-space
+ rhw (el juego la usa para sprites/HUD 2D y quizá para geometría pre-transformada).
`D3DCOLOR` = `0xAARRGGBB`; `D3DCOLOR_ARGB(a,r,g,b) = (a<<24)|(r<<16)|(g<<8)|b`.

---

## 6. Estados

### 6.1 `D3DRENDERSTATETYPE` — subconjunto que hay que implementar (índice → significado)

`D3DRENDERSTATE_*` (d3dtypes.h, enum sin `#define`). Los importantes para 2000:

| # | Estado | Uso / mapeo |
|---|---|---|
| 1 | `TEXTUREHANDLE` | textura v1 (Device2); en Device3 usar `SetTexture` |
| 2 | `ANTIALIAS` | ignorar |
| 3 | `TEXTUREADDRESS` | wrap/clamp global |
| 5/6 | `WRAPU`/`WRAPV` (≤D3D6) | wrap U/V |
| 7 | `ZENABLE` | profundidad on/off → GL depth test |
| 8 | `FILLMODE` | `POINT/LINE/SOLID` → `glPolygonMode` |
| 9 | `SHADEMODE` | `FLAT`/`GOURAUD` |
| 14 | `ZWRITEENABLE` | depth write |
| 15 | `ALPHATESTENABLE` | alpha test (color key de textures) |
| 17 | `TEXTUREMAG` | filtro mag (`D3DTFG_POINT` 1, `LINEAR` 2…) |
| 18 | `TEXTUREMIN` | filtro min |
| 19 | `SRCBLEND` | `D3DBLEND_ONE/ZERO/SRCALPHA/INVSRCALPHA/...` |
| 20 | `DESTBLEND` | |
| 21 | `TEXTUREMAPBLEND` | `D3DTBLEND_MODULATE/...` (legacy) |
| 22 | `CULLMODE` | `D3DCULL_NONE 1/CW 2/CCW 3` |
| 23 | `ZFUNC` | `D3DCMP_LESS 1/LEQUAL 2/...` |
| 24 | `ALPHAREF` | umbral alpha test |
| 25 | `ALPHAFUNC` | función alpha test |
| 26 | `DITHERENABLE` | ignorar (o dithering en shader) |
| 27 | `ALPHABLENDENABLE` | blending |
| 28 | `FOGENABLE` | niebla |
| 29 | `SPECULARENABLE` | |
| 34 | `FOGCOLOR` | |
| 35 | `FOGTABLEMODE` | `D3DFOG_NONE/LINEAR/EXP/EXP2` |
| 36/37/38 | `FOGSTART/FOGEND/FOGDENSITY` | |
| 44/45 | `TEXTUREADDRESSU/V` | wrap/clamp por eje |
| 46 | `MIPMAPLODBIAS` | ignorar |
| 47 | `ZBIAS` | polygon offset (decals/pistas) |
| 48 | `RANGEFOGENABLE` | |
| 60 | `TEXTUREFACTOR` | color de modulación (`D3DTA_TFACTOR`) |

(El enum completo llega a 152, incluyendo stencil 52–59, stipple patterns, wrap 128–135,
y D3D7-only 136+.)

### 6.2 `D3DTEXTURESTAGESTATETYPE` (d3dtypes.h:1016)

| # | Estado | Uso |
|---|---|---|
| 1 | `COLOROP` | `D3DTOP_*` (`DISABLE 1`, `SELECTARG1 2`, `SELECTARG2 3`, `MODULATE 4`, `ADD 7`, …) |
| 2 | `COLORARG1` | fuente (`D3DTA_TEXTURE 0x2`, `DIFFUSE 0x1`, `CURRENT 0x0`, `TFACTOR 0x4`, …) |
| 3 | `COLORARG2` | |
| 4 | `ALPHAOP` | |
| 5 | `ALPHAARG1` | |
| 6 | `ALPHAARG2` | |
| 11 | `TEXCOORDINDEX` | |
| 12 | `ADDRESS` | |
| 13/14 | `ADDRESSU/V` | |
| 15 | `BORDERCOLOR` | |
| 16 | `MAGFILTER` | |
| 17 | `MINFILTER` | |
| 18 | `MIPFILTER` | |
| 21 | `MAXANISOTROPY` | |
| 24 | `TEXTURETRANSFORMFLAGS` | |

Para un juego 2000 alcanza con 1 stage (stage 0) y operaciones `SELECTARG1`/`MODULATE`
(con `D3DTA_TEXTURE` y `D3DTA_DIFFUSE`) — que es exactamente "textura × color de vértice".

### 6.3 `D3DLIGHTSTATETYPE` (d3dtypes.h:818)

`MATERIAL=1`, `AMBIENT=2`, `COLORMODEL=3`, `FOGMODE=4`, `FOGSTART=5`, `FOGEND=6`,
`FOGDENSITY=7`, `COLORVERTEX=8`. `SetLightState(MATERIAL, handle)` es el que activa un
`IDirect3DMaterial3`.

---

## 7. Cómo lo implementan Wine y dgVoodoo (referencia de comportamiento)

### 7.1 Wine `dlls/ddraw` + `wined3d` `[wine]`

- `dlls/ddraw` implementa **toda la capa DirectDraw y Direct3D 1–7** en C; es la referencia de
  código abierto más cercana. Archivos: `ddraw.c`, `surface.c`, `device.c`, `texture.c`,
  `viewport.c`, `material.c`, `light.c`, `palette.c`, `clipper.c`, `executebuffer.c`,
  `main.c`, `utils.c`. `dlls/ddrawex` provee `IDirectDraw3` (IE).
  - https://github.com/wine-mirror/wine/tree/master/dlls/ddraw
  - https://gitlab.winehq.org/wine/wine/-/wikis/DirectDraw
- Wine **no** reimplementa el rasterizador: `ddraw` **relay a `wined3d`** (la capa que en Wine
  se apoya en OpenGL/Vulkan y que modela un device tipo D3D9). `surface.c` crea
  `wined3d_texture`/`wined3d_swapchain` a partir de `DDSURFACEDESC2`; `device.c` mapea
  `IDirect3DDevice3` (draw inmediato, render states, texture-stage states) a las llamadas de
  `wined3d`; `viewport.c` a viewports/scissors; `texture.c` a uploads.
  - WineD3D traduce D3D8/9/10/11 → OpenGL/Vulkan
    (https://deepwiki.com/wine-mirror/wine/3.1-wined3d-direct3d-implementation).
  - Implicación: **no se puede portar wined3d a Vita tal cual** (depende de OpenGL de
    escritorio/Vulkan). Sirve como **especificación de semántica** y como test de referencia.

### 7.2 dgVoodoo2 `[dgvoodoo]`

- Es **propietario, cerrado** (Dege). El repo GitHub es de releases/issues, no de fuente.
  Implementa **DirectX 1–7** (todas las versiones de DirectDraw y D3D ≤7), D3D8.1 y D3D9
  **sobre Direct3D 11/12**, con la **fixed-function pipeline emulada por shaders** ("all the
  fixed function vertex & pixel processing pipeline is implemented by shaders").
  - https://dege.freeweb.hu/dgVoodoo2/
  - https://github.com/dege-diosg/dgVoodoo2
  - Readme DirectX: https://www.dege.fw.hu/dgVoodoo2/ReadmeDirectX/
- Para Hot Wheels el repack lo trae (`dgVoodooCpl.exe` 2.8.1.3, `dgVoodoo.conf`, `D3D8.dll`,
  `D3D9.dll`, `DDraw.dll`) `[estático]`. Tiene **tracing de API** (DebugView) que ya permitió a
  PCGamingWiki identificar `Direct3DDevice3`/`DirectDrawSurface4` ⇒ **es una fuente de
  ground-truth** para el set exacto de llamadas del juego (correr el juego con tracing y leer
  el log).
- Lección de diseño para el shim: dgVoodoo convierte la API legacy a una API moderna con
  **shaders**, no con fixed-function del driver. En Vita, la ruta equivalente es
  **vitaGL** (GL subconjunto → GXM) o shaders GXM directos.

---

## 8. IIDs (GUIDs) que el shim debe reconocer en `QueryInterface`

`[estático]` de `ddraw.h`/`d3d.h`:

```
IID_IDirectDraw4         9c59509a-39bd-11d1-8c4a-00c04fd930c5
IID_IDirectDrawSurface4  0b2b8630-ad35-11d0-8ea6-00609797ea5b
IID_IDirect3D3           bb223240-e72b-11d0-a9b4-00aa00c0993e
IID_IDirect3DDevice3     b0ab3b60-33d7-11d1-a981-00c04fd7b174
IID_IDirect3DViewport3   b0ab3b61-33d7-11d1-a981-00c04fd7b174
IID_IDirect3DTexture2    93281502-8cf8-11d0-89ab-00a0c9054129
IID_IDirect3DMaterial3   ca9c46f4-d3c5-11d1-b75a-00600852b312
```
Devices para `IDirect3D3::CreateDevice`:
```
IID_IDirect3DRGBDevice   a4665c60-2673-11cf-a31a-00aa00b93356
IID_IDirect3DHALDevice   84e63de0-46aa-11cf-816f-0000c020156e
IID_IDirect3DMMXDevice   881949a1-d6f3-11d0-89ab-00a0c9054129
IID_IDirect3DRefDevice   50936643-13e9-11d1-89aa-00a0c9054129
```
`IID_IDirectDrawSurface4` se pide también desde la primaria; para obtener D3D3 el QI es sobre
cualquiera de los objetos DirectDraw (típicamente la primaria o el `IDirectDraw4`).

---

## 9. Estrategia de shim en PS Vita (borrador para HW-006)

Backend de render recomendado: **vitaGL** (GL subconjunto → `sceGxm` con aceleración), que es
lo que usa D2Vita para su anillo Glide. Alternativa: shaders GXM directos.

1. **COM sobre el `Bridge`**: exponer objetos con vtable armada a mano (array de punteros a
   funciones nativas del port). `DirectDrawCreate` devuelve el objeto; `QueryInterface` compara
   GUIDs (§8). Los punteros de objeto deben vivir en memoria guest (o al menos ser direcciones
   válidas del espacio del CPU emulado) porque el guest los trata como `void*`.
2. **Superficies = render targets / texturas GXM** con un buffer CPU direccionable para
   `Lock`. Un `IDirectDrawSurface4` guarda: `width,height,pitch,format,mem,is_primary,is_back,
   is_texture,GXM texture`, etc.
3. **Present**: `Flip`/`Blt` de la primaria → `vitaGL` swap. Bombeo de input del guest acá.
4. **T&L**: dado que Device3 es fixed-function, la ruta más simple y fiel es **transformar por
   software** (aplicar WORLD×VIEW×PROJECTION, luces, fog, color) y emitir
   `GL_TRIANGLES` con vértices ya en clip/screen space. Así se evita replicar la FFP completa en
   shaders GXM, que es la parte más caras. Para `D3DFVF_TLVERTEX`/`XYZRHW` no hay T&L: dibujar
   directo.
5. **Texturas**: upload a GXM; convertir ARGB1555→RGBA4444/8888; **color key** con alpha test
   (`D3DRS_ALPHATESTENABLE`/`ALPHAREF`/`ALPHAFUNC`) o horneando alpha=0.
6. **`GetDC`/`SetPalette`/`Blt` con ROP**: son el path clásico de UI/2D de un juego de 2000; hay
   que implementar al menos `SRCCOPY`, `SRCAND`/`SRCPAINT` (máscaras), `COLORFILL` y stretch
   nearest. `GetDC` se puede emular devolviendo un DIB falso que al `ReleaseDC` se suba a la
   textura (igual que hace D2Vita con su camino `CreateDIBSection`/`BitBlt`).
7. **Audio/Input**: `DirectSoundCreate` (buffers) + `mciSendCommandA` para la música (redirigir
   a los `.wav` de `MUSIC/`), y `DirectInputCreateA` para joystick/teclado (mapear a controles
   Vita). Fuera del alcance gráfico de este doc, pero son imports del exe real.
8. **CD check**: como el shim no tiene CD, hay que **stubear la comprobación**. El repack ya
   resuelve esto montando `HWMR.iso`; en Vita no habrá ISO, así que el shim debe hacer que la
   verificación dé OK (probablemente `FindFirstFile`/`GetDriveType` sobre la unidad del CD, o
   `mciSendCommand`). Confirmar con traza Wine qué llama exactamente.

Nota de arquitectura: D2Vita ya resolvió el mismo problema para **Glide3x → GXM**
(`d2vita/D2Vita/src/glide_ring/`: `glide3x_ring.c`, `gx_host.cpp`, `replay60.cpp`). Ese
"ring + replay a GXM" es la mejor plantilla: el juego llama la API legacy, se graba en un ring
y se traduce/reprodució a GXM por frame. Reutilizar ese patrón para DDraw/D3D evita bloquear el
hilo del guest en cada llamada gráfica.

---

## 10. Copy protection y formatos de assets UDS

### 10.1 SafeDisc / SecuROM en Hot Wheels: Micro Racers

- `[web]` **PCGamingWiki: DRM = "Disc check (requires the CD/DVD in the drive to play) —
  Simple disc check."** No figura SafeDisc ni SecuROM. Sección *Availability*. (La página
  directa da 403 por Cloudflare; se leyó vía Wayback:
  https://web.archive.org/web/20251112212909/https://www.pcgamingwiki.com/wiki/Hot_Wheels:_Micro_Racers)
- `[estático]` **No hay rastro de SafeDisc ni SecuROM** en la instalación: no existe
  `00000001.TMP`, ni `CLCD16.DLL`/`CLCD32.DLL`, ni `SECDRV.SYS`, ni `.b5t/.b6t/.icd`; tampoco
  aparecen los strings en `HotWheels.exe` ni en `ISO/HWMR.iso`.
- `[estático]` El `readme.txt` original (2000-01-27) dice textualmente: *"place the CD-ROM ...
  to begin play"* y lista *"Quad speed (4x) CD-ROM drive"*. La protección es entonces un
  **chequeo de presencia de CD** (simple disc check), coherente con que el repack se distribuya
  como *"Bladez1992 LGU Repack And NoCD Fix"* y que `HWMR.ps1` haga
  `Mount-DiskImage "[APPDIR]ISO\HWMR.iso"` antes de lanzar el juego `[estático]`
  (https://oldgamesdownload.com/hot-wheels-micro-racers/, nombre de archivo del repack).
- Conclusión para el port: no hay que lidiar con un esquema de protección por sectores
  (SafeDisc) ni con drivers (SecuROM); solo **stubear el chequeo de CD**. La música Red Book
  (`mciSendCommandA`) se puede cubrir sirviendo los `.wav` de `MUSIC/` como hace `_inmm`.

### 10.2 Formato de assets UDS (observación local `[estático]`)

No hay documentación pública de estos formatos; lo siguiente sale de medir los archivos
instalados (`hotwheels/wineprefix/.../DATA`, gitignoreado). **Es material para HW-001/RE**, no
una spec cerrada.

| Ext. | Ejemplo | Lectura observada |
|---|---|---|
| `.FRM` | `ENGREG2.FRM` | **Texto INI**: `[Misc]\r\nWidth=420\r\nHeight=250\r\nTitle=Mattel Media...`. Son pantallas/diálogos del engine (campos `token`, `NextScreen`). Legible sin RE. |
| `.FSS` | `SLIDPOR.FSS` | **Binario con magic `SSBOB`** (53 53 42 4f 42) + versión. Slideshows/animaciones (los `SLID*.FSS`). Entradas con offsets. "Bob" = sprite/objeto animado, `SS` = slideshow. RE pendiente. |
| `.SFP` | `VEHICLES.SFP`, `WAVE.SFP`, `OFFICE.SFP` | **Banco binario de sprites/frames.** El header arranca con un DWORD igual al **tamaño del archivo** y luego pares/estructuras (p. ej. `VEHICLES.SFP` = 389.400 B: `0x0005F118`, `1`, `0x0004136C`, `2`, `0x0001040C`, `3`, …) que parecen tabla de offsets/IDs. Píxeles indexados a continuación. RE pendiente (probable 8-bit con paleta o 16-bit). |
| `.SIN` | `ASIN.SIN`, `SINUS.SIN` | **Tablas matemáticas, no gráficos.** `ASIN.SIN` es texto ASCII de valores decimales con signo (tabla de arc-seno); `SINUS.SIN` es una secuencia `int16` LE (`0,1,3,4,6,7,9,…` = tabla de seno). |
| `.TAB` | `SHADE.TAB` | 360.448 B de datos crudos con rampas de bytes crecientes = **tabla de sombreado/gradientes** precomputada. RE pendiente. |
| `.TGA` | `INTRO.TGA`, `HUD_HIGH.TGA`, `MAINMENU.TGA` | **TGA estándar** colormap, origen top-left (descriptor `0x20`); string de herramienta `WTGA version 1`. Convertible con cualquier librería. |
| `.DAT` | `Ai.dat` | `Ai.dat` empieza con `0x20` (32) y luego ternas `int32` (posiciones/rutas de IA por pista). `ACC.DAT`, `highscore.dat`, `settings.dat` = datos de juego/partida. |
| `.BMP`/`.GIF`/`.WAV` | `HWICON.BMP`, `GREEN.GIF`, `Track*.wav` | Formatos estándar. `.WAV` de música = Red Book rip. |
| `.SEQ` | `REGISTER.SEQ`, `REGRMDR.SEQ` | Secuencias de la pantalla de registro. |

Mapa de carpetas: `DATA/{INTRO,MENU,OFFICE,SLUMBER,LOADING,WAVE,SOUND}`, `MUSIC/`,
`EREG/` (registration/marketing), `DIAG/`, `DEPENDS/`.

---

## 11. Fuentes

Headers y herramienta (locales, `[estático]`):
- mingw-w64 `ddraw.h`, `d3d.h`, `d3dtypes.h`, `d3dcaps.h`
  (`~/opt/llvm-mingw-20260922-.../generic-w64-mingw32/include/`).
- Binario y assets: `hotwheels/wineprefix/drive_c/Games/Hot Wheels - Micro Racers/`
  (extraído por el agente `wine`; gitignoreado).

Juego / DRM / API:
- PCGamingWiki (vía Wayback, DRM "simple disc check", D3D6, 800x600):
  https://web.archive.org/web/20251112212909/https://www.pcgamingwiki.com/wiki/Hot_Wheels:_Micro_Racers
- Repack + NoCD fix (nombre de archivo):
  https://oldgamesdownload.com/hot-wheels-micro-racers/
- API D3D6 confirmada (d3dim.dll, dgVoodoo traces):
  https://www.pcgamingwiki.com/wiki/Hot_Wheels:_Micro_Racers
- TCRF (assets sin comprimir): https://tcrf.net/Hot_Wheels_Micro_Racers

DirectDraw/D3D en Wine y wrappers:
- Wine dlls/ddraw: https://github.com/wine-mirror/wine/tree/master/dlls/ddraw
- Wine wiki DirectDraw (numeración de interfaces):
  https://gitlab.winehq.org/wine/wine/-/wikis/DirectDraw
- WineD3D (arquitectura): https://deepwiki.com/wine-mirror/wine/3.1-wined3d-direct3d-implementation
- dgVoodoo2 (features, FFP por shaders): https://dege.freeweb.hu/dgVoodoo2/
- dgVoodoo2 readme DirectX: https://www.dege.fw.hu/dgVoodoo2/ReadmeDirectX/
- dgVoodoo2 repo: https://github.com/dege-diosg/dgVoodoo2

Vita:
- vitaGL (OpenGL→sceGxm): https://github.com/Rinnegatamante/vitaGL
- D2Vita (referencia de runtime + ring Glide→GXM): https://github.com/Franckrst/D2Vita
- (local) `d2vita/D2Vita/src/glide_ring/`
