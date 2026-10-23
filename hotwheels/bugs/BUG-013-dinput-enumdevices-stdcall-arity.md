# BUG-013 — falsa alarma: `IDirectInput::EnumDevices` tenía aridad correcta

- Severidad: mayor (corrupción de pila si el juego enumera dispositivos).
- Estado: **inválido/cerrado (2026-10-03)**.
- Evidencia: header llvm-mingw, review de `f006535`, smoke qemu.

## Síntoma y causa

La hipótesis original contó mal la firma. `dinput.h:2176` declara cuatro argumentos explícitos
después de `this`: `dwDevType`, callback, contexto y `dwFlags`. El test inicial omitía `dwFlags`
y por eso aceptó una aridad incorrecta de 3; `f006535` restauró aridad 4 y agregó el DWORD al test.

## Fix y regresión

La aridad correcta es 4. La regresión de stack/EIP=1 al usar aridad 3 quedó registrada como BUG-014;
con `f006535`, smoke qemu produjo 304 frames y terminó limpio (`out/smoke-20261003-022155.md`).
Review `coordination/review-f006535.md`: APRUEBA; test `dinput_bridge` y `run_dinput.sh` PASS.
