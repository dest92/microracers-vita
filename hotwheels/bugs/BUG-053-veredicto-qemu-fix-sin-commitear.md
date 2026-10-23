# BUG-053 — Veredicto qemu: el fix de BUG-003 nunca se commiteó; HEAD sigue dando FAIL a corridas sanas por `GIL busy`

Nivel: **medido** (oc-qa, 2026-10-21, HEAD `3e69c13` y `ce837ec`, worktree aislado, `run_host.sh 20`). Dueño: **investigador** (autor del fix en el árbol de trabajo). Severidad: mayor (falso rojo permanente en la etapa 3 de `smoke.sh` → el gate no distingue una corrida sana de un cuelgue). Estado: abierto.

## Síntoma (exacto, logs)
`hotwheels/out/qa-clean-m8/verdict-ce837ec.txt` (HEAD `ce837ec`, corrida de 20 s):
```
FAIL
base=1 frames=58 cuelgue=[hw]   sched-native: GIL busy — racy snapshot follows
```
y el `run.log` de esa misma corrida (`out/run-20261021-211735…/run.log`):
```
[hw] muestra EIP: 0c300730 / 0c300770
[hw]   sched-native: GIL busy — racy snapshot follows
[hw] fin: shutdown requested exit=0x00000000 frames=1917 missing=0
```
O sea: **1917 frames, `missing=0`, apagado limpio** y sin `salida forzada` → la corrida es sana y el veredicto la marca FAIL.

## Reproducción mínima
```bash
bash hotwheels/tools/run_host.sh 20           # deja out/run-<stamp>/
bash hotwheels/tools/smoke_qemu_verdict.sh hotwheels/out/run-<stamp>/
```

## Esperado vs observado
- Esperado (lo que dice el cierre de BUG-003 en `bugs/BUG-003-smoke-falso-verde.md:56-58`): OK ⇔ `base` + `frames>0` + `fin: shutdown requested` + sin `salida forzada`; `muestra EIP`/`GIL busy` no deciden.
- Observado en HEAD: el script commiteado todavía usa el criterio viejo (`grep -ahE 'salida forzada|GIL busy|muestra EIP'` → `forced`, sin mirar `fin:`), así que cualquier corrida con el diagnóstico benigno del sampler (`GIL busy`, y `muestra EIP` que `host_main.cpp` emite siempre en los 2 s previos al corte) da FAIL.

## Causa raíz (verificada)
El fix existe **sólo en el árbol de trabajo**: `git status` → `M hotwheels/tools/smoke_qemu_verdict.sh` (+ `M hotwheels/tests/test_smoke.sh`), con el criterio nuevo ("NO se usa GIL busy como fallo"). El commit que lo integró nunca entró; `git show HEAD:hotwheels/tools/smoke_qemu_verdict.sh` sigue con `forced='...|GIL busy|muestra EIP'`.

Agravantes medidos en HEAD `9580f82`:
- `bash tests/test_smoke.sh` → **5 ok, 0 fallas**: la regresión commiteada no tiene el caso "sano con `muestra EIP`/`GIL busy` + `fin:` limpio" que atraparía este falso negativo (el caso 6 vive en el WIP).
- `run_all` no ejecuta `tests/test_smoke.sh` (sólo `ctest`, `tests/run_*.sh` y `tests/test_*.py`), así que el veredicto no tiene regresión en el gate.

## Fix + test de regresión
- Commitear `tools/smoke_qemu_verdict.sh` (criterio `base` & `frames>0` & `fin: shutdown requested` & sin `salida forzada`) junto con el caso nuevo de `tests/test_smoke.sh`.
- Regresión: dir sintético con `base` + frames + `GIL busy` + `muestra EIP` + `fin: shutdown requested` → **OK**; con `salida forzada` → FAIL.
