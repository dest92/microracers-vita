# SPEC-041 — XTest de teclado en Vita3K headless

## Problema

BUG-041: el harness Vita3K permite boot y capturas, pero no tiene un canal versionado para inyectar teclas durante la corrida. `tools/vita3k/send_keys.c` existe, aunque el runner no lo compila ni ejecuta. Se espera que Vita3K traduzca teclas configuradas a botones SceCtrl que `VitaInputSource` consume; esa traducción sigue siendo una hipótesis hasta observar respuesta en el guest.

## Contrato

- `HW_VITA3K_KEY_EVENTS` es opcional. Formato: lista separada por comas `segundo:keysym:duracion`, compatible con `send_keys.c` (por ejemplo `1:Return:0.1,5:x:0.2,8:Up:12`).
- Si se configura esa secuencia, también se exige `HW_VITA3K_INPUT_EXPECT` con formato `waypoint-keymask:N` (N decimal, 1..15). El runner requiere una fila `[waypoint] tick x_q12 z_q12 keymask` cuyo último campo sea exactamente N; una fila periódica con máscara cero no satisface el criterio. Sin marcador o con formato inválido, rechaza la configuración antes del launch.
- Vacío/ausente conserva el comportamiento actual y no inicia ningún inyector.
- Con eventos, el runner los pasa al contenedor y ejecuta `send_keys` contra el DISPLAY Xvfb después de lanzar el juego. Su salida/estado queda en `send_keys.log`; eso sólo registra que XTest terminó.
- La secuencia usa tiempo monotónico desde el inicio del inyector; `send_keys` reporta fallo y el artefacto permanece para diagnóstico. No modifica `env.txt`, la configuración global de Vita3K ni el InputSource del port.

## Aceptación

1. Prueba Xvfb confirma KeyPress/KeyRelease en un cliente enfocado; sin eventos no se invoca el inyector.
2. El binario `send_keys` se compila en la imagen headless con XTest disponible; el foco queda en el widget clickeado, no se vuelve a robar al top-level.
3. Un smoke Vita3K con `HW_RACE_RECORD=1`, secuencia XTest y máscara esperada muestra una fila DINPUT del guest con esa máscara; sólo entonces el runner da PASS. El mensaje de salida del inyector o XFlush no cuenta como recepción del cliente.

## Fundamento de SO / ABI

XTest coloca eventos en la cola del servidor X; `XFlush` publica las solicitudes antes de que el inyector espere. La entrega X a un cliente Xvfb está probada. La traducción de Vita3K desde eventos de teclado a estado de pad y la observación posterior por `VitaInputSource::read_hardware`/DINPUT siguen sin demostración. La prueba debe distinguir evento X entregado de botón guest observado; un clic de foco o el boot exitoso no cuentan como input verificado.

## No objetivos

- No reemplaza el guion `HW_INPUT_SCRIPT` que alimenta DINPUT directamente.
- No cambia mapeos de botones ni el código Vita de `VitaInputSource`.
- No afirma validación de pad físico o consola real.
