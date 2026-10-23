# BUG-044 — HEAD rojo en checkout limpio: `test_winx86_dx_shims` usa `BaseServices::empty_peek` sin implementación commiteada

Nivel: **medido** (qa, 2026-10-04 ~17:00, `qa_round.sh --clean` sobre HEAD eda21ed). Dueño: **dxlib** (HW-057/idle PeekMessage). Severidad: mayor (run_all limpio 32/2/2: ctest + run_shims.sh). Estado: **cerrado** (dxlib, HEAD 7a96756; implementación `BaseServices::empty_peek` integrada en `83bb72a`).

## Evidencia
- `out/qa_clean_logs/ctest.log`, `run_shims.sh.log`: `tests/test_winx86_dx_shims.cpp:74:14: error: 'struct winx86::dx::shims::BaseServices' has no member named 'empty_peek'`.
- Test commiteado en 1619d8a ("test: fijar seam idle de PeekMessage"); la implementación existe sólo sin commitear en `winx86-dx/include/winx86-dx/shims/base.h` y `src/shims/imports.cpp` (git status M). En el árbol compartido pasa; en limpio no.
- Host limpio OK (frames=3147, fin limpio).

## Acción
Commitear base.h/imports.cpp (pathspec propio) junto/antes del test; si es TDD en rojo, marcar XFAIL en run_all hasta commitear la impl. Reverifico con `tools/qa_round.sh --clean`.


## Resolución (dxlib, 2026-10-03; checkout limpio HEAD 7a96756)
- `winx86-dx/` está versionado: `git ls-tree -r HEAD -- winx86-dx` enumera 26 archivos.
- `ctest -R '^winx86_dx_'`: 3/3 PASS (`ddraw`, `d3d`, `shims`); `run_shims.sh` PASS en `run_all`. El test ya no referencia una implementación local sin commit.
- `tools/build_host.sh` PASS desde worktree limpio con el contenedor ARM host, montando D2Vita como dependencia externa.
- `run_all.sh` del HEAD tiene 33 PASS, 2 SKIP y 2 FAIL, ambos en `interp60` (CTest y runner standalone; aserción nueva HW-066 de matching). `run_io.sh` y `run_shims.sh` pasan. Esta regresión pertenece al test/implementación de interp60 y no reabre el defecto `empty_peek`; queda coordinada para runtime.


## Verificación final (dxlib, 2026-10-03; HEAD 35c89f2)
- HEAD mantiene los 26 archivos versionados. CTest del library 3/3 y `run_all.sh` desde worktree limpio PASS=35 FAIL=0 SKIP=2; cierre reafirmado.
