# BUG-023 — out/ sin límite de tamaño: disco lleno (0 MB) mata corridas en curso

Nivel: **medido en host**, mimo, 2026-10-03. Severidad: mayor (bloquea al equipo:
sin espacio, docker/daemon y builds fallan). Estado: **cerrado con política global de retención**, 2026-10-03. Feature: HW-027.

## Evidencia
- `df`: `/` 164G **155G usados /0 disponibles (100%)** durante el soak de30 min.
- `du -sh hotwheels/out` = **32G** en **635 corridas** (runs de smoke/qa/soak de todo el día).
- El soak de30 min acumuló **4.2-5.9G de frames** en un solo run
  (`out/run-20261003-060309` y `061314`); el fallo se manifestó como
  `what(): PNG write fallo` → `dumped core` → `== salida no cero: 134`
  (rc reportado sólo porque el disco quedó en134 MB).
- Liberación aplicada por mimo (sólo runs propios): borrado de `frame_*.png` de
  runs propios → `df` de0 a **8.1G libres**; `out/` de32G a24G.

## Mitigación inicial en soak.sh
- Criterio (6): aborta si libre < `SOAK_MIN_FREE_MB` (300).
- Limpieza periódica: no acumular > `SOAK_MAX_FRAMES` (200) durante el run.
- Al final conserva `SOAK_KEEP_FRAMES` (10+10) y borra el resto.

## Cierre — política de todos los runners (2026-10-03)
La SPEC-020 limita los PNG host a cada 50.º frame y los 20 más recientes; `HW_FRAMES_ALL=1` es opt-in. WAV requiere `HW_WAV=1`. `run_host`, `run_race`, `vita3k_run`, `smoke` y `run_all` desactivan core dumps/limpian sus propios artefactos al aprobar y conservan evidencia al fallar; `soak` rota y limpia al finalizar. `prune_out.sh` mantiene las 3 corridas no citadas más recientes y reduce corridas citadas a evidencia mínima.

Verificación: `run_frame_retention.sh`, `run_audio.sh`, `test_smoke.sh` 6/6, `run_ddraw.sh`, `run_d3d.sh`, shell syntax checks y qemu real de 30 s (344 frames, shutdown limpio, 27 PNG retenidos) PASS. Cambios de retención: `e4a9551`; política soak: `6f0361b`. Se preservan las corridas fallidas para diagnóstico y el historial citado no se elimina.
