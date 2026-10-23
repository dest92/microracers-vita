# BUG-007 — DDraw: `ddsCaps.dwCaps` se escribe en el offset equivocado (116 vs 104)
- Severidad: mayor · Estado: cerrado · Feature: HW-013 · Dueño: bugs
## Síntoma (exacto, logs)
Detectado al correr TODOS los tests host (`tests/run_all.sh`): `ctest -R ddraw_bridge` fallaba en
`test_ddraw_bridge.cpp:82` (CreateSurface de la sonda 16 bpp). El test escribía `dwCaps` en
`desc+116`; `dd_call` case 6 lo lee en `desc+104` (`src/ddraw/bridge.cpp:142`) → `caps=0` → `kNo3D`.
## Causa raíz (verificada)
En `DDSURFACEDESC2` estándar (y en la observación de `docs/re-engine.md` §3: `ddsCaps.dwCaps +0x68`,
`dwCaps2 +0x6c`, `dwSize 0x7c`), `ddsCaps.dwCaps` va en **offset 104**, no 116.
`Adapter::surface_descriptor` (`src/ddraw/bridge.cpp:84`) escribe `s.caps` en
`len>=124 ? a+116 : a+104`, **inconsistente con su propio `CreateSurface` (104)** y con la ref.
El test quedó alineado a 116, por eso el fallo sólo apareció al agregar la verificación de `dwCaps`.
## Esperado vs observado
- Esperado: `GetSurfaceDesc`/`GetAttachedSurface` informan `ddsCaps.dwCaps` en el offset 104 (11068).
- Observado: se escribe en 116 → el juego leería `dwCaps2`/basura como `dwCaps`.
## Fix + test de regresión
- Cambiar `surface_descriptor` a escribir `s.caps` en 104 para `len>=124`.
- Test: extender `test_ddraw_bridge.cpp` para leer `dwCaps` del descriptor devuelto por
  `GetSurfaceDesc`/`GetAttachedSurface` y compararlo con los caps de la superficie.
## Mitigación aplicada (deepseek, tarea run_all)
El **test** `test_ddraw_bridge.cpp` se corrigió a `desc+104` (3 escrituras), que es lo que ya lee
`CreateSurface`; `ctest` 6/6 verde. La inconsistencia del **escritor de descriptores** quedó corregida luego en `66db451` (integ), con caps siempre en104.

## Regresión sol (2026-10-03)
`test_ddraw_bridge.cpp` verifica caps104 de sonda RGB565, primaria y backbuffer con descriptor108/124/136. Offset116 permanece0 en DD2 y guarda0xDEADBEEF fuera del descriptor108. Variante histórica116 compilada en `build/bug007-red` falla con SIGABRT en assertion97 (no se modificó código productivo). Código actual: rebuild CTest8/8 verde, incluyendo legacy y sized. Cierre definitivo tras review del test.

### Verificación y cierre (2026-10-03, bugs)
El `ddraw.h` de `shared/opt/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64` define ambos layouts
con `ddsCaps` en offset 0x68 (104); en `DDSURFACEDESC2`, `dwCaps2/3/4` siguen en 108/112/116 y
`dwTextureStage` en 120. Se corrigió la tabla de offsets en `docs/d3d6-ddraw-surface.md`.
`ctest --test-dir hotwheels/build -R ddraw_bridge`: 2/2 PASS, incluye descriptores legacy y sized.
BUG-007 cerrado.
