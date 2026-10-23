# BUG-018 — tests/test_dinput.cpp no compila: redeclaración de `dword_property` (run_dinput.sh rojo)

Nivel: **medido en host** (qa, 2026-10-03, HEAD 0356601). Dueño: **luna** (autor de 3dbecf1 "cubrir tamaños restantes de propiedades DINPUT"). Severidad: mayor (run_all FAIL=1; cubre BUG-017). Estado: **cerrado (2026-10-03, review mimo)**.

## Evidencia
- `out/run_all-20261003-033227/run_dinput.sh.log`: `tests/test_dinput.cpp:169:13: error: redeclaration of 'uint8_t dword_property[20]'`.
- run_all PASS=17→ ahora 17/1/1; antes (2103ae3) verde. Código de producción sin cambios; sólo test.

## Acción
Renombrar/eliminar la segunda declaración (o reutilizar la variable previa); re-correr run_dinput.sh y run_all.

## Cierre (mimo, 2026-10-03)
Fix `d521acb` "consolidar regresiones": elimina el bloque duplicado (16 L) que redeclaraba
`dword_property` y **conserva la cobertura** — queda consolidada en `test_dinput.cpp:139-150`
(SATURATION short/válido en get/set, BUFFERSIZE y AXISMODE short/válido en set).
Verificación fresca con binario borrado (compilación nueva, sin binarios viejos):
`tests/run_dinput.sh` → **rc=0, OK (CA1..CA14)**; `tests/run_all.sh` → **PASS=18 FAIL=0 SKIP=1**
(`run_dinput` PASS). Corresponde cerrar también la fila en `hotwheels/BUGS.md` (WIP de luna).
