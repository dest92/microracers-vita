# BUG-003 — `smoke.sh` reporta PASS con 0 frames (falso verde)
- Severidad: mayor · Estado: **cerrado** (2026-10-21: `2a5a358`+`690d9f3` verificados por oc-qa en HEAD `8c7fc17`) · Feature: HW-019 · Dueño: investigador
## Síntoma (exacto, logs)
`hotwheels/out/smoke-20261002-235907.md`:
```
- **Resultado global: PASS**
| 3 | run host qemu-arm | OK | 20s |
- Frames: **0**
- Nota: run_host.sh rc=1 (espurio si el artefacto existe)
```
Pero `hotwheels/out/latest/run.log` termina en el centinela, colgado (BUG-001):
```
[hw] muestra EIP: 0c3ffff0   (x8)
[hw]   sched-native: GIL busy — racy snapshot follows
[hw]   thr 1 st=2 wait=- eip=0c3ffff0 sl=0
[hw] el scheduler no paró en 3 s: salida forzada
```
El único criterio de la etapa 3 es `grep '\[hw\] base='` (`smoke.sh:68`), así que un cuelgue
post-boot pasa como `OK` y `smoke.sh` devuelve 0.
## Reproducción mínima
`bash hotwheels/tools/smoke.sh 15 15` → leer `Frames` y el `run.log` de `hotwheels/out/latest/`.
## Esperado vs observado
- Esperado: si el host queda clavado en el centinela y no presenta frames, la etapa 3 debe marcar
  FALLA (o al menos advertencia), y el reporte global no debe ser PASS.
- Observado: PASS global con 0 frames y `salida forzada`.
## Causa raíz (sólo si verificada)
Verificada: el criterio de la etapa no incluye frames ni detecta el timeout forzado. (Relacionado con
BUG-001, que es la causa del cuelgue; éste es el defecto de la herramienta que lo deja pasar.)
## Fix + test de regresión
- `smoke.sh`: exigir `FRAMES>0` y ausencia de `salida forzada`/`GIL busy` para dar OK en la etapa 3;
  usar el rc real del contenedor (hoy se ignora a propósito como "espurio").

### Resuelto (2026-10-03, investigador)
- Se extrajo el veredicto de la etapa 3 a `hotwheels/tools/smoke_qemu_verdict.sh` (testeable sin
  Docker). Criterio: **OK ⇔ `run.log` con `[hw] base=` AND frames>0 AND sin marcas de cuelgue**
  (`salida forzada` / `GIL busy` / `muestra EIP`). El rc de `run_host.sh` **no** decide (es
  espurio por su `set -e` + `grep` sin matches); se reporta aparte como nota.
- `smoke.sh` usa ese veredicto: en falla marca la etapa `FALLA(1)` y hace global `FAIL`; la
  sección qemu del reporte ahora imprime el motivo y la **última trampa** (o "sin tags de imports").
- **Test**: `hotwheels/tests/test_smoke.sh` (5 casos, verde): cuelgue+0 frames→FAIL, base+0
  frames→FAIL, sin base→FAIL, sano con tag→OK (y reporta la trampa), sano sin tags→OK.
- **Evidencia end-to-end**: `bash hotwheels/tools/smoke.sh 8 8` → `Resultado global: FAIL`, etapa 3
  `FALLA(1)`, `Nota: veredicto=FAIL (base=1 frames=0 cuelgue=... salida forzada)`. Ya no hay falso
  verde. BUG-001 (cuelgue de fondo) sigue abierto y es lo que ahora se ve.

### Falso negativo histórico corregido (2026-10-03, reviewer/QA)
`smoke_qemu_verdict.sh` incluye `muestra EIP` entre las marcas de cuelgue, pero `host_main.cpp:113-119`
emite `[hw] muestra EIP:` **siempre** en los ~2 s previos al corte de tiempo (cualquier corrida con
`secs>4`), incluso en una corrida sana que presenta frames y se apaga limpio. Resultado: una corrida
buena que llega al límite se marca **FAIL**. Verificado con un dir sintético (base + 3 frames +
`muestra EIP` + `fin: sentinel`) → `FAIL`.
Arreglo: no tratar `muestra EIP` como cuelgue (y evaluar `GIL busy`, que también puede aparecer en el
shutdown); el marcador fiable es `salida forzada` + `frames>0` + `base`. Agregar el caso "sano con
`muestra EIP` a `tests/test_smoke.sh`.

### Cierre vigente (2026-10-03; supersede falso negativo anterior)
El veredicto actual exige base, frames>0, `fin: shutdown requested` y ausencia de `salida forzada`;
`muestra EIP`/`GIL busy` no deciden por sí solos. `bash tests/test_smoke.sh`: 6/6 PASS.

### Reabierto (2026-10-21, oc-qa; HEAD `3e69c13`/`ce837ec`)
El criterio del "cierre vigente" **no está en HEAD**: `git show HEAD:hotwheels/tools/smoke_qemu_verdict.sh`
sigue tratando `GIL busy`/`muestra EIP` como cuelgue, y una corrida sana (1917 frames, `missing=0`,
`fin: shutdown requested`, sin `salida forzada`) sale **FAIL**. El fix está en el árbol de trabajo
(M) pero nunca se commiteó. Detalle, evidencia y fix en **BUG-053**.

### Verificado y cerrado (oc-qa, 2026-10-21; HEAD `8c7fc17`)
Los commits `2a5a358` (veredicto: OK ⇔ `base` + frames>0 + `fin: shutdown requested` + sin `salida forzada`)
y `690d9f3` (`tests/test_smoke.sh` con el caso limpio + `GIL busy`, 6/6) están en HEAD.
- `bash tests/test_smoke.sh` → **6 ok, 0 fallas** (incluye "limpio + GIL busy → OK").
- Corrida real `run_host.sh 20` en worktree limpio: `fin: shutdown requested … frames=2060 missing=0`,
  con `GIL busy` en el log → `smoke_qemu_verdict.sh` = **OK** (`base=1 frames=61 fin=limpio`).
  Evidencia: `hotwheels/out/qa-clean-m8/verdict-8c7fc17.txt`.
