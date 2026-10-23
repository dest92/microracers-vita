# SPEC-046 — Oráculo de render por pista (BUG-047)
- Feature: HW-067 · Milestone: M8 · Dueño: opus-gfx · Estado: implementada (CA1–CA5 verdes; ver docs/bug047-oraculo.md)

## Problema
En consola real la pista de la playa (WAVE) se ve bien y **todas las demás** muestran polígonos que saltan y texturas
incorrectas (BUG-047, reporte del usuario). No hay forma reproducible de entrar a una pista que no sea la por defecto
(`race_enter.script`) ni de comparar la misma pista entre la referencia x86 (Wine) y nuestros backends, así que no se
sabe si la falla está en el backend GXP/VitaGL, en los shims D3D/DDraw (ring, caché de texturas, pipeline) o en el
runtime x86 (dynarec/x87). Sin esa clasificación HW-068 (sol-gfx) arreglaría a ciegas.

## Contrato observable
1. **Selección de pista por `settings.dat`** (RE estático, `settingsLoad` 0x426bf0): `tools/race-d3d-track<N>.dat` =
   `race-d3d-settings.dat` con **línea 5** (0x4722bc, modo: 0 campeonato que fuerza la pista 0, 1 carrera simple) = 1,
   **línea 6** (0x4722c0, índice de pista del menú, 0-based) = N-1 y **línea 18**
   (short 0x4722e0, pistas desbloqueadas por progreso) = 2. Con la 18 en 0 el selector sólo habilita la playa y pisa la
   línea 6 (verificado en qemu: DIK_RIGHT en el selector y la línea 6 sola siguen abriendo WAVE). La navegación es la de
   `race_enter.script`. `state_raceLoad` 0x4111b0 intercambia 1↔2 antes de indexar la tabla 0x45ff5c
   {wave, slumber, office}.sfp. Identidad de la pista **verificada por I/O**, no por imagen: la corrida debe abrir
   `DATA\<MUNDO>\<MUNDO>.SFP` (WAVE/SLUMBER/OFFICE).
2. **Runners**: `tools/run_race.sh` acepta `HW_RACE_SCRIPT` (default `race_enter.script`, sin cambio de
   comportamiento). `tools/wine/wine_run.sh <segs> <out> [eventos]` corre la referencia x86 headless (Xvfb dentro del
   contenedor `vita3k-headless`, inyección XTest sin clicks, captura cada 2 s) sin tocar el escritorio ni dejar el
   `settings.dat` del prefijo cambiado.
3. **Estadística de ring por escena** (`src/d3d/ring_stats.h`, header-only; `HW_D3D_STATS=1` en host): por escena
   cerrada, una línea `[hw] ringstat ...` con draws, vértices, triángulos, rango x/y/z/rhw, vértices no finitos,
   vértices fuera de banda de guarda (|x| o |y| fuera de `[-W, 2W]×[-H, 2H]`), `rhw<=0`, `z∉[0,1]`, arista máxima en px,
   uploads de textura (w×h×fmt) y **n máximo de payload por op** (para C1/ring de 16 bits). Invariante: sólo lee el
   ring; no altera el replay.
4. **Clasificador** `tools/track_oracle.py`: agrega las líneas `ringstat` de una corrida (percentiles por pista) y
   compara pista vs playa; emite tabla Markdown pista × backend × toggle.
5. **Entregable**: `hotwheels/docs/bug047-oraculo.md` (tabla, capturas mínimas, causa(s) con nivel de evidencia) y
   casos concretos para sol-gfx en `coordination/sol-gfx.md`.

## Criterios de aceptación (cada uno ⇒ un test)
- [x] CA1 `race-d3d-track<N>.dat` difieren de `race-d3d-settings.dat` sólo en las líneas 5 (=1), 6 (=N-1) y 18 (=2) y
      tienen 18 líneas enteras → `tests/test_track_oracle.py`.
- [x] CA2 `ring_stats` sobre un ring sintético reporta exactamente: draws, verts, rangos, no finitos, fuera de guarda,
      rhw<=0, arista máx y n máx por op; ring vacío → ceros; ring con header truncado no lee fuera → `tests/test_ring_stats.cpp`.
- [x] CA3 `track_oracle.py` agrega líneas `ringstat` y marca una pista como anómala sólo si su tasa de vértices fuera
      de guarda / no finitos supera la de la playa por el umbral → `tests/test_track_oracle.py`.
- [x] CA1b identidad de pista por I/O: una corrida por pista (qemu con `HW_TRACE=CreateFileA`, Wine con
      `+file`/strace) abre `DATA\<MUNDO>\<MUNDO>.SFP` del mundo pedido y ningún otro `<MUNDO>.SFP` → extractor
      `tools/track_oracle.py identity` + `tests/test_track_oracle.py` (caso positivo y caso de pista equivocada).
- [x] CA4 (oráculo, manual-evidencia) **control negativo**: playa qemu/RefRaster vs playa Wine sin diferencias
      estructurales (misma geometría/texturas a ojo + ringstat sin anomalías). Si falla, el oráculo está mal.
- [x] CA5 (oráculo) cada pista no-playa clasificada en {backend GXP, shims D3D/DDraw, runtime x86, no reproduce} con
      evidencia (qemu/Vita3K/wine) y toggle de bisección cuando aplique.

## Diseño
- La identidad del frame es "mismo momento de carrera" aproximado (timing/RNG difieren): se compara **estructura**
  (geometría y texturas correctas), no píxeles. Para geometría, el ring es la verdad de nuestro lado: RefRaster y GXP
  consumen el **mismo** ring, así que ringstat anómalo ⇒ shims/runtime (backend descartado); ringstat sano + imagen GXP
  mala ⇒ backend.
- `ring_stats` es O(palabras del ring), sin asignaciones por vértice; se llama en el hilo que cierra la escena (host),
  sólo con `HW_D3D_STATS=1`.
- Wine: el prefijo `hotwheels/wineprefix` y el Wine de `shared/opt` se montan en el contenedor con el mismo path; el
  proceso corre con el uid del usuario.

## No-objetivos
- No arreglar BUG-047 (HW-068, sol-gfx). No diff píxel a píxel. No usar la consola (Fase 2 con el usuario).

## Riesgos / preguntas abiertas
- El mapeo índice de menú → mundo (swap 1↔2) se confirma por I/O en cada corrida, no se asume.
- En consola, el `settings.dat` del usuario tiene pistas desbloqueadas; para reproducir hay que reemplazarlo (y
  restaurarlo) o navegar a mano.

## Evidencia
estático (código), host (tests), qemu (RefRaster), wine (x86 de referencia), Vita3K (GXP). Consola: pendiente Fase 2.
