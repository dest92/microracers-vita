# BUG-005 — Vita3K cae al presentar el fixture P8

- Severidad: bloqueante · Estado: cerrado para validación emulada · Feature: HW-021 · Dueño: sol
- Evidencia: emulador, sin consola. `tools/vita3k_run.sh samples/ddraw-present/build/ddraw-present.vpk 20`.
- Antes: primer draw FFP invoca `sceShaccCgCompileProgram` sin implementar. Cambio: GXP precompilados de blit incluidos en vitaGL, sin compilación FFP.
- Después: `build-vita3k/ddraw-present-20261003-001546/run.stdout` registra init ok y frame 0 status=ok; inmediatamente SIGSEGV del proceso emulador, acceso `0x400100000`. `shot_020.png` sigue negra.
- Segundo diagnóstico: recrear descriptor con `sceGxmTextureInitLinear` + `SetPalette` en `ddraw-present-20261003-001905` produce los mismos cuatro words y SIGSEGV. Se retiró ese cambio redundante.
- Descriptor observado: `83e00090,7527f1df,60ec6010,818308e6`; índices `0x60ec6010`, paleta `0x60c23980` (log de GetPalette). Son datos observados, no demuestra que P8 sea la causa del crash.
- Próximo diagnóstico: fixture idéntico RGBA para aislar textura P8 de shaders/draw; backtrace del emulador. No afirmar menú visible ni marcar HW-021 done con logs frame0 solamente.

## Aislamiento adicional (emulador)
- `present-rgba-20261003-002058`: índices expandidos a RGBA en fixture ignorado; mismo SIGSEGV tras frame0.
- `present-rgba-20261003-002158`: backend Vulkan por CLI privada; mismo SIGSEGV. No se cambió configuración compartida.
- `present-clear-20261003-002145`: se elimina sólo glDrawArrays; clear+swap llega a frame480 (log cada120), sin el crash de draw durante 15s de harness. Esto acota el fallo al draw, no sólo a P8.
- `ddraw-present-20261003-002407`: shaders creados como GL_CG_* en vez de GL_*; mismo fallo. Se conserva Cg porque los binarios provienen de fuente Cg.

## Causa y fix demostrado
GDB `build/debug-draw.log`: SIGSEGV en `export_sceClibMemcpy`, destino host `0x400100000` (guest `0x00100000`), longitud32. El pool circular default de vitaGL pide32MiB; no cabe en el poolRAM8MiB y devuelveNULL. La reserva legacy1MiB por frame desplaza el punteroNULL hasta0x100000.
Fix: `vglSetCircularPoolSize(2MiB)` ANTES del init y legacyPool0 (no usamos glBegin). `ddraw-present-20261003-002655`: termina600 frames, sinSIGSEGV; `shot_005.png` muestra las ocho franjas P8 y barras negras117/118px, dentro del viewport960x544. `shot_015.png` negra es posterior al ExitProcess del fixture (no es fallo de draw). Fixture RGBA fixed confirma mismoquad visible. No se ejecutó consola.

## Cierre de BUG-005 (2026-10-03, bugs)
La causa demostrada fue el pool circular de vitaGL de 32 MiB que excedía el pool RAM de 8 MiB; el
fix configura 2 MiB antes del init y legacy pool 0. `coordination/review-HW-021.md` APRUEBA y
verifica capturas fixture P8/RGBA visibles, más menú del juego en Vita3K. BUG-005 cerrado para la
validación emulada; no implica prueba en consola ni cierre de HW-021 por su owner.
