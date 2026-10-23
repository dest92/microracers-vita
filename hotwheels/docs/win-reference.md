# Hot Wheels original en Windows — referencia dinámica

Estado: el usuario lanzó el juego desde la sesión interactiva; `HotWheels.exe` siguió vivo durante una captura de
62,2 s. La música no se oyó. La captura de 60 s sí registró MCI/QPC/Sleep, pero no valores de retorno MCI.

## Reproducción

La copia aislada está en `D:\HotWheels-REA\Program_Files`; las pistas están en
`D:\HotWheels-REA\Program_Files\MUSIC\Track1.wav` … `Track4.wav`. El árbol contiene el ejecutable original
`HotWheels.exe` (430.080 bytes) y los cuatro WAV (206.918.056 bytes en total). El directorio está en D:, sin
instalación ni cambios fuera de `D:\HotWheels-REA`.

Para iniciar la captura, ejecutar `D:\HotWheels-REA\Launch-HotWheels-Capture.bat`. El BAT abre el observador Frida
y el juego con `Program_Files` como directorio de trabajo. El observador espera hasta 10 minutos, se adjunta al PID
`HotWheels.exe` y captura 60 s en `D:\HotWheels-REA\trace.jsonl`. Se instaló Frida 17.24.3 en el venv
`D:\HotWheels-REA\frida-venv`; el chequeo remoto obtuvo 290 procesos visibles. El script de hooks está en
`D:\HotWheels-REA\observe.js`.

El JSONL registra MCI (`mciSendCommandA`/`mciSendStringA`), apertura y bytes por llamada `ReadFile` de `Track*.wav`,
formato/flags/tamaño de `CreateSoundBuffer`, `Lock`/`Unlock`, posiciones y controles de `IDirectSoundBuffer`, y
muestreo de QPC/Sleep. Cada evento lleva tiempo relativo y thread ID; no lee ni guarda contenido de buffers WAV.

## Hallazgos

La primera ejecución produjo sólo un error del agente, sin eventos del juego: `Module.findExportByName is not a
function` (Frida 17.24.3). El API de Frida 17 movió la búsqueda de exports a `Process.findModuleByName(...).findExportByName(...)`;
el hook ya fue actualizado conforme a la [API oficial de Frida](https://frida.re/docs/javascript-api/). También corregí
la lectura de `GetCurrentPosition` para guardar los valores de play/write cursor y el offset de `lpwfxFormat` en
`DSBUFFERDESC`. No se atribuyen cantidades, cadencias ni threads observados. Hace falta repetir la carrera OFFICE
durante aproximadamente 60 s.

| Prioridad | Qué medir | Resultado |
|---|---|---|
| a | MCI, secuencia y thread; `Track*.wav` abiertos, cantidad/tamaño y patrón de `ReadFile` | 17 MCI: PLAY/PAUSE/STATUS; no `file_open`/`file_read` de pistas; ver diagnóstico de audio abajo |
| b | `CreateSoundBuffer`, formato y tamaño; offsets/tamaños de Lock/Unlock; posiciones y controles | Sin eventos DirectSound en esta ventana |
| c | QPC/Frequency y Sleep; cadencia del paso de juego cercano a 40 Hz | QPF=10 MHz; 439 muestras QPC y 24.431 Sleep en 62,2 s; Frida había emitido QPF repetido, corregido para próximo run |

### Diagnóstico de música en Windows

La traza completa `hotwheels/out/win-rea/attempt-4-user-run.jsonl` muestra comandos sobre MCI device 1: `MCI_PLAY`
(`0x806`, flags `0xC` = FROM|TO y luego `0x8` = TO), `MCI_PAUSE` (`0x809`) y `MCI_STATUS` (`0x814`, flag `0x100`
= ITEM), con llamadas STATUS/PLAY aproximadamente cada 4,1 s. El hook anterior sólo guardó parámetros crudos en
`onEnter`: no registró `MCIERROR`, el elemento/tipo de OPEN ni los valores de STATUS. MCI_OPEN no aparece y pudo
ocurrir antes del attach. No se observó `mciSendString`, lectura de `MUSIC\Track*.wav` ni DirectSound.

Lectura de sólo lectura en Windows: G: es `Microsoft Virtual DVD-ROM`, label `26413_0919`, con `HWICON.BMP` y
contenido del instalador; el ISO es sólo datos, sin pistas CD Red Book. `CDAudio` x86 está registrado como
`mcicda.dll`. Los cuatro WAV sí existen en `D:\HotWheels-REA\Program_Files\MUSIC` (55.102.870 / 55.279.270 /
46.682.710 / 49.853.206 B), pero no hay `_inmm.dll` ni `_inmm.ini` junto al exe para redirigir MCI CD audio a esos
WAV. `DATA\MENU\settings.dat` tiene 18 valores: está verificado que línea 1 es 3D=0 y línea 2 resolución=1; los
campos restantes no están mapeados a sonido. El BAT fija cwd en `Program_Files`.

Hipótesis principal, aún pendiente de los retornos MCI: el exe intenta reproducir CD audio desde un ISO de datos, que
no contiene Red Book; los WAV extraídos no se usan automáticamente. El shim del port sustituye `cdaudio` y mapea
`MCI_PLAY` a `MUSIC\TrackN.wav`, pero esa lógica no existe en el exe Windows sin `_inmm`.

### Diagnóstico del crash de arranque

`HotWheels.exe` falla con `0xc0000005` en RVA `0x234b3` (VA `0x004234b3`, `.text`). El desensamblado en esa
dirección hace `MOV EAX, [EAX+0xC]`. El valor viene de una llamada a `FUN_004472e0`, que recorre los modos de
pantalla y elige el nodo que coincide con ancho/alto/formato; si no encuentra uno devuelve NULL. Este camino de
inicio no comprueba NULL antes de leer `+0xC`. El llamador vecino `FUN_00404660` sí valida el mismo resultado antes
de leer los campos, lo que apoya que el AV es una ruta de resolución de pantalla sin modo coincidente. Es evidencia
estática de Ghidra; no es un fallo de CD, registro ni timer.

La ejecución remota directa con `Start-Process` y directorio de trabajo `Program_Files`, sin Frida, reprodujo el
mismo módulo, excepción y offset (`HotWheels.exe`, `0xc0000005`, `0x234b3`) dentro de 8 s. El crash ocurre sin el
observador, así que Frida no es la causa. `WINXPSP3` tampoco lo cambia. `WIN98` cambia la ruta y produce AV en
`winmmbase.dll` (`0x5ab3`), sin llegar a una ventana estable.

Se probó DDrawCompat v0.7.2 x86 (SHA-256 del archivo instalado: `94895CF6EDE8FD0F74EE1447FDC08B7044A9C65A1A25A49A4DF893A0370975A3`), con el `ddraw.dll` junto al exe. El wrapper carga, informa modo inicial 1024x768x32@60 y engancha DirectDraw/Direct3D, pero el proceso aún produce AV dentro de `DDRAW.dll` en offset `0x53849`. Agregar 320x240 a la lista de modos no lo evitó; restringir a 640x480 y combinar `WinVersionLie=98` tampoco. Los logs son `hotwheels/out/win-rea/ddrawcompat-attempt1.log` y `ddrawcompat-attempt2.log`.

El intento de dgVoodoo2 v2.87.4 oficial quedó en cuarentena por Microsoft Defender (`Trojan:Win32/Kepavll!rfn`). No se sorteó la detección ni se instaló. Es un bloqueo de seguridad observado, no evidencia de que el paquete sea malicioso.

Resultado actual: **sin lanzamiento visible ni proceso estable**. El siguiente paso verificable requiere depurar el minidump de DDrawCompat con símbolos o repetir desde la sesión de escritorio interactiva para observar enumeración de modos y configuración de pantalla. No declarar el juego arrancado por el mero hecho de que la DLL wrapper cargue.

Evidencia esperada: `[controlada, Windows 11 x64, Frida]` en `hotwheels/out/win-rea/trace.jsonl` una vez copiada
desde D:. El trazador no demuestra por sí mismo audibilidad o exactitud de tiempo; registra llamadas y argumentos en
el proceso observado.
