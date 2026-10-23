# BUG-041 — el teclado XTest del runner Vita3K no llega a DINPUT

- Estado: bloqueado tras reintentos headless; bloquea la captura interactiva de HW-030. Dueño de seguimiento: integ/harness Vita3K.
- Nivel: emulador Vita3K; no implica un defecto del port ni del backend DINPUT.
- Dueño sugerido: integ / harness Vita3K.

## Evidencia

Cuatro corridas del build Vita con `HW_RACE_RECORD=1` (80, 65, 55 y 50 s) bootearon y terminaron por timeout normal. Inyecté Return/X y flechas por XTest en el Xvfb `:98`; una corrida mostró el diálogo modal de actualización. El inyector luego pulsó OK y enfocó la ventana/viewport, pero el resultado siguió igual: `CreateDevice size=640x480`, `guest_d3d_frames=0`, ningún `[waypoint]` en `run.stdout`, y `shot_050.png` permanece en el menú. No hay crash.

Runs: `build-vita3k/hotwheels-vita-20261003-155517620936232-2326789`, `...155706472114866-2331141`, `...160546038712019-2349483`, `...160710956945835-2356667`. `env.txt` se restauró al contenido previo después de cada prueba.

## Próximo paso

Comprobar cómo Vita3K recibe controles de teclado/gamepad en Xvfb o exponer un canal de control de test que alimente `VitaInputSource`. Con una carrera activa, confirmar que `GetDeviceState` ve Up/Left/Right y que el recorder emite muestras antes de hacer un replay largo. QEMU no está disponible en este entorno.

## Seguimiento integ — canal XTest y verificación guest (2026-10-04)

Detecté que `send_keys.c` estaba versionado pero nunca conectado a `vita3k_run.sh`. Integré `HW_VITA3K_KEY_EVENTS` y un marcador obligatorio `HW_VITA3K_INPUT_EXPECT` para que el runner no confunda boot con input recibido. El Xvfb aislado confirma KeyPress/KeyRelease en un cliente testigo (imagen `vita3k-headless:26.10-xtest2`, test 1/1); build de imagen compiló el helper. También dejé el foco en el widget clickeado, sin restaurar foco al top-level después.

Prueba guest Vita3K con `HW_RACE_RECORD=1` en stage y XTest `14:Return:0.2` por 18 s: boot=1, 19 screenshots, `CreateDevice` 640×480, `guest_d3d_frames=0`, cero escenas/waypoints. Con `HW_VITA3K_INPUT_EXPECT=[waypoint]`, runner **FAIL** por marcador ausente (container_rc=0); conserva `hotwheels/build-vita3k/hotwheels-vita-20261003-165756888328508-2519058`. Dos corridas largas previas de 35/48 s también quedaron en el menú. La Xvfb cliente prueba entrega X, no que Qt/Vita3K lo traduzca a SceCtrl/DINPUT.

**Estado BUG-041: abierto, sin resolución guest.** El marcador obligatorio quita el falso PASS. No tomar captura de carrera ni cerrar HW-030 hasta observar `[waypoint]`/DINPUT guest real; investigar la jerarquía de ventanas y traducción teclado→controlador de Vita3K. El stage temporal fue retirado y `env.txt` del perfil compartido restaurado byte por byte.

### Gate y re-review — 2026-10-04

El primer code review marcó P2 porque el test era sólo textual y el runner informaba PASS por boot. Cambios posteriores: `fd5bce0` conserva el foco del widget tras el click; la suite inicia `xev` en la imagen y observa KeyPress+KeyRelease (1/1); `eb8536d` exige `HW_VITA3K_INPUT_EXPECT` y coteja el marcador como literal. La suite total `python3 tests/test_vita3k_runner_input.py` pasó 6/6. Build real de `vita3k-headless:26.10-xtest2` compiló `send_keys.c` con gcc/libc6-dev/libXtst.

Smoke Vita3K 18 s con evento Return y expectativa `[waypoint]`: boot=1, `container_rc=0`, pero marcador ausente y `guest_d3d_frames=0`; runner devuelve FAIL, run retenido en el path de arriba. Una corrida de 48 s después del ajuste de foco siguió sin escenas/waypoints. El test Xvfb sólo prueba que el cliente X recibe la secuencia.

Revisión de spec encontró que `[waypoint]` también se emite cada ocho polls con `keymask=0`; el gate literal podía aceptar una fila sin input. `0e952e2` cambia el contrato a `HW_VITA3K_INPUT_EXPECT=waypoint-keymask:N` y compara el último campo numérico de la fila contra N (1..15). Test de regresión cubre máscara cero, distinta y esperada (suite focalizada 7/7). La spec aclara que X→SceCtrl sigue siendo hipótesis. BUG-041 permanece abierto: no se repitió el smoke guest con máscara Up=1 y aún no hay evidencia de teclado→DINPUT.

Reintento final (2026-10-03): stage temporal con `HW_RACE_RECORD=1` (sin `HW_INPUT_SCRIPT`), siete pulsos XTest `Return` y `Up` sostenido 2 s. Boot=1, container_rc=0, 22 screenshots, 1.900 presents software; `guest_d3d_frames=0`, `waypoint-keymask:1` ausente y el runner dio FAIL como corresponde. Artefacto: `build-vita3k/hotwheels-vita-20261003-171816690339683-2608044`. El `env.txt` previo se restauró byte a byte. BUG-041 sigue abierto; sin recursos para más corridas/emulador en esta sesión.

### Reintento render con entorno aislado (2026-10-03)

Corrí `vita3k_run.sh` durante 45 s con lock `VITA3K_BUSY` dueño `render`, `HW_RACE_RECORD=1`, XTest Return repetido y Up, y un stage que reemplazó explícitamente `ux0:data/hotwheels/env.txt` por ese único flag (sin `HW_INPUT_SCRIPT`). El runner desactivó el diálogo de updates, inició `HWMR00002` y capturó 22 screenshots. Log: `build-vita3k/hotwheels-vita-20261003-175752859368008-2715973`.

Resultado: `CreateDevice` S_OK a 640×480, `guest_d3d_frames=0`, cero `[waypoint]`; el gate obligatorio `waypoint-keymask:1` falló. La captura `shot_040.png` no muestra una carrera con input observado. Esto descarta que el `HW_INPUT_SCRIPT` persistente explique por sí solo los intentos fallidos. Se restauró el `env.txt` previo y el lock quedó retirado. BUG-041 sigue abierto y HW-030 bloqueada; no hay base para afinar el piloto por posición sin una carrera interactiva observable.

### Reintento render XTest con env limpio (2026-10-03, 50 s)

Run `build-vita3k/hotwheels-vita-20261003-182245316715494-2733113`; owner del lock `render`. El stage sustituyó `env.txt` por `HW_RACE_RECORD=1`, sin `HW_INPUT_SCRIPT`; secuencia XTest: Return, x, Return, Up, x, Return, luego Up sostenida. Boot=1, 22 capturas, CreateDevice 640×480 S_OK, `guest_d3d_frames=0`, sin `[waypoint]`; gate `waypoint-keymask:1` FAIL. `shot_040.png` mantiene la pantalla de selección. Perfil anterior restaurado y lock retirado. Sumado al reintento aislado de 45 s, esto confirma que quitar el override guionado no alcanza: sigue sin input observable en el guest. No iniciar grabación/piloto por waypoints hasta resolver foco/traducción o usar un canal guest verificable.


## Estado actualizado por render (2026-10-03)

**Bloqueo confirmado tras dos reintentos aislados adicionales (45 s y 50 s).** Ambos bootearon, obtuvieron CreateDevice S_OK 640×480 y aceptaron el envío XTest a Xvfb, pero `guest_d3d_frames=0` y no hubo ninguna muestra `[waypoint]`; el segundo screenshot aún muestra la pantalla de selección. El env de ambos stages no tenía `HW_INPUT_SCRIPT`, por lo que un override scripted no explica la falla. El harness prueba KeyPress/Release en Xvfb, pero no la traducción Qt/Vita3K → SceCtrl → DINPUT ni el foco del widget que contiene el juego. No seguir gastando corridas iguales: destrabar inspeccionando foco/ventana efectiva o agregando un canal de control guest observable. HW-030 queda bloqueada por este bug; BUG-025 no tiene ruta grabada para calibrar el piloto.
