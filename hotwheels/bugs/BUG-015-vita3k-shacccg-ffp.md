# BUG-015 — Vita3K no compila shaders FFP de vitaGL

- Estado: mitigado para el renderer D3D6 integrado; limitación abierta para el sample FFP standalone. HW-011 permanece en review sólo por su aceptación FFP standalone.
- Severidad: mayor para validación/emulación; el backend GXP de HW-010 no depende de esta ruta.
- Evidencia: emulador, `build-vita3k/vitagl-ffp-20261003-025129`.

Con `libshacccg.suprx` instalado en `ur0:/data/`, Vita3K carga el módulo y el sample registra
`vglInit ok (resolution_fallback=0)` y `entering loop`. Al compilar el primer shader FFP, Vita3K
registra `sceShaccCgInitializeCompileOptions` y `sceShaccCgCompileProgram` como imports no
implementados, lee address 0 y termina con SIGSEGV. `shot_002.png` queda negra. La re-review de
`95397e8` observó también `shot_008.png` negra; no hay salida FFP visible.

No es el fallo de pool P8 de BUG-005: ese camino evita compilación FFP con shaders GXP
precompilados. Este sample necesita justamente la compilación runtime que Vita3K no ofrece en la
versión instalada.

Seguimiento: confirmar si una build/version soportada de Vita3K implementa esos exports para
resolver el sample standalone. La limitación FFP ya no bloquea el renderer D3D6 GXP del juego.

## Mitigación para el juego — 2026-10-04

**Estado: mitigado para el renderer D3D6 integrado; el sample FFP standalone sigue bloqueado.** HW-010/HW-029 no llama a `sceShaccCgCompileProgram`: usa shaders GXP propios precompilados y enlazados por vitaGL. En el VPK actual, Vita3K corrió 40 s con `HW_INPUT_SCRIPT`; `CreateDevice` 640×480 y 800×600 devolvió `S_OK`, `D3D frame=300 backend=vitaGL`, y `D3D escena=100 draws=972`. La captura `build-vita3k/hotwheels-vita-20261003-042027/shot_039.png` muestra carrera activa.

Esto evita que la limitación ShaccCg bloquee el juego. No indica soporte de Vita3K para compilación FFP ni cambia el resultado negro del sample HW-011; queda como limitación de ese sample, fuera del camino GXP integrado.
