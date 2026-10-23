# BUG-052 — `ab_cpu_frame.sh` depende del locale: con `LC_NUMERIC=es_AR.UTF-8` imprime `10,000` y el A/B muere al agregar

Nivel: **medido** (oc-qa, 2026-10-21 ~21:17, HEAD `9580f82`; entorno del usuario `LANG=es_AR.UTF-8`). Dueño: **herramientas de medición** (perf/QA). Severidad: menor (no toca el port, pero deja sin medición A/B cualquier shell en español). Estado: **cerrado** (`d7728c3`+`4b5c867`; review rev-gfx APRUEBA, `8c3e467`).

## Síntoma (exacto, logs)
`hotwheels/out/qa-clean-m8/test_ab_cpu_frame_pairing.py.log` (suite `run_all`):
```
pata 1 B: frames/ms_cpu_frame=100 20,000 load=5.73
pata 2 A: frames/ms_cpu_frame=100 10,000 load=5.73
Traceback (most recent call last):
ValueError: could not convert string to float: '10,000\n'
```
Con `LC_ALL=C` la misma suite pasa: `Ran 1 test ... OK`.

## Reproducción mínima
```bash
locale | head -2                     # LANG=es_AR.UTF-8, LC_NUMERIC="es_AR.UTF-8"
cd hotwheels
python3 tests/test_ab_cpu_frame_pairing.py                 # FAILED (ValueError)
LC_ALL=C python3 tests/test_ab_cpu_frame_pairing.py        # OK
```

## Esperado vs observado
- Esperado: la métrica y su parseo son independientes del locale (los números que consume `float()` son siempre `punto`).
- Observado: `awk 'printf "%.3f"'` respeta `LC_NUMERIC` y emite coma decimal; el bloque Python de agregación (`float(x)`) revienta.

## Causa raíz (verificada)
`tools/ab_cpu_frame.sh:40` (`awk '{printf "%.3f", ($1+$2)*1000/$3}'`) y `:74` (`a = [float(x) for x in open(...)]`) asumen separador `.` sin fijar `LC_ALL=C` (introducido en `2b09fca`/`d7a1ee5`; la regresión `test_ab_cpu_frame_pairing.py` de `2d52a8b` lo expone porque hereda el locale del shell). Nota: `TIMEFORMAT='cpu_user=%U ...'` también formatea según locale, así que el `grep -o "cpu_user=[0-9.]*"` puede quedar sin match con coma decimal.

## Fix + test de regresión
- `export LC_ALL=C` en la cabecera de `tools/ab_cpu_frame.sh` (o `LC_ALL=C awk ...` + parseo con `locale.atof` en el test).
- Regresión: la suite debe correr con `LC_ALL=es_AR.UTF-8` en CI/QA y pasar (hoy pasa sólo por el locale del autor).

## Fix (oc-qa, 2026-10-21)
- `d7728c3` (test, rojo): el test del par A/B corre un caso con `LC_NUMERIC=es_AR.UTF-8`
  (`run_pairing({"LC_NUMERIC": …})`, con `LC_ALL` limpio como el shell del usuario) y otro con
  `LC_ALL=C`; antes del fix el de es_AR fallaba con `ValueError: could not convert string to float: '10,000'`.
- `4b5c867` (fix): `export LC_ALL=C` en la cabecera de `tools/ab_cpu_frame.sh` (la métrica es un número
  que consume `float()`; C es el locale correcto para una salida de máquina). Cubre también `TIMEFORMAT`
  del host.
- Verificación: `python3 hotwheels/tests/test_ab_cpu_frame_pairing.py` → 2/2 OK con `LANG=es_AR.UTF-8`
  del shell; `run_all` en worktree limpio de HEAD `8c7fc17` → `test_ab_cpu_frame_pairing.py` **PASS**
  (PASS=57 FAIL=4: los 4 restantes son BUG-048 ×2, BUG-049, BUG-051).
- Review: rev-gfx **APRUEBA** (`8c3e467`; sin CAMBIOS; ver `coordination/review-8c3e467.md` si aplica).
