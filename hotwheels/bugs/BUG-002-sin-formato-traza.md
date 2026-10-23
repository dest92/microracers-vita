# BUG-002 — El log de impl no tiene formato de traza; `diff_trace.py` no puede comparar
- Severidad: mayor · Estado: cerrado · Feature: HW-007 · Dueño: impl
## Síntoma (exacto, logs)
```
$ bash hotwheels/tools/smoke.sh 15 15        # etapa 3 (qemu-arm) verde espuria
$ python3 hotwheels/tools/diff_trace.py hotwheels/out/latest/run.log
el log de impl no contiene llamadas a APIs conocidas (¿formato? ver coordination/wine.md)
$ echo $?   # 2  (no 0/1)
```
`hotwheels/out/latest/run.log` sólo tiene líneas `[hw] base=…`, `[hw] sched: nativo`,
`[hw] muestra EIP: …`, `[hw] pila principal …`; ninguna `Dll.Func(args) = ret` ni `[hw] trap #N DLL!Func`.
`python3 hotwheels/tests/test_diff_trace.py` pasa (10/10), así que el parser no es el problema: falta la traza.
## Reproducción mínima
`bash hotwheels/tools/run_host.sh 15` → `python3 hotwheels/tools/diff_trace.py hotwheels/out/latest/run.log`
## Esperado vs observado
- Esperado: cada llamada Win32 hecha desde el exe, en orden, como `[Dll.]Func(args) = ret` (formato en `coordination/wine.md` §"Oráculo diferencial"), para que el oráculo marque la primera divergencia vs `docs/wine-ref/boot-trace.txt`.
- Observado: no hay ninguna llamada en ese formato; `diff_trace.py` sale 2 antes de comparar.
## Causa raíz (sólo si verificada)
No verificada. Depende de que el harness host emita la traza: `run_host` hoy sólo loguea `MISSING`/`trap #N`.
## Fix + test de regresión
- Pedido a impl: env `HW_TRACE=1` que loguee cada shim/trampa como `Dll.Func(arg0,arg1,…) = ret` (punteros a string como `"texto"`, resto `P`).
- Alternativa del harness: extender `diff_trace.py` para parsear `[hw] trap #N DLL!Func (ret=0x…)` (sólo nombres) y mapear ordinales (`DSOUND.dll!#1` = `DirectSoundCreate`), como sugiere el HANDOFF.
- Test de regresión: sobre una corrida host con `HW_TRACE=1`, `diff_trace.py` debe devolver 0 o 1 (nunca 2).

### Cerrado (2026-10-03)
Se adaptó `tools/diff_trace.py` al wrapper host `[hw] trace DLL!Func(args...) -> ret`, incluidos
argumentos escalares separados por espacios y puntero seguido de string. Test de regresión
`test_integrated_hw_trace_format` agregado a `tests/test_diff_trace.py`; suite 11/11 PASS. La
corrida trazada existente `out/run-20261002-234933/run.log` ahora llega a comparación y devuelve
código 1 por divergencia real tras una llamada coincidente, en vez del código 2 de formato inválido.
