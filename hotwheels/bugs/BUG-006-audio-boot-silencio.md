# BUG-006 — audio en el boot: dsound.wav en silencio (mmio/MCI de HW-015)
Nivel: qemu-arm (hotwheels-host, scheduler nativo). Dueño fix: fixer. Estado: resuelto.
Síntomas, con DSOUND (ports, HW-020) y los puentes MCI/mmio ya enganchados: missing=0, pero `dsound.wav` daba rms=0.
Causas (3, verificadas con `HW_MCI_LOG=1` + TRAPTAG):
1. `Mmio::descend` con flags=0 (= "chunk en la posición actual", el uso del juego) nunca encontraba
   nada (0x109 CHUNKNOTFOUND); FINDRIFF/FINDLIST ignoraban fccType; dwDataOffset de RIFF/LIST quedaba
   en p+12 (Win32: p+8, apunta al form type).
2. `Cdaudio` ignoraba `MCI_SET_TIME_FORMAT`: el juego pone MSF, pide STATUS POSITION/LENGTH por pista
   y hace PLAY FROM/TO en MSF absoluto; recibía TMSF y reproducía desde un punto inválido.
   Fix: MSF absoluto con pregap de 150 frames, sólo si se pidió explícitamente (CA11 sigue igual).
3. `resolve_music` buscaba `<game_root>/MUSIC/TrackN.wav`, pero el repack deja las pistas en `extracted/music/`.
Tests: `tests/run_winmm.sh` (casos BUG-006 nuevos) OK; run_ds_bridge/run_audio PASS.
Resultado: run_host 15 s → menú, missing=0, sin STUB, dsound.wav 48 kHz estéreo con rms≈2900 en cada segundo.
