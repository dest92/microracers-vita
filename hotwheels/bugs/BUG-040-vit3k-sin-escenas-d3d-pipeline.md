# BUG-040 — Vita3K inicia D3D6 pero no entrega escenas para medir el pipeline

## Evidencia

Dos ejecuciones Vita3K independientes del build con HW-053 duraron 20 y 30 s. Ambas registraron `D3D backend vitaGL listo`, `CreateDevice` exitoso y P8 presents, pero `d3d_scenes_closed=0` y `guest_d3d_frames=0`; por eso no prueban replay ni swap del consumidor.

En qemu ARM, el guion rápido produjo submits D3D, pero las escenas fueron vacías (draws=0). La ventana baseline original midió `guest_d3d_hz_milli=1315`, `consumer_hz_milli=1315`, `completed=22`, `dropped=0`, `tex_uploads_per_frame_milli=357`. Pipeline midió `guest_d3d_hz_milli=39695`, `consumer_hz_milli=1170`, `completed=20`, `submitted=470`, `dropped=449`, `tex_uploads_per_frame_milli=0`. Son runs de 25 s y no son workload gráfico comparable; pipeline sufrió saturación/drop en el raster de referencia.

El build Vita aislado usó stubs locales temporales para símbolos HW-051 ausentes, sin activar su perfilador. Build normal sigue bloqueado por esas referencias del runtime.

## Estado / siguiente acción

Bloquea aceptación integral de HW-053 en emulador; no demuestra defecto del pipeline. Correlacionar con integ/runtime para restaurar una ruta de carrera D3D con draws en Vita3K y qemu y repetir los dos modos. No se atribuye este síntoma al cache HW-055.

## Revalidación / cierre (2026-10-03)
Con `HW_INPUT_SCRIPT=ux0:data/hotwheels/race_enter.script` en `env.txt`, dos modos Vita3K sí entregan frames y draws: en `interp60`, escena 300 tuvo 719 draws; `original` cerró 206 frames en su ventana, sin timeout. El síntoma inicial no aparece con el guion de carrera activo; se cierra como problema de configuración/harness no reproducido, manteniendo la limitación del raster qemu vacía en el handoff.
