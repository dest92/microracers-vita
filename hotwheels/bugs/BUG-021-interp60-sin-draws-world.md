# BUG-021 — interp60 runtime no interpola draws del juego

- Estado: cerrado por fix `2b37f5c` y regresión `6cc7ff2`; HW-025 sigue `in_progress` por su gate 60 Hz.
- Severidad: mayor: el modo seleccionado presentaba rings pero repetía toda la geometría.

La documentación del juego ubica HUD/overlays en `Surface4::BltFast` de DirectDraw (`docs/wine-reference.md:142`); geometría va en los draws D3D. El runtime clasifica sólo ordinals `Op::Draw` del ring D3D como mundo; comandos desconocidos quedan fuera.

Regresión T0 pasó de error de compilación (helper ausente) a PASS: comprueba ordinals Draw e interpolación de dos draws. `bash tests/run_interp60.sh` y build Vita PASS. Vita3K `build-vita3k/hotwheels-vita-20261003-055651`: carrera visible en `shot_020.png`, 293 capturas, 7 rechazos, 41 ticks, 2.583 interpolados y 39.072 fallback.

El log con métrica de Hz, commit `2c537b6`, en `hotwheels-vita-20261003-060629` dio 1.520 Hz promedio al llegar a escena 300. BUG-021 cierra sólo el síntoma de clasificación; HW-025 no cumple 60 Hz sostenidos.
