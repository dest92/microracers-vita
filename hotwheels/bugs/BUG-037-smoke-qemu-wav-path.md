# BUG-037 — smoke qemu 0 frames: `boot_to_entry: dsound: falta wav_path (host)` (working tree sin commitear)

Nivel: **medido en qemu** (qa, 2026-10-03 ~07:35, HEAD 06258da + working tree; 2 corridas). Dueño: **integ**. Severidad: bloqueante para smoke mientras el árbol esté así. Estado: **cerrado** (`e4a9551`, 2026-10-03).

## Evidencia
- `out/run-20261003-073345528974227-1213069/run.log`: `[hw] boot_to_entry: dsound: falta wav_path (host)`, frames=0; smoke rc: tests=0 build_vita=0 **qemu=1** vita3k=0. run_all 21/0/1 (host tests OK).
- Origen del mensaje: `src/integration/dsound_bridge.cpp:109` (`!cfg.vita && cfg.wav_path.empty()`).
- Working tree sin commitear: `tools/run_host.sh` ahora pasa `HW_WAV=1` sólo si está seteado (WAV opt-in) y cambia `STAMP`, `finalize_frames.py`, veredicto; `src/host/host_main.cpp`/`integrate.cpp` modificados. Hipótesis (no verificada contra HEAD limpio): el run por defecto ya no entrega wav_path/D2_SON y el host exige una ruta → desajuste entre run_host.sh nuevo y el host.
- Último verde con carga baja: 4f94565 (299 frames).

## Acción
Que host acepte sink nulo cuando no hay wav_path (o run_host.sh exporte HW_WAV por defecto); commitear tooling+host juntos y re-correr smoke.

## Cierre — bugs (2026-10-03)
`Engine::start` usa `DiscardSink` si `wav_path` está vacío y `dsound_bridge` acepta ese contrato sin exportar `WX86_SONDUMP`. La salida WAV se habilita sólo con `HW_WAV=1`. Regresión `run_audio.sh` PASS y `run_host.sh 30` ejecutó qemu hasta shutdown limpio con 344 frames (PNG contados desde los artefactos retenidos). La acción requerida quedó integrada en `e4a9551`.
