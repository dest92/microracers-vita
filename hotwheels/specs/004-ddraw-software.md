# HW-013 — DirectDraw software de 8 bpp

## Problema y evidencia

`[wine]` La traza de intro y menú de 60 s (`docs/wine-reference.md` y `docs/wine-ref/ddraw-methods-counts.txt`) muestra una sonda D3D a 640×480×16 y luego un modo software 640×480×8. Por frame: `Lock`/`Unlock` de superficie offscreen, `BltFast` al backbuffer, `Flip` y cambios de paleta. El binario importa `DirectDrawCreate` y `DirectDrawEnumerateExA` (`[estático]`).

## Contrato observable

- Registrar `DDRAW.dll` después de los shims genéricos y antes de `Bridge::link`: `DirectDrawCreate` crea `IDirectDraw` v1; `QueryInterface` entrega `IDirectDraw4` e `IDirect3D3`, con vtables i386/`stdcall`, referencias válidas y HRESULT definidos. `DirectDrawEnumerateExA` anuncia un dispositivo primario.
- `SetCooperativeLevel` acepta las flags del juego. `EnumDisplayModes` anuncia como mínimo 640×480×16 y 640×480×8. `SetDisplayMode` acepta esos modos: 16 bpp RGB565 con pitch/tamaño coherentes sólo para la sonda; 8 bpp para el render. `IDirect3D3::CreateDevice` devuelve error y anula su salida, para activar el camino software. `RestoreDisplayMode` y `WaitForVerticalBlank` devuelven éxito.
- `CreateSurface` acepta primaria `COMPLEX|FLIP` con un backbuffer y offscreen `SYSTEMMEMORY` 8 bpp. `GetAttachedSurface(BACKBUFFER)` obtiene el backbuffer. `Lock` entrega `DDSURFACEDESC2` con `lpSurface` en VA guest y pitch en bytes; escritura del guest visible tras `Unlock`. No reservar 640×480 píxeles en el scratch de trampolines.
- Un `SetDisplayMode` válido invalida la primaria anterior de forma observable: deja de anunciar `DDSCAPS_PRIMARY`, no es render target y su `Flip` falla. La primaria nueva para la resolución elegida puede crearse aunque el guest aún conserve referencias COM a superficies del modo anterior; las referencias previas siguen siendo liberables.
- `BltFast` y `Blt` copian rectángulos 8 bpp con clipping. `DDBLTFAST_SRCCOLORKEY`/`DDBLT_KEYSRC` respetan `SetColorKey` del origen. `Flip` intercambia frontal/backbuffer y presenta el frontal nuevo. `CreatePalette` + `SetEntries` leen `PALETTEENTRY` y `SetPalette` asocia la paleta con la primaria. Cada presentación llama `present(const u8* idx, pitch, const u32 pal[256])`, con colores `0xFFRRGGBB`.
- El backend host genera PNG RGBA de un cuadro sintético; HW-009 implementará el backend Vita (GXM/vitaGL). Ni imágenes del juego ni otros binarios se commitean.

## Criterios de aceptación

1. Test host verifica posiciones y aridades de métodos COM críticos contra las vtables de `docs/d3d6-ddraw-surface.md`.
2. Test host escribe por el VA de `Lock`, hace `Unlock` y lee los mismos bytes; verifica pitch 640.
3. Test host verifica `BltFast`/`Blt` con rectángulo y color key.
4. Test host verifica intercambio de buffers por `Flip` y presentación de índices del nuevo frontal.
5. Test host verifica actualización de paleta y píxel RGBA del PNG resultante.
6. Test host verifica superficie de sonda RGB565 (pitch 1280, máscaras F800/07E0/001F), error de `CreateDevice` y salida nula. Build host y tests verdes.
7. Test bridge cambia de 640×480 a 800×600 con una primaria anterior aún referenciada: `GetCaps` ya no anuncia PRIMARY, `Flip`/render target viejos fallan, y `GetAttachedSurface(BACKBUFFER)` devuelve el nuevo backbuffer de 800×600.

## Límites

No se implementan D3D, superficies RGB16 para render, backend Vita ni sincronización vertical real. La sonda 16 bpp sólo debe progresar hasta `CreateDevice` fallido. No tocar winx86: integración por shim externo aditivo. El arranque del juego en Wine/Vita3K y la integración del backend Vita requieren features posteriores.
