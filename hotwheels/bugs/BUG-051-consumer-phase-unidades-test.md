# BUG-051 — HEAD rojo: `run_consumer_phase.sh` aborta por expectativa de unidades en el test (900 vs 900000 milésimas)

Nivel: **medido** (oc-qa, 2026-10-21 ~21:17, HEAD `9580f82`, worktree aislado). Dueño: **perf-host** (HW-073 CA4). Severidad: mayor (`run_all` rojo). Estado: **cerrado** (`df44389`; verificado por oc-qa en HEAD `df44389`).

## Síntoma (exacto, logs)
`hotwheels/out/qa-clean-m8/qa-oc3-run_consumer_phase.sh.log`:
```
test_consumer_phase: tests/test_consumer_phase.cpp:38: int main(): Assertion
`fmt(q, 10, 100'000) == "consumer_busy_us_per_frame=500 consumer_wait_us_per_frame=400 "
 "consumer_replay_us_per_frame=300 consumer_busy_pct_milli=50 "
 "consumer_draws_per_frame_milli=900 consumer_state_ops_per_frame_milli=4500"'
failed.
tests/run_consumer_phase.sh: línea 8: ... Abortado
```
El impl commiteado (`a20a19a`) emite `draws*1000/frames` = `900000` y `4500000` (milésimas por frame), no `900`/`4500`.

## Reproducción mínima
```bash
git worktree add --detach /tmp/q 9580f82
cd /tmp/q/hotwheels && bash tests/run_consumer_phase.sh    # aborta
```

## Esperado vs observado
- Esperado: el test commiteado codifica el contrato que el impl commiteado cumple (`run_all` verde).
- Observado: test e impl commiteados discrepan en el factor 1000; la corrección existe **sólo en el árbol de trabajo** (`git status` M en `hotwheels/tests/test_consumer_phase.cpp`: `900` → `900000`, comentario "-> 900000 milésimas").

## Causa raíz (verificada)
El test `f9135e4` (HW-073 CA4) se commiteó con la expectativa en unidades por-frame (`900`) mientras `src/d3d/consumer_phase.h` (impl `a20a19a`) reporta milésimas (`draws*1000/frames`); nadie commiteó el ajuste del test.

## Fix + test de regresión
- Commitear el `test_consumer_phase.cpp` corregido que ya está en el árbol de trabajo (paths :30-:42).
- Regresión: `bash tests/run_consumer_phase.sh` PASS en worktree limpio de HEAD.

## Re-verificación oc-qa (2026-10-21, HEAD `6ee09ac`, worktree limpio)
Sigue **rojo en HEAD**: `run_consumer_phase.sh` aborta con la aserción de unidades
(`consumer_busy_us_per_frame=500 … draws_per_frame_milli=900` vs `900000` esperado por el test).
El árbol compartido da verde **sólo por WIP no commiteado** en `tests/test_consumer_phase.cpp`;
cerrar requiere el fix commiteado (o el test corregido si la unidad correcta es la otra).

## El WIP de perf-host, probado aislado (oc-qa, 2026-10-21 22:52; HEAD `8c3e467`, worktree limpio)
Copié el `tests/test_consumer_phase.cpp` **sin commitear** del árbol compartido a un worktree limpio de
HEAD (sin tocar el original) y corrí `tests/run_consumer_phase.sh`:
```
CA4 OK: fases del consumer (busy/wait/replay + ocupación)
```
→ La corrección del test (esperar `900000`/`4500000` milésimas, más el caso base "1 draw en 1 frame =
1000 milésimas") es coherente con el impl de `consumer_phase.h` en HEAD. Commitearlo cierra BUG-051 y deja
el gate verde (HEAD da 81/1 con este único rojo).

## Verificación y cierre (oc-qa, 2026-10-21; HEAD `df44389`, worktree limpio)
- Fix `df44389`: el test fija la escala en milésimas (`900000`/`4500000`) y agrega el caso base
  "1 draw en 1 frame = 1000 milésimas", que es inequívoco contra una regresión de escala.
- Worktree limpio de HEAD: `run_all` → **PASS=82 FAIL=0 XFAIL=0 SKIP=5** — **gate verde por primera vez**
  desde que se abrió BUG-048..053 (`PASS run_consumer_phase.sh`).
- `build_vita` OK (VPK 1.946.653 B md5 `815567c4ec6335d8ad6b350b9339b327`); qemu 20 s: 58 PNG,
  `fin: shutdown requested … frames=1872 missing=0`, veredicto OK (`load=3.01`).
