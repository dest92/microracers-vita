# BUG-004 — HW-024: los binds físicos Start/Select/L1/L2/R1/R2 no se emiten en `GetDeviceState`
- Severidad: bloqueante · Estado: cerrado · Feature: HW-024 · Dueño: impl
## Síntoma histórico (antes del fix)
`dinput.cpp` asigna en `ButtonMap::for_pad(0)` los DIK nuevos —
`Start→ESCAPE`, `Select→PAUSE`, `L1/L2→RSHIFT`, `R1/R2→RCONTROL` (`dinput.cpp:27-33`)— pero
`Device::get_device_state` sólo recorre `Dpad*`, `Cross/Circle/Square/Triangle` y sticks
(`dinput.cpp:164-179`): nunca consulta `Button::Start/Select/L1/L2/R1/R2`.
Prueba end-to-end (pulsar cada bit `kPad*` y leer los 256 bytes):
```
bit=8 (L1)   → ESCAPE=00 PAUSE=00 RSHIFT=00 RCONTROL=00
bit=12(Start)→ ESCAPE=00 PAUSE=00
```
El test CA13 no lo detecta porque sólo mira `ButtonMap::for_pad`.
## Reproducción mínima
```
c++ -std=gnu++17 -Ihotwheels/src <prog>.cpp hotwheels/src/dinput/dinput.cpp -o /tmp/dit
# prog: setear pads[0].buttons=1<<12 (Start), get_device_state, leer st[dik::ESCAPE] -> 00
```
## Esperado vs observado
- Esperado: Start produce `ESCAPE=0x80`; L1/L2 `RSHIFT=0x80`; R1/R2 `RCONTROL=0x80`; Select `PAUSE=0x80`.
- Observado: todos quedan en 0. El "verde" de HW-024 sólo cubre X/O.
## Causa raíz (verificada)
`get_device_state` no tiene los `press(Button::Start/Select/L1/L2/R1/R2, pad.buttons & kPad*)`.
## Plan original de fix (completado abajo)
- Agregar los `press(...)` faltantes en `dinput.cpp` (teclado).
- Test end-to-end (como CA5) en `test_dinput.cpp`/`test_dinput_vita.cpp`: pulsar cada botón físico y
  verificar el DIK en el buffer de 256 bytes. Hoy ese test debe fallar (rojo) hasta el fix.


## Cierre vigente (2026-10-03)
`61ec373` agrega los `press(...)` faltantes de Start/Select/L1/L2/R1/R2 a `GetDeviceState`. CA14 en `tests/test_dinput.cpp` verifica cada DIK en el buffer de 256 bytes; `bash tests/run_dinput.sh` PASS (CA1..CA14). Revisión de `61ec373` APRUEBA (`coordination/reviewer.md`). Validación de pad físico en consola queda fuera del contrato bridge→DIK y no mantiene abierto este bug.
