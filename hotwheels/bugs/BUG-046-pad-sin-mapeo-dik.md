# BUG-046 — En consola el pad no llega al juego ("abre pero no funcionan los controles")

- **Estado:** corregido (test RED→GREEN). Falta confirmarlo en consola.
- **Síntoma:** con el build de HEAD en la Vita el juego arranca y presenta la intro/menú 2D sin parar, pero ningún botón responde.
  - No es un deadlock: el hb UDP mostró ddraw, interp60, audio y logsink vivos (edad ~0 ms) más allá de t=70 s.
- **Causa:** a829a43 (extraer DirectInput a winx86-dx) quitó el constructor de `MapperConfig` que cargaba el mapeo de Hot Wheels (`ButtonMap::for_pad`). Ahora el core deja `ButtonMap` vacío.
  - `dinput_bridge` construía `DirectInput di(waypoints)` con `MapperConfig{}`, así que `owner_.config().pad[p]` no tenía teclas y ningún botón se traducía a DIK.
  - El `mapper` de hwvita.ini sólo llegaba a `VitaInputSource` (deadzone), no al `DirectInput`.
  - Los tests no lo detectaban: inyectaban `keys[]` directo y nunca probaban pad→DIK.
- **Fix:**
  - `hw::dinput::hotwheels_mapper()` (en src/dinput/dinput.cpp, que entra en los builds host y Vita);
  - `install_dinput(..., cfg)` con ese default;
  - `integrate::Config::mapper`, que runtime_vita apunta al mapper de hwvita.ini;
  - config.cpp reutiliza el preset.
- **Test:** tests/test_dinput_bridge.cpp: Cross en pad0 → DIK_RETURN en GetDeviceState. RED antes del fix, GREEN después; ctest 23/23.
