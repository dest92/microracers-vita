# HW-021 — presentación Vita de superficies DirectDraw 8 bpp

## Problema y evidencia

HW-013 entrega `present(idx, pitch, pal[256])`, con colores `0xFFRRGGBB`. El menú real usa 640×480×8 y cambia la paleta por frame (`FINDINGS.md` #13, `[wine]`). vitaGL local (`third_party/vitaGL`, commit fijado por HW-011) soporta `GL_COLOR_INDEX8_EXT` como `P8_ABGR`, `vglGetTexDataPointer` y `glColorTable` (`[estático]`). En esta revisión de vitaGL, la textura conserva el puntero GPU de la paleta pero limpia `texture::palette_data` tras crearla; por eso actualizar con `glColorTable` por frame dejaría la textura apuntando a memoria liberada. Se crea una sola tabla y se actualiza el puntero obtenido del descriptor GXM (`sceGxmTextureGetPalette`).

El layout HW-009 reserva arena winx86 de 212 MiB, newlib hasta 32 MiB, pila principal 4 MiB y JIT 32 MiB; el presupuesto documentado es ~330 MiB de RAM user con ATTRIBUTE2=12 (`specs/002-vita-runtime.md`). El backend debe reservar pools explícitos de vitaGL (objetivo inicial: RAM 8 MiB, CDRAM 16 MiB, sin MSAA), y registrar éxito o fallo de inicialización. Se limita también el pool circular a 2 MiB antes del init (default de vitaGL: 32 MiB), con reserva legacy nula porque el quad no usa glBegin. No usar `vglInit()` de tamaño automático en el juego.

## Contrato observable

- `VitaPresenter::init(w,h)` inicializa vitaGL 960×544 con pools fijos, crea una textura P8 del tamaño lógico y una paleta de 256 colores. `present` copia `w` índices por fila respetando el pitch guest y el stride de textura alineado a 8, convierte cada `0xAARRGGBB` a `0xAABBGGRR` para P8_ABGR, limpia negro, dibuja un quad aspect-fit con filtro nearest y hace swap con vsync.
- Para 640×480 el rect de destino es `{x=117,y=0,w=725,h=544}`; barras negras de 117 y 118 px. `fit_rect` admite otras resoluciones 8 bpp (p. ej. carrera 800×600) y protege divisiones/overflow.
- La textura y la paleta se crean una sola vez por tamaño; cambios de paleta no asignan una tabla nueva. `shutdown` libera la textura/backend. `present` nunca conserva el puntero `idx` después de volver.
- El quad usa los GXP de blit precompilados del vitaGL vendorizado mediante `vglShaderGxpBinary`, atributos position/texcoord y coordenadas clip. No requiere compilar shaders FFP durante el draw.
- `samples/ddraw-present/` genera barras de índices y una paleta animada sin assets externos, empaqueta un VPK de prueba y se corre con `tools/vita3k_run.sh` en entorno aislado. La captura se valida por presencia de las franjas y barras laterales, con log del backend.
- El runtime del juego registra el callback a través de `BootOptions::install_shims` y lo inicializa luego de mapear el runtime, con allocator VA guest compartido con HW-009/HW-006. No se commitean artefactos del juego ni capturas con copyright.

## Criterios de aceptación

1. Test host rojo→verde: `fit_rect(640,480,960,544) == {117,0,725,544}`, conversión ARGB→ABGR y copia de filas con pitch 672→stride 640, sin tocar guardas.
2. VPK sample compila con el vitaGL instalado; al correr en Vita3K muestra barras sintéticas animadas sin crash y produce al menos una captura que contiene barras de color y laterales negros. Loguea resultado de `vglInit`/textura/paleta.
3. Build Vita del juego enlaza el backend, registra HW-013 y usa pools fijos compatibles con el layout. `ctest` host y build Vita verdes. Revisión por otro agente antes de `done`.

## Límites

La prueba visual no demuestra todavía que el juego alcance el menú; eso depende de shims/arranque de HW-006/007. No se modifica winx86. El backend no implementa D3D6 ni 16 bpp de carrera.

## Revalidación HW-029 — juego con D3D6 GXP

En `build-vita3k/hotwheels-vita-20261003-042027`, el juego llegó a carrera y siguió presentando el framebuffer P8 mientras el overlay D3D6 usaba vitaGL/GXP: `D3D frame=300 backend=vitaGL`, captura `shot_039.png` a ~39 s. Esto confirma convivencia del present P8 de HW-021 con el backend D3D GXP; no convierte el sample standalone FFP de HW-011 en prueba verde.
