# BUG-054 — Veredicto qemu espurio bajo carga: el watchdog de shutdown usa un presupuesto fijo de 3 s

Nivel: **medido** (oc-qa, 2026-10-21 21:51–22:20, worktrees limpios de `f1ac5b9` y `02c54e2`; loadavg
anotado en cada corrida). Dueño: **integ/runtime** (watchdog de shutdown). Severidad: mayor (rompe el
gate del smoke de forma intermitente: falso FAIL con el guest sano). Estado: **abierto**.
Relacionado: BUG-038 (mismo síntoma; cerrado como no reproducible en 2026-10-04).

## Síntoma (exacto)
`tools/run_host.sh 20` + `tools/smoke_qemu_verdict.sh`:
```
[hw]   sched-native: GIL busy — racy snapshot follows
[hw]   thr 1 st=2 wait=- eip=0c3007xx sl=0
[hw] apagando
[hw] el scheduler no paró en 3 s: salida forzada
```
veredicto `FAIL base=1 frames=4x fin=0 forzada=1`; `missing=0` en todas las corridas (no se perdió un
frame; el guest no estaba colgado, simplemente no se lo scheduleó a tiempo).

## Medición (mismo sha, misma máquina, única variable = carga del host)
| HEAD | loadavg 1 min | resultado | frames guest |
|---|---|---|---|
| `f1ac5b9` | ~2–3 (ronda 7) | **FAIL** forzada | — |
| `f1ac5b9` | 1.73 | OK limpio | 1839 |
| `f1ac5b9` | 1.73 | OK limpio | 1865 |
| `02c54e2` | ~10 (ronda 8) | **FAIL** forzada | — |
| `02c54e2` | 9.66 | OK limpio | 877 |
| `02c54e2` | 10.14 | **FAIL** forzada | — |
| `3c105ad` | 11.15 | OK limpio | — (37 PNG) |

4 fallos y 3 verdes en 7 corridas, con los fallos concentrados donde el host estaba saturado
(loadavg ≈ 10 con 3 cores útiles ⇒ ~3× oversubscription), **aunque no es determinante**: una corrida
limpia salió OK con load 11.15. La variable real es si el invitado alcanza un safepoint dentro de los
3 s; la carga alta sólo hace más probable que no lo logre (starvation estocástica). En las verdes el
mismo commit no cambia.

## Causa (teoría, refutable)
El apagado es cooperativo: el watchdog adquiere el GIL, pide `request_shutdown` y espera que el hilo
invitado alcance un safepoint para soltar el GIL. Bajo oversubscripción, el invitado puede no ser
despachado durante >3 s: el presupuesto **fijo en tiempo de pared** mide *starvation del scheduler*,
no *liveness del guest*. Detector correcto de cuelgue = progreso del guest (frames dejando de avanzar),
no una espera de 3 s. Hipótesis refutable: con el mismo loadavg ≈ 10, subir el presupuesto a ≥15 s
debería dar 0/5 forzadas (y el cuelgue real seguiría detectándose por falta de progreso).

## Fix propuesto (a decidir por el dueño)
1. Presupuesto configurable y más holgado: `HW_SHUTDOWN_GRACE_S` (default 15 s; el smoke puede dejarlo
   en 3 s si se quiere estricto).
2. Espera por progreso: si el contador de frames del guest avanza, renovar el presupuesto; declarar
   forzada sólo si no hay progreso durante N s.
3. Veredicto: distinguir `fin=forzada` (timeout) de `fin=0` (guest muerto) y anotar `loadavg` en el
   reporte del smoke, para que QA no confunda ruido de carga con regresión.

## Impacto en el gate
`run_all` no lo ve (es integración, etapa 3 del smoke). Con el host cargado por varios agentes
compilando, el smoke pasa a FAIL espurio y tapa regresiones reales: hoy 3/5 corridas limpias de HEAD
dieron FAIL sin defecto en HEAD.

## Instrumentación para el fix (oc-qa, 2026-10-21; `53f6376`+`12f70f3`)
`tools/smoke_qemu_verdict.sh` ahora imprime la carga del host en el motivo:
`OK  / base=1 frames=877 fin=limpio load=9.66` y `FAIL / base=1 frames=41 fin=0 forzada=1 load=10.14`.
Regresión propia: `tests/test_verdict_load.py` (2 casos, rojo antes del cambio) y `tests/test_smoke.sh`
sigue 6/6. Sirve para que la evidencia del fix (presupuesto por progreso) muestre con qué carga se midió.
