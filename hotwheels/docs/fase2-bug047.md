# Fase 2 — BUG-047 en consola (con el usuario) · SPEC-052 · opus-gfx

Objetivo: **confirmar en la Vita real que en modo original no hay glitches** en las pistas no-playa. La causa principal
encontrada es interp60: en Vita3K, con la cámara en movimiento, interp60 empareja polígonos equivocados (agujeros y
polígonos fuera de lugar), y con `original` la imagen sale limpia (BUG-047, SPEC-056). Desde 8dc6031 el runtime corre
**siempre** en original: el `fps_mode` del ini se ignora (el log dice `[hw] fps_mode=… ignorado`) y el selector ya no
ofrece FPS. Si en original quedan glitches, recién ahí se bisecta con los binarios y toggles de abajo, una variable por
corrida (`docs/bug047-oraculo.md`, SPEC-052).

## Qué hay para instalar
`hotwheels/out/diag-vpk/` (gitignored; SHA en `MANIFEST.txt`, una línea por variante):

| VPK | vitaGL | para qué |
|---|---|---|
| `hotwheels-diag-V0.vpk` | flags de producción (control) | reproducir; es el mismo juego que el VPK normal más lo que esté cableado de telemetría |
| `hotwheels-diag-V1.vpk` | + `SAFE_DRAW` + `SAFE_UNIFORMS` | ¿estado/draw de vitaGL? |
| `hotwheels-diag-V2.vpk` | + `NO_CIRCULAR_POOL` (buffers temporales sin reciclar) | ¿vida útil del circular pool de vértices? (hipótesis 1) |

Los tres usan el TitleID `HWMR00002`: instalar uno **reemplaza** al anterior (y al VPK normal); los datos de
`ux0:data/hotwheels/` se conservan. Al terminar, reinstalar el VPK normal.

**Antes de ir a la consola:** rehacer los tres VPK con `tools/build_diag_vpk.sh V0 V1 V2` con el árbol quieto y
verificar en `MANIFEST.txt` que las tres líneas tengan el mismo `repo=`, `tree_changed=0` y `patch_sha256` (el parche de
diagnóstico de vitaGL de cc-gfx). Una línea con `tree_changed=1` no es reproducible: rehacer.

## Preparación (una vez)
1. Respaldar en la PC (FTP/VitaShell) `ux0:data/hotwheels/hwvita.ini`, `ux0:data/hotwheels/env.txt` (si existe) y el
   `settings.dat` efectivo. El juego lee **primero** `ux0:data/hotwheels/save/data/menu/settings.dat` (overlay donde
   escribe; mayúsculas indistintas) y, si no existe, `ux0:data/hotwheels/DATA/MENU/settings.dat`.
2. `hwvita.ini`, sección `[display]`: `filter=game` en todas las corridas. Anotar el `fps_mode` que tenía el usuario
   (si era `interp60`, eso explica el reporte original). No hace falta cambiarlo: el VPK nuevo lo ignora; verificar en
   `hotwheels.log` la línea `fps_mode guardado=… efectivo=original`.
3. Elegir pista copiando a `ux0:data/hotwheels/save/data/menu/settings.dat` (crear carpetas si faltan):
   `tools/race-d3d-track1.dat` = WAVE (playa, control), `track2` = OFFICE, `track3` = SLUMBER. Fijan carrera simple,
   pista y 2 pistas desbloqueadas; la resolución es 800×600 (línea 2 = 2). Del menú se entra con Return/✕ hasta la
   carrera. El juego reescribe el archivo con lo elegido al salir del menú; re-copiarlo sólo al cambiar de pista.

## Toggles (archivo `ux0:data/hotwheels/env.txt`, líneas `CLAVE=VALOR`; sin archivo = default)
Estado verificado en el código (2026-10-21, cc-gfx HW-068): "sí" = tiene efecto; "pendiente" = se parsea pero todavía
no hace nada (no usarlo en consola hasta que figure como "sí").
| clave | efecto | estado |
|---|---|---|
| `HW_D3D_SYNC_BASELINE=1` | sin hilo consumidor ni pipeline de 3 slots | sí |
| `HW_VGL_RAM_MB=N` / `HW_VGL_VRAM_MB=N` | pools de vitaGL (default 24 / 64; rango 4–48 / 8–96) | sí |
| `HW_DIAG_GLFINISH=1` | `glFinish()` tras cada frame: la CPU espera a la GPU | sí |
| `HW_DIAG_TEXCACHE=0` | sin caché de texturas por contenido | sí |
| `HW_DIAG_TEXDEFER=N` | difiere los borrados de textura N presents | sí |
| `HW_DIAG_RINGSTAT=1` | `ring_*` en `[hw] diag`: ¿la geometría ya sale rota del runtime x86? | sí |
| `HW_DIAG_CANARY=1` | canario del slot reciclado del circular pool (`canary_bad`; sólo VPKs con el parche de vitaGL) | sí |
| `HW_DIAG_CIRC_MB=N` | tamaño del circular pool (default 2) | pendiente |
| `HW_DIAG_POISON=1` | rellena el slot reciclado (si la GPU lee tarde, el glitch cambia de forma) | pendiente |
La línea `[hw] diag` (cada 10 s y al salir) va siempre en `hotwheels.log`.

## Corridas (60 s manejando: acelerar, doblar, chocar un borde una vez)
Captura con PS+Start a ~20 s y ~40 s. Después de cada corrida, traer `ux0:data/hotwheels/hotwheels.log` (renombrarlo en
la PC como `<n>-<VPK>-<pista>-<toggle>.log`) y las capturas de `ux0:picture/SCREENSHOT/`. Cortar en cuanto un paso aísle
la causa.

| # | VPK | pista | env.txt | si hay glitch / si no |
|---|---|---|---|---|
| 0 | VPK normal (≥ 8dc6031) | SLUMBER y OFFICE, después la pista que el usuario vio peor | vacío | **la prueba principal**: manejando 60 s en cada una, sin glitches ⇒ BUG-047 cerrado. Si hay glitches, seguir |
| 1 | V0 | WAVE | vacío | control: debe verse bien. Si falla, el problema no es por pista: avisar |
| 2 | V0 | OFFICE | vacío | debe reproducir lo que quedó en la corrida 0. Si no: repetir con el `settings.dat`/ini originales del usuario y su pista (difiere resolución/modo) |
| 3 | V0 | OFFICE | `HW_DIAG_RINGSTAT=1` | `ring_bad` OFFICE ≫ WAVE ⇒ runtime x86 en ARM real: parar y avisar al orquestador |
| 4 | V0 | OFFICE | `HW_DIAG_GLFINISH=1` | si arregla ⇒ carrera CPU/GPU: seguir 5–7 para ubicar el recurso; si no ⇒ ir a 8 |
| 5 | **V2** | OFFICE | vacío | si arregla ⇒ vida útil del circular pool (precedente Vita3K PR #4248) |
| 5b | V0 | OFFICE | `HW_DIAG_CIRC_MB=16` (pendiente) | si arregla sólo esto ⇒ pool chico, no lifetime |
| 6 | V0 | OFFICE | `HW_DIAG_TEXDEFER=3`, luego `HW_DIAG_TEXCACHE=0` | si arregla ⇒ vida útil de texturas / caché |
| 7 | V0 | OFFICE | `HW_D3D_SYNC_BASELINE=1` | si arregla ⇒ pipeline / carry |
| 8 | **V1** | OFFICE | vacío | si arregla ⇒ estado/draw de vitaGL |
| 9 | V0 | OFFICE | `HW_VGL_RAM_MB=40` + `HW_VGL_VRAM_MB=96` | si arregla ⇒ memoria de vitaGL (mirar `vgl_*_free_min` en el log de la corrida 2 si hay B) |
Todas las corridas salvo la 5b se pueden hacer con los VPK actuales (ver `MANIFEST.txt`).

## Qué anotar por corrida
VPK (de `MANIFEST.txt`), pista, contenido exacto de `env.txt`, la línea `fps_mode guardado=… efectivo=…` del log, `filter`, glitch sí/no y en qué momento
(segundos, zona), tipo de glitch (spikes al infinito, triángulos que tiemblan, textura de otra pista, textura negra).
Si hay telemetría B: copiar las líneas `[hw] diag` y `[hw] diag-pool` de ese log.

## Cerrar
Reinstalar el VPK normal, restaurar `hwvita.ini`, `env.txt` (o borrarlo si no existía) y el `settings.dat` respaldado
(o borrar `save/data/menu/settings.dat` si antes no existía).
