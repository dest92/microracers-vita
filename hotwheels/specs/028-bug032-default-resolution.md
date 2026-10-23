# BUG-032 — resolución de carrera por defecto

## Problema y contrato

El log de consola `coordination` muestra que, después de la sonda D3D fija en 640×480, la carrera hace ~2.400 presents a 320×240. La RE del ejecutable identifica resolución como el segundo entero de `DATA/MENU/settings.dat`: 0=320×240, 1=640×480, 2=800×600. El shim expone 320/640/800 en `EnumDisplayModes`, acepta esos mismos tamaños en `SetDisplayMode`, y `GetAvailableVidMem` devuelve 8 MiB libres; este último sólo selecciona formato/paletizado de textura bajo 4 MiB, no resolución.

Al preparar datos para una instalación nueva, elegir 800×600 por defecto sin cambiar la opción 3D ni otras preferencias. Si `settings.dat` no existe, crear uno mínimo que deje la opción 3D en su valor inicial 0 y resolución en 2. Si existe en los datos fuente, preservar sus demás líneas y forzar únicamente la resolución a 2 durante el stage inicial. El juego sigue siendo dueño de los cambios posteriores al archivo.

## Aceptación

- Test sintético de `stage_data.sh`: con settings ausentes, el destino tiene líneas `0`, `2`; con opciones existentes, sólo la segunda línea cambia a `2`, resto idéntico.
- Re-stage con `settings.dat` de origen y un destino ya editado por el juego conserva los bytes del destino, incluidas opciones distintas de la resolución inicial.
- `run_all.sh` pasa suites existentes más el test nuevo.
- Vita3K: la entrada/fuente del stage no contiene `settings.dat`; el stage final debe generarlo como `0/2`. Apartar temporalmente y restaurar el save overlay de Vita3K (para que no prevalezca su ajuste previo); confirmar en log/captura que el menú permanece 640×480 y el primer present de carrera usa 800×600, sin crash. Esto sólo demuestra el default de instalación limpia, no el comportamiento con una partida existente ni instrumenta directamente los argumentos de `SetDisplayMode`.
- Dejar asentado que la evidencia de consola previa usa 320×240 y el build Vita3K no sustituye una nueva captura de consola.

## Razonamiento de bajo nivel

El ejecutable elige un índice de su tabla `ddFindMode` antes de llamar DirectDraw; `EnumDisplayModes` sólo alimenta la detección de capacidades y `SetDisplayMode` instala el modo solicitado. El costo de CPU no puede cambiar retroactivamente el índice elegido: el cambio observado ocurre durante setup, antes de los presents de carrera. La consulta de VRAM decide 8 bpp frente a formatos 16/32 bpp para texturas y no modifica el campo de resolución. Por lo tanto la corrección se aplica al archivo de preferencias inicial y no a los límites o retornos del shim DirectDraw.

El test usa un `HotWheels.exe` vacío como marcador y no incluye código ni datos del juego.

## No objetivos

No forzar 3D, alterar preferencias existentes distintas de resolución, cambiar `EnumDisplayModes`/`SetDisplayMode`, o atribuir rendimiento de Vita real a Vita3K.
