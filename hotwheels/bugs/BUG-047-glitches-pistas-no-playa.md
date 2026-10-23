# BUG-047 — Glitches gráficos graves en todas las pistas salvo la playa
- Severidad: mayor (injugable) · Estado: abierto · Feature: HW-067/HW-068 · Dueño: opus-gfx · Nivel: consola (reporte del usuario, 2026-10-21)
## Síntoma (exacto, logs)
Usuario jugando en consola real: la pista de la playa (primera) se ve bien; **todas las demás** muestran polígonos que "saltan"
(vértices/triángulos fuera de lugar), texturas incorrectas y glitches generalizados. Sin logs ni capturas todavía.
## Reproducción mínima
Selección de pista: `tools/race_enter.script` + `tools/race-d3d-track{1,2,3}.dat` (WAVE/OFFICE/SLUMBER; SPEC-046).
**Reproduce en Vita3K con `fps_mode=interp60` y la cámara en movimiento** (orquestador, 2026-10-21): SLUMBER +
`race_full.script` (acelerando), VPK V0, videos en `C:\Users\USER\Desktop\vita-capturas\video\`: con interp60 hay agujeros
negros y polígonos fuera de lugar por toda la pista; con `fps_mode=original`, en los mismos momentos, limpio. El oráculo
de SPEC-046 no lo veía porque comparaba grilla/cámara quieta y corría GXP en `original` (`docs/bug047-oraculo.md`). En
`original` no reproduce en qemu/RefRaster, Wine x86 ni Vita3K/GXP.
## Esperado vs observado
Esperado: misma imagen que Wine x86. Observado: geometría y texturas corruptas fuera de la playa.
## Hipótesis (sin verificar, refutables con el oráculo)
- Backend GXP/VitaGL: render state o formato de textura que sólo usan esas pistas (si RefRaster sale bien).
- Pipeline D3D/caché de texturas (HW-053/055/063): colisión FNV64, carry de estado, reuso de slot (si RefRaster sale bien y A/B sync lo arregla).
- Runtime x86 (dynarec/x87 HW-054, tcache HW-059): matemática de transformación errónea (si RefRaster también sale mal).
Clasificación HW-067 (2026-10-21): backend GXP y shims D3D/DDraw descartados en emulador (RefRaster y GXP correctos con
el mismo ring); runtime x86 descartado en qemu pero abierto en ARM real. Candidatas sólo-hardware: reuso del circular
pool/arrays de vértices con la GPU leyendo (precedente Vita3K PR #4248), vida útil o alloc de texturas, memoria de vitaGL.
Bisección en consola: SPEC-052.
## Causa raíz (sólo si verificada)
**Principal, verificada en emulador (Vita3K): interp60.** El consumidor de 60 Hz (`src/fps/interp60.cpp`) arma cada
frame intermedio emparejando cada draw del frame N con uno del N-1 por ordinal o por cercanía (radio 8) con la misma
firma (textura, primitiva, cantidad de vértices, viewport) e interpola XY. En movimiento, el motor reordena y cullea
draws, y en pistas con tiles repetidos el par es otro polígono: vértices a mitad de camino entre dos objetos (polígonos
fuera de lugar) y huecos donde el polígono real no se dibuja (agujeros negros). Es un error de correspondencia, no de
precisión. La playa se ve bien probablemente porque tiene menos geometría repetida con la misma firma (sin medir).
Origen en consola: `fps_mode=interp60` en `ux0:data/hotwheels/hwvita.ini`, elegido en el selector de arranque (L).
Pendiente: confirmar en consola que en `original` no quedan glitches (Fase 2, `docs/fase2-bug047.md`). Si quedan, siguen
las hipótesis sólo-hardware de arriba con el VPK de diagnóstico (SPEC-052).
## Fix + test de regresión
- Decisión del usuario: funcionar como el original. SPEC-056 / 8dc6031: el runtime corre siempre en `original`
  (`fps_mode` guardado se ignora con log `[hw] fps_mode=X ignorado`) y el selector ya no ofrece FPS; interp60 queda
  compilado pero inalcanzable (reversible). Tests: `tests/test_fps_runtime.cpp` (CA1), `tests/test_io.cpp` CA9/CA7.
- Verificado en Vita3K (2026-10-21): VPK 8dc6031 con ini `fps_mode=interp60` loguea `ignorado` / `efectivo=original` y las
  3 pistas manejando (`race_full.script`, 1 captura/s) salen limpias (`docs/bug047-oraculo.md`, actualización noche).
- Abierto hasta la confirmación en consola.

## Cierre en consola (2026-10-22)
VPK HEAD `b5e3fda` (modo original forzado, SPEC-056). Usuario: carrera completa en OFFICE sin glitches, primero por el camino
software (save con 3D apagado) y luego con D3D/GXP a 800×600 ("funcionó mil veces mejor"). Log: `fps_mode guardado=interp60
efectivo=original`. Causa raíz confirmada: interpolación interp60. **Cerrado.**
