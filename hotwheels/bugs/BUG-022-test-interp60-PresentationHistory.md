# BUG-022 — test_interp60.cpp no compila: `PresentationHistory` no existe (run_interp60.sh rojo)

Nivel: **medido en host** (qa, 2026-10-03, HEAD f989bb1). Dueño: **luna** (interp60; autor de 7dbff87). Severidad: mayor (run_all FAIL=1). Estado: **cerrado** (`2da7842`; métrica corregida en `efb4f56`; runner PASS).

## Evidencia
- `out/run_all-20261003-061913/run_interp60.sh.log`: `tests/test_interp60.cpp:96: error: 'PresentationHistory' was not declared in this scope` (+ `history` en 98/99).
- `grep -rn PresentationHistory src tests` → sólo el test: el commit 7dbff87 (test: detectar caídas de interp60 en ventana móvil) no trae la implementación en `src/fps/`. Posible TDD en rojo commiteado sin la parte de producción.
- Smoke PASS igual (239 frames, MISSING 0): producción sin impacto; sólo corta run_all/etapa tests (en smoke etapa tests dio rc=0 porque smoke corrió antes de/aislado; ver run_all).

## Resolución en curso (2026-10-04)
- Implementé `PresentationHistory` de capacidad fija para contar timestamps dentro de una ventana móvil; el test comprueba 60 Hz y detecta una caída a 40 Hz cruzando el límite de ventana. No se relajó ni condicionó el test.
- Evidencia: `bash tests/run_interp60.sh` PASS; `bash tests/run_all.sh` PASS=19/FAIL=0/SKIP=1; `bash tools/build_vita.sh` PASS (VPK 7.974.672 bytes).
- Código pendiente de commit/review; mantener el bug en seguimiento hasta re-review.

## Cierre por review (2026-10-04)
- `PresentationHistory` mantiene 620 timestamps en buffer fijo; cuenta desde `now` hacia atrás dentro de la ventana y corta al primer timestamp antiguo (historial monótono). La capacidad supera los 600 stamps de 60 Hz en 10 s; si se excede, el conteo conservador puede fallar el gate, no aprobarlo indebidamente.
- Review independiente del código de `2da7842` y del gate runtime: APRUEBA; sin defectos nuevos. Ver `coordination/bugs.md`.
- Repetí `bash tests/run_interp60.sh`: `test_interp60 OK`. BUG-022 cerrado. Esto sólo repara el runner; no demuestra el gate runtime HW-025.
- Corrección posterior: review independiente `coordination/review-2da7842.md` detectó un P2 de conteo inclusivo que puede inflar el mínimo de 1 s en el gate HW-025. BUG-022 (runner no compila) sigue cerrado; el defecto de métrica queda accionable bajo HW-025 y también está en `review-9cc0a13.md`.
- Resolución: `efb4f56` usa ventana semiabierta y agrega el borde 54/55 Hz; re-review APRUEBA en `coordination/re-review-efb4f56.md`. Sigue pendiente la medición de pacing sostenido de HW-025.
