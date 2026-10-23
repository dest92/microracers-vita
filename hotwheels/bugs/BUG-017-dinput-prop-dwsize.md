# BUG-017 — DINPUT: `get_property`/`set_property` no validan `dwSize` del `DIPROPHEADER`

- Severidad: **menor** (defensa en profundidad; el bridge ya valida la longitud real del buffer).
- Estado: **cerrado** (`d521acb` corrige la redeclaración; `run_dinput.sh` CA1..CA14 PASS; re-review APRUEBA).
- Evidencia: host/estático (hallazgo 2 de `coordination/review-HW-016.md`).
- Feature: HW-016.

## Síntoma

`Device::set_property` / `get_property` (`src/dinput/dinput.cpp:250` y `:277`) usan el buffer sin
comprobar que el `dwSize` del `DIPROPHEADER` declare el tamaño del struct correspondiente
(≥ 16 header; 20 para `DIPROPDWORD` — `BUFFERSIZE`/`AXISMODE`/`DEADZONE`/`SATURATION`; 24 para
`DIPROPRANGE` — `RANGE`). Un caller que declare `dwSize = 0` con un buffer largo hace que el core
escriba según su propio supuesto de tamaño en vez de según lo declarado; uno con `dwSize` corto
puede leer/escribir fuera de lo que el caller reservó.

Hoy el camino real está cubierto: `src/integration/dinput_bridge.cpp` valida la longitud del
buffer (`len >= 16` y la necesaria según `prop`) antes de llegar al core. Por eso es menor y no
bloqueante.

## Fix sugerido

Al inicio de `get_property`/`set_property`: leer `dwSize` (primer dword del header) y exigir
`dwSize >=` el tamaño mínimo para `prop` (16/20/24) o devolver `DIERR_INVALIDPARAM`.

**Atención a los tests**: `tests/test_dinput.cpp:117-120` pasa buffers en ceros
(`uint8_t range[24] = {}` → `dwSize = 0`) y espera `DI_OK`; hay que escribir el `dwSize` correcto
ahí y en cualquier otro caso equivalente antes de activar la validación, o los CA fallarán.

## Relación con bugs previos

- **BUG-013** (cerrado): era la aridad stdcall de la vtable (`EnumDevices`); se superponen sólo
  en que ambos protegen el contrato del puente DINPUT.

## Corrección

`Device::get_property` y `set_property` ahora exigen `dwSize` mínimo por propiedad (24 B para
RANGE; 20 B para DEADZONE/SATURATION y setters de BUFFERSIZE/AXISMODE) antes de leer/escribir.
La regresión pone tamaños válidos en los casos existentes y comprueba rechazo de RANGE con 20 B
y DWORD con 16 B, tanto en get como set. `tests/run_dinput.sh` pasa CA1..CA14.
