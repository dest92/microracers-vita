# BUG-047 — oráculo de render por pista (HW-067, SPEC-046) · opus-gfx · 2026-10-21

**Resultado: BUG-047 no se reproduce fuera de la consola.** Las tres pistas (WAVE playa, OFFICE, SLUMBER) se ven
estructuralmente iguales a la referencia x86 (Wine) en qemu/RefRaster, quietas y manejando. En Vita3K/GXP las tres
también coinciden (pata GXP: sol-gfx, `docs/bug047-gxp.md`). Como RefRaster y GXP consumen el **mismo ring**, la
geometría y las texturas que generan el runtime x86 y los shims D3D/DDraw son correctas al menos en qemu. Clasificación
por descarte: **causa sólo de hardware** (vida útil CPU→GPU de vértices/texturas o memoria de vitaGL, que Vita3K esconde
al correr la GPU casi sincrónica), o bien **runtime ARM real ≠ qemu**. Se separan en la Fase 2 con el VPK de diagnóstico
de **SPEC-052**.

## Cómo se entra a cada pista (RE estático, verificado por I/O)
El selector sólo habilita pistas desbloqueadas por progreso; en modo campeonato la pista es siempre la 0. Por eso los
pulsos `DIK_RIGHT` en el selector no cambiaban de pista (qemu: siguió abriendo `wave.sfp`). Receta:
`tools/race_enter.script` + `tools/race-d3d-track<N>.dat` (en qemu `HW_RACE_SETTINGS`, en Wine `HW_WINE_SETTINGS`, en
Vita3K/consola `ux0:data/hotwheels/DATA/MENU/settings.dat`, respaldando el original):

| línea de settings.dat | global | valor | fuente |
|---|---|---|---|
| 5 | 0x4722bc modo | 1 = carrera simple (0 = campeonato, fuerza pista 0) | `state_menuEnter` 0x42545e |
| 6 | 0x4722c0 pista (índice del menú, 0-based) | N-1 | `settingsLoad` 0x426bf0, `menuBindVar` 0x423fa0 |
| 18 | 0x4722e0 (short) pistas desbloqueadas | 2 | `settingsLoad` 0x426cd6, `state_menuEnter` 0x4254dd |

El menú ordena Wave, Office, Slumber; `state_raceLoad` 0x4111b0 intercambia 1↔2 antes de indexar la tabla 0x45ff5c
{wave, slumber, office}.sfp. Resultado: **track1 = WAVE, track2 = OFFICE, track3 = SLUMBER**.

## Tabla pista × backend × condición
Identidad: qemu `HW_TRACE=CreateFileA` → `<mundo>.sfp`; Wine inotify (`tools/wine/fwatch.c`) → `DATA/<MUNDO>/<MUNDO>.SFP`;
Vita3K `sceIoOpen` (sol-gfx). `tools/track_oracle.py identity` = OK en todas las filas.

| pista | backend | condición | identidad | resultado estructural | nivel |
|---|---|---|---|---|---|
| WAVE (control) | Wine x86 (wined3d/llvmpipe) | quieto, 0–60 s | ✔ (default) | referencia | wine |
| WAVE | qemu + RefRaster | quieto, 90 s | ✔ | = Wine (misma curva, conchas, arena, HUD) | qemu |
| WAVE | Vita3K GXP | grilla | ✔ | = Wine (sol-gfx) | Vita3K |
| OFFICE | Wine x86 | quieto y manejando | ✔ | referencia | wine |
| OFFICE | qemu + RefRaster | quieto y manejando (`race_full.script`, 150 s) | ✔ | = Wine: escritorio, riel rojo, caída al piso, sin polígonos sueltos | qemu |
| OFFICE | Vita3K GXP | grilla → fin 01:08 | ✔ | = Wine, sin glitches (sol-gfx) | Vita3K |
| SLUMBER | Wine x86 | manejando | ✔ | referencia | wine |
| SLUMBER | qemu + RefRaster | manejando, 150 s | ✔ | = Wine: largada, cubos, alfombra, historieta | qemu |
| SLUMBER | Vita3K GXP | carrera, HUD 00:28 | ✔ | = Wine: dormitorio, elefantes, caja, autos (sol-gfx, `docs/bug047-gxp.md`) | Vita3K |

Capturas mínimas (gitignored, sin assets en git): `out/bug047-oracle-evidence/{wave,office,slumber}-qemu-vs-wine.png`.
El momento exacto difiere (timing/RNG: en Wine los rivales empujan al auto), así que se compara estructura y no píxel.

### ringstat (qemu, ring D3D por escena, `HW_D3D_STATS=1`)
| corrida | escenas | vértices | no finitos x/y/u/v | fuera de guarda | n máx de payload | arista máx px |
|---|---|---|---|---|---|---|
| WAVE quieto | 92 | 238 808 | 0 | 16 | 32 776 (TexUpload 256² 16 bpp) | 2 218 |
| OFFICE quieto | 65 | 134 164 | 0 | 318 | 32 776 | 2 696 |
| OFFICE manejando | 102 | 80 878 | 0 | 1 330 | 32 776 | 3 122 |
| SLUMBER manejando | 179 | 90 664 | 0 | 829 | 32 776 | 2 688 |
- **Cero vértices no finitos** en todas; z/rhw: rhw = 1 constante (el motor transforma en x86 y manda XYZRHW en pantalla).
- "Fuera de guarda" sube en OFFICE/SLUMBER y manejando, pero la imagen coincide con Wine: son polígonos grandes de
  escenario legítimamente fuera de pantalla (el rasterizador los recorta). La marca `ANOMALA` de `track_oracle.py stats`
  queda como **indicador débil**, confundido por la cámara; no prueba corrupción. Su uso real es en consola (SPEC-052
  `ring_bad`): si ahí aparecen no finitos o saltos de arista con la misma escena, la geometría ya sale rota del runtime.
- n máx de payload = 32 776 < 65 535: el desborde del header de 16 bits del ring (caso C1, fix de sol-gfx en 9da4281) es
  **latente** en estas pistas. Todo draw tiene ≤ 5 462 vértices ⇒ tampoco se alcanza el límite U16 de `glDrawArrays` de
  vitaGL (`MAX_IDX_NUMBER` 0xC000, que si se pasa sólo loguea y dibuja igual).

## Hipótesis descartadas (con evidencia)
| hipótesis | veredicto | evidencia |
|---|---|---|
| Pistas no-playa leen archivos distintos/erróneos (VFS, mayúsculas, ReadStream) | descartada en qemu | mismos 7–8 archivos por mundo con el mismo layout; identidad OK; imagen = Wine. La precarga `ReadCache` (`preload=1`) es código muerto: no está cableada (`grep`) |
| Runtime x86 (dynarec/x87) en qemu calcula mal la transformación | descartada **en qemu** | ring sin no finitos, imagen = Wine manejando. **Queda abierta en ARM real** (dynarec idéntico pero distinto hardware/timing): SPEC-052 `ring_bad` |
| Shims D3D/DDraw (ring, caché FNV64, carry, pipeline) | descartada en qemu/Vita3K | RefRaster y GXP correctos con el mismo ring y pipeline |
| Backend GXP (render states, formatos, address modes) | descartada en Vita3K | OFFICE/SLUMBER GXP = Wine (sol-gfx) |
| Overflow del header de 16 bits del ring (C1) | latente | n máx 32 776 |

## Causa(s) candidata(s) y siguiente paso
Lo que Vita3K no modela: (1) reuso del circular pool de vitaGL o de los arrays del backend mientras la GPU todavía lee
(precedente exacto de "spikes + UV incorrectas": Vita3K PR #4248, ver `docs/research-ports-2026.md`); (2) borrado o
re-subida de texturas en vuelo, o alloc fallido silencioso con más páginas por pista; (3) runtime en ARM real.
→ **SPEC-052** (`specs/052-vpk-diagnostico-bug047.md`): VPK V0/V1/V2 con telemetría (`ring_bad`, `vglMemFree` por pool al
entrar a la pista, `circ_wraps`/overrun, canary del slot reciclado, `tex_del_inflight`, `gl_err`) y toggles por `env.txt`
(GLFINISH, SYNC_BASELINE, TEXCACHE=0, TEXDEFER, CIRC_MB, POISON) con protocolo de 9 pasos para la Fase 2 con el usuario.
Implementación: sol-gfx (HW-068).

## Reproducir
```bash
HW_RACE_SETTINGS=/work/hotwheels/tools/race-d3d-track2.dat HW_ENV="HW_TRACE=CreateFileA HW_D3D_STATS=1" \
  HW_RACE_SCRIPT=/work/hotwheels/tools/race_full.script HW_KEEP_OUT=1 tools/run_race.sh 150 d3d   # de a UNA qemu
python3 tools/track_oracle.py identity out/latest/stdout.txt OFFICE
python3 tools/track_oracle.py stats out/latest/run.log
HW_WINE_SETTINGS=tools/race-d3d-track2.dat tools/wine/wine_run.sh 58 out/wine-office \
  "10:Return:0.15,12.5:Return:0.15,15:Return:0.15,17.5:Return:0.15,20:Return:0.15,22:Up:6"
python3 tools/track_oracle.py identity out/wine-office/opens.log OFFICE
```
`HW_D3D_STATS` necesita el hook de `src/host/host_main.cpp` (cambio local sobre el WIP sin commitear de ese archivo;
ver `coordination/decisiones.md`).

## Actualización 2026-10-21 (noche): la causa en emulador era interp60 (SPEC-056)
El orquestador reprodujo BUG-047 en Vita3K **con `fps_mode=interp60` y la cámara en movimiento** (SLUMBER + `race_full.script`):
agujeros negros y polígonos fuera de lugar; con `original`, limpio. El oráculo anterior no lo veía porque comparaba grilla
o cámara quieta y corría GXP en `original`. Mecanismo, fix y tests: SPEC-056 / 8dc6031 (el runtime corre siempre en original).

### Barrido en movimiento, modo original (VPK `out/opus-sweep/hotwheels-8dc6031.vpk`, SHA 5cc7c01d…eadb, `race_full.script`, 95 s, 1 captura/s)
| pista | backend | ini | log | resultado en movimiento | evidencia |
|---|---|---|---|---|---|
| SLUMBER | Vita3K GXP | `fps_mode=interp60` | `fps_mode=interp60 ignorado` + `guardado=interp60 efectivo=original` (**SPEC-056 CA4**) | limpio: cocodrilo, historieta, trabado contra el peluche igual que Wine | `spec056-sweep-slumber-gxp.png` |
| OFFICE | Vita3K GXP | original | `efectivo=original` | limpio: escritorio, libros, archivero, cables, cae al piso y vuelve | `spec056-sweep-office-gxp.png` |
| WAVE | Vita3K GXP | original | `efectivo=original` | limpio: piedras, pelota, estrella | `spec056-sweep-wave-gxp.png` |
| WAVE | qemu RefRaster | (host, sin interp60) | identidad `wave.sfp`; ringstat 163 escenas, 0 no finitos | limpio salvo R1 | `r1-wave-wedge-qemu-vs-gxp.png` |
| OFFICE / SLUMBER | qemu RefRaster | (host) | ver tabla de arriba (manejando) | limpio | `{office,slumber}-qemu-vs-wine.png` |
Evidencia en `out/bug047-oracle-evidence/` (gitignored). Nivel: Vita3K y qemu.

### Residual R1 (cerrado más abajo: no es RefRaster, no reproducible)
qemu/RefRaster, WAVE manejando, HUD 00:04–00:07: el auto queda contra el borde de piedras y una cuña negra ocupa la esquina
inferior derecha durante 4 cuadros seguidos (estable, no titila). RefRaster dibuja todo lo que trae el ring (bordes de 64
bits, sin culling ni recorte), así que en esos cuadros **el juego no emitió** los polígonos del suelo cercano. No aparece en
GXP (Vita3K) ni en Wine en el mismo tramo, pero la cámara no está en la misma posición exacta en los tres (timing/física),
así que no es una comparación válida: puede ser el borde legítimo del terreno visto desde otra posición o un culling del
motor que difiere en el runtime host. Para clasificarlo hace falta reproducir la misma pose de cámara (guion determinista
y ring grabado). No bloquea BUG-047.

### R1 — clasificado y cerrado (2026-10-21, opus-gfx)
Método: hook local de `host_main` (`HW_D3D_DUMP_DIR`/`HW_D3D_DUMP_MAX`, sin commitear por el WIP ajeno del archivo) que
vuelca el ring crudo por escena, y `r1_analyze.cpp` (en `out/bug047-oracle-evidence/`). La herramienta re-reproduce las
escenas 0..N con el `Replayer` + `RefRaster` reales y, para la escena N, calcula la cobertura de los Draw con un test de
borde en `double`, independiente de RefRaster.
- **Fidelidad:** el replay offline reproduce los PNG de qemu byte a byte (`compare -metric AE` = 0 en las escenas 50 y 59 ↔
  frames 51 y 60, baseline sincrónica).
- **RefRaster descartado:** en 6 escenas de WAVE manejando (x ∈ [-295, 1056], y ∈ [-256, 813], hasta 2.344 triángulos), de
  480.000 píxeles quedan descubiertos 0–74 (grietas de 1 px de la malla del juego, también visibles en GXP), y los
  "cubiertos pero negros" son 0–19 (texels oscuros). RefRaster no pierde triángulos grandes ni fuera de pantalla.
- **Consecuencia:** en los cuadros de la cuña, el ring no traía el suelo cercano. El motor no emitió esos polígonos en esa
  pose de cámara.
- **No reproducible:** dos corridas más (baseline sincrónica, 60 escenas volcadas; pipeline, 124 escenas) con la misma
  receta: la física diverge entre corridas, el auto queda en otra posición y ninguna captura de carrera tiene la cuña.
- **Hipótesis más simple (sin verificar):** el motor (herencia PsyQ, `gpuDrawPacket`) descarta los polígonos que cruzan el
  plano cercano cuando la cámara queda pegada al suelo contra el borde. Eso sería comportamiento del juego original.
**Veredicto:** no es un artefacto de RefRaster ni hay evidencia de defecto en shims o runtime. Se cierra sin bug. Reabrir si en consola (Fase
2, modo original) o en GXP aparece una cuña negra cerca de la cámara: con el volcado de ring de esa pose se separa "el juego no la emitió" (comparar con Wine en la misma pose) de "la emitimos mal".
