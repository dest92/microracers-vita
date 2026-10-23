# HW-029 — Carrera D3D6 en Vita3K con GXP precompilados

## Problema

Vita3K no implementa los imports ShaccCg que vitaGL usa al compilar shaders FFP durante runtime (BUG-015). El renderer D3D6 debe dibujar la carrera con los GXP precompilados propios, sin pasar por compilación FFP.

## Contrato observable

- El stage contiene `hotwheels/HotWheels.exe`, `hotwheels/DATA/` y `hotwheels/env.txt`; el env fija `HW_AUDIO=0` y `HW_INPUT_SCRIPT=ux0:data/hotwheels/race_enter.script`. `race_enter.script` es el guion de HW-026. Vita selecciona D3D6 por el cableado del runtime; se verifica con `CreateDevice` y draws cuyo log informa `backend=vitaGL`. `HW_D3D=1` sólo aplica al runner QEMU. El exe y `DATA` locales no se ingresan al VPK ni al repositorio.
- `STAGE=<dir>` copia el contenido del directorio a `ux0:data/` en el perfil aislado de Vita3K; por eso el stage debe contener la carpeta `hotwheels/` como raíz. Los settings D3D `race-d3d-settings.dat` se usan en la reproducción qemu, no hacen falta en Vita.
- Vita3K inicia el juego, crea los targets D3D de 640×480 y 800×600 con `S_OK`, y registra actividad de escenas/draws en `backend=vitaGL`.
- Una captura después de 30 s muestra la carrera 3D activa (pista/HUD/temporizador), y un run de más de 30 s termina por el límite controlado del harness, sin SIGSEGV ni error de compilación ShaccCg.
- Los errores esperables de archivos opcionales ausentes se distinguen de crash; no cuentan como fallo gráfico.

## Validación reproducible

```sh
PATH="$PWD/shared/vitasdk/bin:$PATH" hotwheels/tools/build_vita.sh
STAGE=hotwheels/out/vita3k-d3d-20261003-014551/stage hotwheels/tools/vita3k_run.sh hotwheels/build-vita/hotwheels-vita.vpk 40
```

Verificar `run.stdout` para boot, `CreateDevice`, `D3D frame`, `D3D escena` y ausencia de `SIGSEGV`/errores ShaccCg; inspeccionar un `shot_*.png` tomado después del segundo 30.

## Límites

El run qemu `HW_D3D=1` es una validación independiente. En `out/run-20261003-042307`, el log limita el resumen de draws a escenas 0/1/2, pero el output tiene frames D3D 230–267; `frame_00267.png` muestra carrera, autos y HUD. BUG-012 continúa cerrado. La comparación con Vita3K valida recorridos separados, no identidad píxel a píxel de escenas reales.

El sample standalone FFP de HW-011 sigue fallando en Vita3K. La ruta GXP elimina esa dependencia para el juego; BUG-015 queda mitigado para el renderer integrado, no corregido en Vita3K.
