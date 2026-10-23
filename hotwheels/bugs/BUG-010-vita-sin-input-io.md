# BUG-010 — módulos de input/IO de Vita no llegan al VPK; el runtime usa EmptyInput

Nivel: **estático** (lectura de `tools/build_vita.sh`, `src/runtime_vita.cpp`, `src/host/integrate.cpp`;
verificado con `arm-vita-eabi-g++ -fsyntax-only`, OK). Dueño fix: integ/impl.
Estado: **resuelto en build/runtime Vita** (2026-10-03); validación de controles físicos en consola pendiente. Severidad: mayor (no rompe host/qemu; afectaba el entregable en consola).
Features: HW-020, HW-022, HW-024 (y por arrastre HW-007/HW-026 en consola).

## Síntoma esperado (aún no medido en consola: nunca se llegó a esa etapa)
En Vita el juego recibiría input siempre en cero (`GetDeviceState` → 0) y no existirían el selector
de arranque ni `hwvita.ini`, porque el código que los implementa no se compila ni se instancia.

## Evidencia
1. **Fuera del build Vita**: `tools/build_vita.sh:20-24` lista
   `src/dinput/dinput.cpp`, `src/integration/*.cpp`, `src/audio/*.cpp`, `src/winmm/*.cpp`, … pero
   **no** `src/dinput/vita_input.cpp` ni **ningún** `src/io/*.cpp`
   (`paths`, `read_cache`, `config`, `boot_menu`, `boot_selector`).
   Todos compilan para Vita: `arm-vita-eabi-g++ -std=gnu++17 -D__vita__ -fsyntax-only` → OK en los 6 TUs
   (verificado por mimo, 2026-10-03).
2. **Sin InputSource en el runtime**: `src/runtime_vita.cpp:144-149` arma `cfg` (`vita=true`,
   audio, roots, present) pero **no setea `cfg.input`**; `src/host/integrate.cpp:227-231` hace
   `cfg.input ? *cfg.input : input` con `EmptyInput` (devuelve snapshot vacío). El único que setea
   `cfg.input` es `src/host/host_main.cpp:117` (input scriptado, sólo para qemu/host).
3. **El glue del INI no se carga**: `src/integration/dinput_bridge.cpp:23` construye `DirectInput`
   con `MapperConfig` default; nadie llama `hw::io::load()` → los overrides de `hwvita.ini`
   (mapa de botones, escalado, deadzone) no llegan a `GetDeviceState`. El test CA8 lo hace a mano
   (`tests/test_dinput_vita.cpp:111`).
4. Ningún caller de `vita_roots`/`mount`/`boot_should_open`/`is_writable_game_file` fuera de tests
   (integración de caché en `ReadFile` es handoff a `re`, `coordination/re.md:24-30` — eso es
   no-objetivo de HW-020 y **no** forma parte de este bug).

## Fix sugerido
- Agregar `src/dinput/vita_input.cpp` y los `src/io/*.cpp` a `SRCS` de `tools/build_vita.sh`.
- En `runtime_vita.cpp`: construir `VitaInputSource` con `MapperConfig` cargado de
  `ux0:data/hotwheels/hwvita.ini` (con default si falta) y pasarlo en `cfg.input`; abrir el
  selector con `boot_should_open()`/`run_boot_selector()` cuando corresponda.
- Regresión: además de los runners host, `-fsyntax-only` de los TUs Vita en algún runner
  (hoy no existe) para que un TU nuevo que no entra al build no pase inadvertido.

## Relación con bugs previos
- **BUG-004** (cerrado, `61ec373`): era el mapa dentro del core portable; este bug es el cableado
  hacia el dispositivo. No se superponen.

## Evidencia adicional Vita3K — render, 2026-10-03
- Antes del script: runtime dejaba `cfg.input=null` y el juego permanecía en el menú, sólo DDraw P8.
- Añadí modo de validación opcional `HW_INPUT_SCRIPT` leído desde `env.txt`; Vita3K lo cargó y `race_enter.script` alcanzó la carrera. En run `015009` se observan D3D `CreateDevice` S_OK, 735 Draw en escena 100 y frame 300 presentado por vitaGL. Esto no cierra el bug: el input físico `VitaInputSource` y el build de IO/config continúan ausentes del runtime de producción.
- Para esta corrida tuve que stagear `DATA` bajo nombres minúsculos en `ux0:data/hotwheels/data/`; Vita3K sobre Linux no resolvía `DATA/WAVE` como `data/wave`. El build de IO case-insensitive sigue siendo parte de la corrección pendiente.

## Resolución de integración — integ, 2026-10-03
- `runtime_vita.cpp` ahora carga `hwvita.ini` desde el data root, abre el selector cuando `HW_CFG` o L lo solicita, aplica el mapa/deadzone y pasa `VitaInputSource` a `install_all()`. `HW_INPUT_SCRIPT` conserva prioridad cuando se solicita explícitamente para stage de validación.
- `build_vita.sh` compila `dinput/vita_input.cpp` y todos los TUs `src/io/*.cpp`; el runtime monta game/CD y save según `HwVitaConfig` y crea el directorio de saves.
- Corregí el selector físico: conserva la pulsación capturada mientras espera la liberación, así UP/DOWN/LEFT/RIGHT/CROSS/CIRCLE se procesan con el estado correcto.
- Medición: `tools/build_vita.sh` PASS (VPK 7.960.175 bytes); `tests/run_dinput_vita.sh` PASS CA1–CA8; `tests/run_io.sh` PASS CA1–CA12. Review focalizada aprobada (`review_bug010`). No ejecuté el VPK con controles físicos en consola.
