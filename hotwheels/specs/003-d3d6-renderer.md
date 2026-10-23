# SPEC-003 — Renderer D3D6 para carreras (HW-010)

- Feature: HW-010 · Milestone: M3→M5 · Dueño: `re` · Estado: en curso
- Depende de: HW-013 (sol, superficies DirectDraw), HW-008 (tabla COM `re-engine.md` §3), HW-012 (traza Wine de carrera, `FINDINGS.md` #18, `wine-reference.md` §8).
- Decisión de arquitectura previa: `docs/render-options.md` §4/§6 (ring de comandos → vitaGL; fallback GXM).

## Problema
En carrera con la opción 3D=SÍ el juego crea `IDirect3DDevice3` (HAL) sobre el backbuffer 16 bpp y dibuja con **una** forma de llamada (FINDINGS #18, re-engine §3): `SetTexture` + `SetTextureStageState(0,COLOROP,SELECTARG1)` + `SetRenderState` (blend) + `DrawPrimitive(TRIANGLESTRIP, 0x104, v, 3|4, DONOTLIGHT)`, ≈887 draws/escena, sin Z ni matrices. Hoy el shim de sol devuelve `DDERR_NO3D` en `CreateDevice` a propósito, y el juego cae al software 8 bpp.

## Arquitectura
```
 COM (Device3/Viewport3/Texture2)  ──►  Recorder ──► ring (bytes POD) ──► Replayer ──► Backend
   hw::d3d::Device (semántica D3D6,        encode()                  decode + estado     ├ RefRaster (host, golden PNG)
   validación, cache de texturas)                                     + coords clip       └ VitaGL (Vita)
```
- **Device** (portable, sin Bridge): implementa la semántica del subconjunto exacto; todo efecto observable va al ring. Las texturas se leen de un `TextureSource` (superficie DDraw de sol: tamaño, pitch, formato, puntero, colorkey, **generación** que incrementa en cada `Unlock`).
- **Ring**: palabras u32 LE, `op | (payload_words << 16)` seguido del payload. Formato estable y documentado (abajo) para que en una fase posterior lo escriba un `DDRAW.dll` x86 guest-side sin cruces por draw (render-options §4). En esta fase lo escriben los shims host.
- **Replayer**: decodifica, mantiene el estado (blend, filtros, colorkey, textura, viewport), convierte vértices a clip y emite `DrawCall` completos al backend. Convierte texturas a ARGB8888 (helper) además de pasar el formato nativo.
- **Backends**: `RefRaster` (rasterizador de referencia CPU, ARGB8888, escribe PNG) para tests de golden image; `VitaGLBackend` (sólo Vita, FFP: `glOrtho` identidad, `glDrawArrays(GL_TRIANGLE_STRIP)`, `glBlendFunc`, `glAlphaFunc`, `glScissor`, texturas `GL_RGBA`).
- La parte COM (vtables, traps, objetos en memoria invitada) va en `src/d3d/bridge.cpp` e integra con los objetos de sol (`IDirect3D3::CreateDevice` delega en `hw::d3d`); se acuerda en `coordination/sol.md`.

## Contrato observable

### Subconjunto COM (offset de vtable → semántica)
| Interfaz | Método | Contrato |
|---|---|---|
| IDirect3D3 | CreateDevice (+0x20) | `IID_IDirect3DHALDevice` sobre una superficie con `DDSCAPS_3DDEVICE` → Device3; otra IID → `DDERR_INVALIDPARAMS` |
| | CreateViewport (+0x18) | Viewport3 nuevo |
| | EnumDevices (+0xc) | un dispositivo HAL (sólo para la sonda) |
| Device3 | AddViewport (+0x14) / DeleteViewport (+0x18) | lista de viewports |
| | EnumTextureFormats (+0x20) | en este orden: ARGB1555, RGB565, ARGB4444, PAL8; corta si el callback retorna 0 |
| | BeginScene (+0x24) / EndScene (+0x28) | ops `BEGIN`/`END`; anidar → `D3DERR_SCENE_IN_SCENE` (0x88760320) |
| | SetCurrentViewport (+0x30) | op `VIEWPORT` con el rect del viewport |
| | SetRenderState (+0x58) | estados soportados abajo; otros se aceptan, se registran una vez en el log y no afectan |
| | DrawPrimitive (+0x70) | `prim` ∈ {4 LIST, 5 STRIP, 6 FAN}; `fvf` debe ser `0x104`; si no → `DDERR_INVALIDPARAMS`; op `DRAW` con vértices crudos (x, y, z, rhw, u, v) |
| | SetTexture (+0x98) | stage 0; si la generación de la fuente cambió → op `TEX_UPLOAD` antes de `TEXTURE` |
| | SetTextureStageState (+0xa0) | stage 0 `COLOROP` = `SELECTARG1(2)` (único valor usado); `MODULATE(4)` también aceptado; otros tipos se ignoran |
| Viewport3 | SetViewport2 (+0x44) | guarda X, Y, W, H (`D3DVIEWPORT2` +4/+8/+12/+16) |
| | Clear (+0x30) | `count = 0` → rect completo del viewport; si no, cada rect intersecado; color = negro opaco; op `CLEAR` |
| Texture2 | QI / AddRef / Release | sólo esos (no se usan GetHandle/Load: re-engine §3.2) |

### Render states soportados
`CULLMODE(22)` (sólo NONE; otros se registran), `TEXTUREMAG(17)`/`TEXTUREMIN(18)` (1 NEAREST, 2 LINEAR), `SRCBLEND(19)`/`DESTBLEND(20)` (D3DBLEND 1–10), `ALPHABLENDENABLE(27)`, `COLORKEYENABLE(41)`.

### Coordenadas
Vértices XYZRHW en píxeles del render target (W×H). Centros de píxel D3D en coordenadas enteras ⇒ `ndc_x = 2(x+0.5)/W − 1`, `ndc_y = 1 − 2(y+0.5)/H`, `z = 0` (el juego no escribe z; no hay Z-buffer), `w = 1`. El viewport recorta (scissor), no transforma.

### Texturas
Formatos de entrada: ARGB1555 (a = bit15), RGB565 (a=255), ARGB4444, ARGB8888, PAL8 (paleta de la superficie). Conversión a ARGB8888 con replicación de bits altos (`5→8: v<<3|v>>2`). **Colorkey → alpha test**: si la fuente tiene `CKSRCBLT` y `COLORKEYENABLE=TRUE`, los texels cuyo valor nativo cae en `[low, high]` quedan con alfa 0 y el draw se hace con alpha test (descarta α=0). Sin colorkey no hay alpha test (fiel a D3D6: el juego no usa `ALPHATESTENABLE`).

### Blend
`out = src·Fs + dst·Fd` por canal RGB, saturado; factores D3DBLEND: 1 ZERO, 2 ONE, 3 SRCCOLOR, 4 INVSRCCOLOR, 5 SRCALPHA, 6 INVSRCALPHA, 7 DESTALPHA, 8 INVDESTALPHA, 9 DESTCOLOR, 10 INVDESTCOLOR (render target sin alfa ⇒ DESTALPHA=1). Con `ALPHABLENDENABLE=0` el src reemplaza.

### Formato del ring (u32 LE)
| op | nombre | payload |
|---|---|---|
| 1 | BEGIN | w, h del target |
| 2 | VIEWPORT | x, y, w, h |
| 3 | CLEAR | x, y, w, h, argb |
| 4 | STATE | id, valor |
| 5 | TSS | stage, tipo, valor |
| 6 | TEXTURE | stage, tex_id (0 = ninguna) |
| 7 | DRAW | prim, n, n×6 float (x, y, z, rhw, u, v) |
| 8 | END | — |
| 9 | PRESENT | — |
| 10 | TEX_UPLOAD | tex_id, w, h, formato, pitch, flags (bit0 colorkey), key_low, key_high, [256 ARGB de paleta si PAL8], filas empaquetadas a `pitch` |
| 11 | TEX_FREE | tex_id |
Formatos: 1 ARGB1555, 2 RGB565, 3 ARGB4444, 4 ARGB8888, 5 PAL8.

## Criterios de aceptación (tests host, `tests/test_d3d.cpp`)
- **R1 ring**: encode→decode reproduce cada op y payload; un op desconocido se saltea por su largo.
- **R2 coordenadas**: (0,0) en 800×600 → (−1+1/800, 1−1/600); (799,599) → (1−1/800, −1+1/600).
- **R3 raster textura**: quad STRIP de pantalla completa con textura 2×2 ARGB1555 NEAREST → los cuatro cuadrantes del framebuffer tienen los 4 colores exactos.
- **R4 blend**: sobre fondo conocido, los tres combos del juego (5/6 con α de 1555, 9/3, 10/2, 1/4) dan el valor calculado (±1).
- **R5 colorkey**: con `COLORKEYENABLE` y key = valor de un texel, ese texel no escribe; sin `COLORKEYENABLE`, escribe.
- **R6 viewport**: `Clear(count 0)` limpia sólo el rect del viewport; un triángulo que excede el viewport no pinta fuera.
- **R7 uploads**: misma generación → no re-sube; generación nueva → re-sube (contado en el backend).
- **R8 validación**: FVF ≠ 0x104 → `DDERR_INVALIDPARAMS` y nada en el ring; `BeginScene` anidado → `D3DERR_SCENE_IN_SCENE`; `EnumTextureFormats` entrega ARGB1555 primero.
- **R9 LINEAR**: muestreo bilineal en el centro entre dos texels da el promedio (±1).
- **R10 golden**: escena compuesta (clear + 3 draws con blends y colorkey) → hash FNV-1a del framebuffer igual al registrado en el test; se escribe `build/d3d_golden.png` para inspección.

## No-objetivos (esta fase)
- `DDRAW.dll` x86 guest-side que escriba el ring (fase siguiente, mismo formato).
- Backend GXM directo (fallback de render-options §6.3).
- Lectura del render target por CPU (`Lock` del backbuffer 3D): se detecta y registra; se resuelve si la traza lo muestra.
- Z-buffer, luces, matrices, vertex buffers, `DrawIndexedPrimitive` (el juego no los usa).
