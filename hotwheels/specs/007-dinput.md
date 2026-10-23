# SPEC-007 — DirectInput (teclado, mouse, joystick Device2) (HW-016)

- Feature: HW-016 · Milestone: M4 · Dueño: `deepseek` · Estado: borrador
- Depende de: HW-005 (esqueleto/bridge). No depende de DDRAW ni del renderer.
- Fuentes: `docs/re-engine.md` §5 y §169, `docs/gap-list.md` §DINPUT, `docs/ports-patterns.md` §1.5,
  patrón COM de `d2vita/D2Vita/third_party/winx86/src/runtime/ds_emul.cpp`,
  headers MinGW `shared/opt/llvm-mingw-*/include/dinput.h`.

## Problema

`HotWheels.exe` importa **un solo** símbolo de `DINPUT.dll`: `DirectInputCreateA`
(`docs/re-engine.md` §2). Todo lo demás pasa por vtables COM:

- `inpInit` (0x41b6a0): `DirectInputCreateA`, `CreateDevice` de teclado y mouse, `SetDataFormat`,
  `SetCooperativeLevel`, `Acquire`.
- `inpEnumJoysticks` (`EnumDevices`) → `inpInitJoystick`: `CreateDevice` + `QI(IID_IDirectInputDevice2A)`,
  `SetDataFormat`, `SetCooperativeLevel`, `SetProperty`×4 (rango/deadzone de ejes X/Y/Z), `Acquire`.
- `inpUpdate` por frame: teclado `GetDeviceState(256)` (re-`Acquire` si falla); joystick `Poll` +
  `GetDeviceState`. El mouse además por mensajes Win32 (fuera de esta spec).
- Teclas por defecto del juego (`re-engine.md` §169): **J1** ↑↓←→ + RShift + RCtrl;
  **J2** T G F H + LShift + LCtrl. El port debe traducir `sceCtrl`/`sceTouch` a esas teclas y a un
  joystick DInput.

## Contrato observable

Unidad portable C++17 en `hotwheels/src/dinput/`, sin Windows ni winx86. La fuente de estado entra
por una interface abstracta; el backend Vita (`sceCtrl`/`sceTouch`) queda fuera.

### Fuente abstracta de input

```cpp
struct PadState { int32_t lx, ly, rx, ry; uint32_t buttons; int32_t pov; };  // ejes -32767..32767
struct InputSnapshot {
    uint8_t keys[256];       // estado DIK crudo (0x80 = presionada), opcional
    int32_t mouse_dx, mouse_dy, mouse_dz;
    uint32_t mouse_buttons;  // bit0 left, bit1 right, bit2 middle
    int pad_count; PadState pads[2];
};
struct InputSource { virtual InputSnapshot poll() = 0; };
```

### Modelo COM (equivalente portable de las vtables)

- `DirectInputCreateA(hinst, version, &lpDI, punkOuter)` → `IDirectInputA`.
  `QI(IID_IDirectInputA)` = sí; otro IID = `DIERR_NOINTERFACE`.
- `IDirectInputA` (slots: 3 `CreateDevice`, 4 `EnumDevices`, 5 `GetDeviceStatus`, 6 `RunControlPanel`,
  7 `Initialize`): `CreateDevice(GUID_SysKeyboard/SysMouse/Joystick)`; `EnumDevices(DIDEVTYPE_*)`
  recorre teclado, mouse y joystick (alias `DIDEVTYPE_DEVICE`/`DI8DEVCLASS_ALL` = los tres).
- `IDirectInputDeviceA` (slots 3..17) y `IDirectInputDevice2A` (slots 18..; `Poll` = 25):
  - `SetDataFormat` acepta teclado (`c_dfDIKeyboard`, 256 B), mouse (`c_dfDIMouse`, 20 B) y
    joystick (`c_dfDIJoystick`, 80 B); otro → `DIERR_INVALIDPARAM`.
  - `SetCooperativeLevel(flags)` acepta cualquier combinación de `DISCL_EXCLUSIVE/NONEXCLUSIVE/
    FOREGROUND/BACKGROUND` y sólo se aplica en `Acquire`.
  - `Acquire`/`Unacquire`: `Acquire` refresca el snapshot (`InputSource::poll`); `GetDeviceState`
    sin `Acquire` → `DIERR_NOTACQUIRED`.
  - `GetDeviceState(cb, buf)`:
    - Teclado: 256 bytes, 0x80 por cada DIK mapeado y presionado (el mapper consume `pads[0]`/`pads[1]`
      y, opcionalmente, `keys[256]`).
    - Mouse: `DIMOUSESTATE` (lX,lY,lZ + 4 botones) desde `mouse_*`.
    - Joystick: `DIJOYSTATE` (lX,lY,lZ,lRx,lRy,lRz, sliders, 4 POV, 32 botones) desde `pads[i]`.
      `GetDeviceState` de joystick usa el último `Poll`.
  - `GetDeviceData` sin `DIPROP_BUFFERSIZE` → `DIERR_NOTBUFFERED` (el juego usa `Poll`+`GetDeviceState`;
    no se observó `GetDeviceData`).
  - `GetCapabilities` informa ejes/botones/POV por tipo; `GetDeviceInfo` GUID de instancia y nombre;
    `SetProperty`/`GetProperty` aceptan `DIPROP_RANGE`/`DIPROP_DEADZONE`/`DIPROP_SATURATION` (id de
    propiedad = valor entero del `REFGUID`, como en Win32).
- HRESULTs: `DI_OK=0`, `DIERR_NOTACQUIRED=0x8007000C`, `DIERR_ACQUIRED=0x800700AA`,
  `DIERR_INVALIDPARAM=0x80070057`, `DIERR_NOINTERFACE=0x80004002`, `DIERR_UNSUPPORTED=0x80004001`,
  `DIERR_NOTBUFFERED=0x80040207`.

### Mapeo configurable botón → DIK_*

`ButtonMap` traduce botones abstractos (`DpadUp…`, `Cross…`, `LStickUp…`, `RStickUp…`) a scancodes
DIK. Defaults (confirmados en `re-engine.md` §169; el orden J2 es configurable):

| pad | d-pad / stick | Cross/Triangle | Circle/Square | Start | L1/R1 |
|---|---|---|---|---|---|
| 0 (J1) | ↑↓←→ = `DIK_UP/DOWN/LEFT/RIGHT` | `DIK_RETURN` | `DIK_BACK` | `DIK_ESCAPE` | `DIK_RSHIFT`/`DIK_RCONTROL` |
| 1 (J2) | `DIK_T/G/F/H` (T=acelerar, G=frenar, F=izq, H=der) | `DIK_LSHIFT` | `DIK_LCONTROL` | — | — |

El mouse se alimenta de `InputSnapshot`; opcionalmente el stick derecho genera deltas
(`mouse_from_right_stick`, con deadzone). Todo el mapa es reemplazable (config del usuario en una
feature posterior).

## Criterios de aceptación (cada uno ⇒ un test host, `hotwheels/tests/test_dinput.cpp`)

1. **CA1** — `DirectInputCreateA` crea el objeto; `QI(IID_IDirectInputA)` ok y otro IID da
   `DIERR_NOINTERFACE`.
2. **CA2** — `CreateDevice` con `GUID_SysKeyboard`/`SysMouse`/`Joystick` devuelve el tipo correcto;
   GUID desconocido → `nullptr`.
3. **CA3** — `EnumDevices` recorre los 3 dispositivos (y por tipo filtra) con el GUID/nombre correctos.
4. **CA4** — `SetDataFormat` de teclado/mouse/joystick ok; formato incompatible → `DIERR_INVALIDPARAM`;
   `GetDeviceState` sin `Acquire` → `DIERR_NOTACQUIRED`.
5. **CA5** — con pad 0 d-pad ↑ y Cross apretados, el estado de teclado pone `0x80` en
   `DIK_UP`/`DIK_RETURN` y `0` en el resto; al soltar, limpia.
6. **CA6** — `SetCooperativeLevel` registra las flags; `Acquire` refresca el snapshot (cuenta de polls).
7. **CA7** — estado de mouse: `lX/lY/lZ` y botones reflejan `InputSnapshot`.
8. **CA8** — joystick: `QI(IID_IDirectInputDevice2A)` y `IID_IDirectInputDeviceA` ok; `Poll` +
   `GetDeviceState` mapean ejes a `lX/lY/lRx/lRy`, POV y botones; `SetProperty(RANGE/DEADZONE)` ok.
9. **CA9** — `GetDeviceData` sin buffer → `DIERR_NOTBUFFERED`; `Acquire` doble no rompe.
10. **CA10** — el `ButtonMap` es reconfigurable: `Cross → DIK_SPACE` cambia el `GetDeviceState`.
11. **CA11** — `GetCapabilities` (ejes/botones) y `GetDeviceInfo` (guids/nombres) devuelven lo esperado.
12. **CA12** — índices de slots COM (CreateDevice=3, EnumDevices=4, GetDeviceState=9, GetDeviceData=10,
    SetDataFormat=11, SetCooperativeLevel=13, Poll=25) y constantes `DI_OK`/`DIERR_*`/GUID/IID con los
    valores de `dinput.h` (static_assert).

## Diseño (bajo nivel)

- `DirectInput` es dueño de los `Device` creados; `Device` referencia a `DirectInput` para refrescar el
  snapshot (un solo `InputSource`). Sin threads.
- El core DirectInput genérico aplica `MapperConfig` explícito a `pads[i]` → 256 bytes DIK. Sus mapas
  arrancan vacíos; no hay un preset implícito por pad.
- El backend real (Vita) implementa `InputSource::poll` con `sceCtrlPeekBufferPositiveExt2` +
  `sceTouchPeek`; mapeo botón→DIK y defaults son config del juego, no código del shim.
- ABI eventual (documentado, no implementado acá): `DirectInputCreateA` = 4 args `stdcall`;
  los métodos COM son `stdcall` con `this` primero, igual que `ds_emul.cpp`: `Shim{argc = 1 + nparams,
  stdcall_cleanup=true}`, vtables JS como tabla de `Bridge::shim_trap`. Integración = HW-006.

## No-objetivos

- `DirectInput8*`, `DirectInputCreateW`, force feedback/efectos, action maps, `EnumObjects`,
  `GetObjectInfo`, `SetEventNotification`, `GetDeviceData` con buffer, mouse por mensajes Win32,
  backend Vita real (`sceCtrl`/`sceTouch`), archivo de config de usuario.

## Riesgos / preguntas abiertas

- El orden direccional de J2 (`T G F H`) no está confirmado (sólo el conjunto de teclas). Es
  configurable; confirmar con HW-012 o con `settings.dat` del juego.
- Si `inpInit` no tolera que `CreateDevice` falle, el shim debe devolver un dispositivo "vacío"
  (todos los ejes centrados) antes que error; el test cubre el camino nominal.

## Evidencia

- `[estático]` imports: `docs/re-engine.md` §2/§5/§169; `docs/gap-list.md` §DINPUT.
- `[estático]` constantes/structs/vtable order: MinGW `dinput.h` local y patrón `ds_emul.cpp`.
- Resto: test host (esta spec). Wine/Vita3K quedan para HW-006/HW-007.

---

# SPEC-007b — InputSource Vita de DINPUT (HW-022)

- Feature: HW-022 · Milestone: M4 · Dueño: `deepseek` · Estado: borrador
- Depende de: HW-016 (core portable), HW-020 (`hwvita.ini`). No toca el backend de audio.

## Problema

HW-016 dejó `InputSource` abstracto. En Vita hay que alimentarlo con `sceCtrl`
(`sceCtrlPeekBufferPositiveExt2`, modo `SCE_CTRL_MODE_ANALOG_WIDE`) y `sceTouchPeek`
(frontal), y respetar el mapa de botones y la zona muerta de `hwvita.ini` (HW-020).

## Contrato observable

`src/dinput/vita_input.{h,cpp}`. La **lógica de mapeo es pura** (host-testeable); el acceso a
hardware queda detrás de `#ifdef __vita__`.

- `fit_rect(srcW,srcH,dstW,dstH,stretch)` idéntico a `present_scale.cpp` (winx86). Para
  640×480→960×544 da `{x=117, y=0, w=725, h=544}` (barras laterales de 117 px).
- `touch_raw_to_display(x,y,maxX,maxY)` normaliza el digitizer (activo hasta `maxX/maxY`) a
  960×544.
- `touch_display_to_game(dx,dy)` mapea display→juego con el `fit_rect`: `gx=(dx-117)*640/725`,
  `gy=dy*480/544`, recortando a `[0,639]×[0,479]` (fuera del rect → borde).
- `axis_from_raw(raw,deadzone)`: `0..255` (centro 128) → `-32767..32767`; `|v|<=deadzone` → `0`;
  más allá se reescala a fondo de escala. El joystick DInput (HW-016) usa estos ejes.
- `buttons_to_pad(SCE_CTRL_*)`: `SELECT→Select`, `START→Start`, `UP/RIGHT/DOWN/LEFT→d-pad`,
  `LTRIGGER→L2`, `RTRIGGER→R2`, `L1→L`, `R1→R`, `TRIANGLE/CIRCLE/CROSS/SQUARE→` caras.
- `VitaInputSource : InputSource` recibe un `MapperConfig` ya volcado desde `hwvita.ini`
  (`hw::io::apply_to`, HW-020). `poll()` arma `InputSnapshot`:
  - `pads[0]`/`pads[1]` desde puertos 0/2 de `sceCtrl ext2`;
  - touch frontal → `mouse_buttons bit0` + `mouse_dx/dy` = delta de la posición de juego entre
    muestras (primera muestra: delta 0), para navegar menús.
  - En host, `set_raw()` inyecta `RawPad`/`RawTouch`; en Vita, `poll()` llama al hardware.
- Formato de `hwvita.ini`: **sin cambios**; se consume `HwVitaConfig`/`apply_to` de HW-020.

## Criterios de aceptación (test host `hotwheels/tests/test_dinput_vita.cpp`)

1. **CA1** — `fit_rect(640,480,960,544,false)` = `{117,0,725,544}`; `stretch` = `{0,0,960,544}`.
2. **CA2** — zona muerta: `axis_from_raw(128,dz)=0`; `raw=128±(dz/256)` → `0`; `255→32767`,
   `0→-32767`; `dz=0` no recorta.
3. **CA3** — `touch_display_to_game`: centro `(480,272)→(320,240)`; borde izq. `x=117→0`;
   barra `x<117→0`; borde der. `x=842→639`; abajo `y=543→479`.
4. **CA4** — `touch_raw_to_display(0,0,maxX,maxY)=0` y `(maxX,maxY)→960×544` recortado a la pantalla.
5. **CA5** — `buttons_to_pad`: d-pad y caras producen los bits `kPad*` correctos.
6. **CA6** — `VitaInputSource` + `IDirectInputDevice::GetDeviceState` de teclado: con `pad0` (cross)
   y `MapperConfig` default → `DIK_RETURN`; con `pad1` (cross) → también `DIK_LSHIFT`;
   `pad_count` = 2 cuando el puerto 2 responde.
7. **CA7** — touch: primera muestra activa → `mouse_buttons bit0` y delta 0; segunda con touch
   desplazado → delta = diferencia de coords de juego.
8. **CA8** — mapa desde `hwvita.ini`: `hw::io::parse("[buttons.pad0]\nup=SPACE\n")` +
   `apply_to(MapperConfig)` hace que `up` del pad0 emita `DIK_SPACE`; `stick_deadzone` del ini
   llega a `axis_from_raw`.

## No-objetivos

- Touch trasero, giroscopio (`sceMotion`), rumble; remapeo en caliente; detección de panel por
  `sceTouchGetPanelInfo` (se usa `maxX/maxY` inyectables; el default es el rango típico).
- El registro con el Bridge (HW-006) y el present/render (HW-013/HW-018).

## Riesgos

- Rango real del digitizer: depende del panel; se usa `maxX/maxY` inyectables (default 1919/1087)
  y se puede ajustar con `sceTouchGetPanelInfo` en la integración.
- `hwvita.ini` puede remapear `up` etc.; el test CA8 fija el mecanismo, no los defaults.

## Evidencia

- `[estático]` `fit_rect` y 725×544: `docs/ports-patterns.md` §1/§3; `sceCtrl`/`sceTouch`:
  `shared/vitasdk/arm-vita-eabi/include/psp2{,common}/{ctrl,touch}.h`.
- Host: `bash hotwheels/tests/run_dinput_vita.sh`.
- `-fsyntax-only` del glue Vita con vitasdk (patrón HW-020).

---

# SPEC-007c — Controles por defecto 100% con botones físicos (HW-024)

- Feature: HW-024 · Milestone: M4 · Dueño: `deepseek` · Estado: borrador
- Depende de: HW-016/HW-022. Requisito duro del usuario: el juego debe ser **jugable completo con
  botones físicos**, sin requerir touch ni teclado.

## Relevamiento de teclas que usa el juego

Fuentes: `extracted/iso/MANUAL.TXT` §Keyboard Controls (autoritativo), `docs/re-engine.md` §5/§169,
strings de `HotWheels.exe` (tabla de nombres de tecla configurable).

| Contexto | Teclas del juego | Fuente |
|---|---|---|
| Menús | flechas + `Enter` + `Backspace` (o mouse) | MANUAL §"Within the menus…" |
| Pausa / salir al menú | `Esc` | MANUAL §"Press Esc during the game…" |
| Carrera J1 | ↑ acelerar · ↓ frenar/reversa · ←/→ doblar · `RShift` turbo · `RCtrl` espejo | MANUAL §Keyboard Controls |
| Carrera J2 | `T` acelerar · `G` frenar · `F` izq · `H` der · `LShift` turbo · `LCtrl` espejo | MANUAL §Two Player |
| Mouse (opcional) | izq acelerar · der frenar · 3.º espejo · mover doblar | MANUAL §Mouse Control |
| Config de teclas | tabla de nombres: `ESCAPE`, `BACKSPACE`, `TAB`, `SPACE`, `PAUSE`, `HOME/END`, `INSERT/DELETE`, `NUMPAD*`, setas | strings del exe (0x45d… tabla de nombres) |

**Entrada de texto / nombre de jugador:** el manual no pide nombre y el exe no tiene strings tipo
"enter your name"/"initials"; `highscore.dat` se lee sin prompt. **No hace falta IME ni selector.**
Los cheats tipeados (`allcars`, `nocars`, …) son teclado-only y quedan **fuera** del alcance
(opcionales, no bloquean jugar).

## Mapa por defecto (sin touch ni teclado)

Se implementa como `ButtonMap::for_pad(0)` (HW-016), con `pad0 = puerto 0` de `sceCtrl` (HW-022).

| Botón Vita | DIK emitido | Acción en el juego |
|---|---|---|
| D-pad ↑ / L-stick ↑ | `UP` | Menú: arriba · Carrera: acelerar |
| D-pad ↓ / L-stick ↓ | `DOWN` | Menú: abajo · Carrera: frenar/reversa |
| D-pad ← / L-stick ← | `LEFT` | Menú: izquierda · Carrera: doblar izq |
| D-pad → / L-stick → | `RIGHT` | Menú: derecha · Carrera: doblar der |
| Cross / Triangle | `RETURN` | Menú: confirmar/seleccionar |
| Circle / Square | `BACK` | Menú: volver atrás |
| Start | `ESCAPE` | Pausa / volver al menú principal |
| L1 / L2 | `RSHIFT` | Turbo (J1) |
| R1 / R2 | `RCONTROL` | Espejo retrovisor (J1) |
| Select | `PAUSE` | Reservado (el juego usa `Esc`) |

Cobertura: **menús** (flechas+Enter+Backspace), **carrera** (flechas+Turbo+Espejo) y **pausa**
(Esc). No se requiere touch ni teclado. El touch→mouse de HW-022 queda **opcional** (comodidad),
nunca requisito. `pad1` (2 jugadores) sigue mapeado por completitud (`T/G/F/H`+`LShift`/`LCtrl`),
pero evaluar 2P local no es objetivo.

## Criterios de aceptación

- **CA13** (HW-024) — el perfil Hot Wheels mapea pad0 d-pad/stick→flechas, Cross/Triangle→`RETURN`,
  Circle/Square→`BACK`, Start→`ESCAPE`, L1/L2→`RSHIFT`, R1/R2→`RCONTROL`; pad1 mapea
  ↑/↓/←/→→`T/G/F/H` y Cross/Circle→`LSHIFT`/`LCONTROL`. → test: `tests/test_io.cpp` CA11.
- **CA14** (BUG-004) — pulsar **cada** botón físico (`Start/Select/L1/L2/R1/R2`) en
  `InputSnapshot` produce su DIK en `GetDeviceState` (256 B): `ESCAPE`/`PAUSE`/`RSHIFT`/`RCONTROL`.
  → test: `tests/test_dinput.cpp` CA14.
- Los CA5/CA10 (HW-016) y CA6 (HW-022) se actualizan a los nuevos defaults (`RETURN` en vez de
  `RSHIFT` para Cross).

## No-objetivos

- Cheats por texto, navegación de teclado real, 2 jugadores local, remapeo en caliente (se sigue
  pudiendo por `hwvita.ini`).

## Evidencia

- `[estático]` MANUAL.TXT y strings del exe (arriba). `[host]` CA13.
