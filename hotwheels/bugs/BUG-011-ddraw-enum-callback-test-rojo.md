# BUG-011 — ctest `ddraw_bridge`/`ddraw_bridge_sized` rojos con `src/ddraw/bridge.cpp` modificado sin commitear

Nivel: **medido en host** (qa, 2026-10-03 ~01:33, HEAD 5ff753f + working tree).
Dueño fix: **render** (o quien tenga `src/ddraw/bridge.cpp` en curso). Severidad: mayor (smoke FAIL por etapa tests=1).
Estado: **cerrado (2026-10-03)**.

## Evidencia
- `out/run_all-20261003-013342/ctest.log`: `tests/test_ddraw_bridge.cpp:90: c.callback_calls==3 && c.callback_entry==0x00402001` en ambos tests (abort).
- smoke `out/smoke-20261003-013214.md`: tests=1, build_vita=0, qemu=0 (219 frames, MISSING 0), vita3k=0. Sólo falla la etapa de tests.
- El diff sin commitear de `src/ddraw/bridge.cpp` cambia el corte del callback de enumeración (`if(!invoke_callback(...)) return kOk;`); el test en HEAD (EnumDisplayModes dd4, 3 llamadas) ya no se cumple.
- En HEAD limpio (183d037) todo estaba verde (PASS=17).

## Acción
Ajustar test o bridge para que concuerden y commitear ambos juntos; re-correr `tests/run_all.sh`.

## Resolución (Codex bugs, 2026-10-03)
El cambio de render anuncia 3 resoluciones × 2 formatos. Actualicé sólo la regresión del test para
comprobar los seis descriptores (320/640/800 × 16/8 bpp) y el callback adicional de enumeración.
No toqué el bridge WIP de render. `cmake --build hotwheels/build --target test_ddraw_bridge -j4`
PASS; `ctest --test-dir hotwheels/build -R ddraw_bridge --output-on-failure`: 2/2 PASS. BUG-011
cerrado; el cambio productivo sigue sujeto al review del owner de render.
