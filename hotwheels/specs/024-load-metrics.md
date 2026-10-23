# Spec 024 — Métricas de arranque/carga en qemu (`tools/load_metrics.sh`)

Problema: no hay serie de **tiempos de carga** (prioridad 4 de AGENTS.md) ni
línea base de fps de arranque; cualquier cambio de runtime/render se "mide a ojo".

Contrato observable:
- `tools/load_metrics.sh --analyze <RUNDIR>` analiza UN run de `run_host.sh`
  y emite por stdout un registro `KEY=valor` con:
  - `ttf1_s` = segundos desde el **origen del run** hasta el mtime del primer
    `frame_*.png`. El origen se parsea del STAMP del nombre
    (`run-YYYYmmdd-HHMMSS…`, resolución 1 s, instante de creación del dir):
    el mtime de `run.log` **no** sirve porque crece con cada escritura y
    producía ttf1 negativo. Dirs sin STAMP (sintéticos) usan el mtime de
    `run.log` como fallback.
  - `fps_10_30` = pendiente de frames entre t=10 s y t=30 s respecto del mismo
    origen (0 si no hay frames en ambas ventanas).
  - `frames_60` = frames con mtime ≤ origen+60 s.
  - `frames_total` y `duration_s` (último frame − origen).
- `tools/load_metrics.sh [runs=3] [sw|d3d]` corre `runs` corridas con
  `HW_INPUT_SCRIPT` del soak (menú→carrera), analiza cada una y al final imprime
  el agregado `TTF1_MIN/MAX/MED`, `FPS_MIN/MAX/MED` (por stdout, líneas
  `AGG k=v`) y escribe `out/load-metrics-<stamp>.csv`.
- Gate opcional: `LOAD_TTF1_MAX_S` / `LOAD_FPS_MIN` no vacíos → rc1 si alguna
  corrida los excede/viola (regresión de carga).
- rc del runner: rc≠0 de `run_host.sh` → fila `RUN_FAIL` y rc1 **siempre**
  (y su dir se conserva como evidencia); corrida sin `== <rundir>` → `SIN_RUN`
  + rc1. `frames_total=0` (dir existe pero sin PNG) → fila `SIN_FRAMES` con
  rc1 **sólo si hay gate** (sin gate: reporte, no falla).

Criterios de aceptación (tests):
1. `tests/run_load_metrics.sh` fabrica un RUNDIR sintético (frames con mtimes
   conocidos: primer frame +12 s,2 frames/seg entre10-30 s) y exige valores
   exactos de `ttf1_s`, `fps_10_30`, `frames_60`.
2. Sin frames → `ttf1_s=NA` y rc0 (análisis nunca truena; la corrida completa
   reporta `frames_total=0` como falla de gate sólo si se pidió gate).
3. `bash -n` de los dos scripts.

No-objetivos: instrumentar el guest (sin hooks de timestamp en el exe); medir
fases internas (menú/carrera) — sólo señales del host (PNGs y run.log);
métricas en consola (eso queda para integ con consola).
