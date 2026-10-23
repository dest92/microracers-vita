# SPEC-047 — Regresiones y fixes de glitches por pista
- Feature: HW-068 · Milestone: M8 · Dueño: sol-gfx · Estado: aprobada (C1, rev-gfx; oráculo por pista pendiente)

## Problema
BUG-047: todas las pistas salvo playa presentan glitches en consola según usuario. HW-067 provee el oráculo para atribuir causas. Auditoría C1 independiente: `Ring::put_patchable` codifica `n << 16`; un upload ARGB8888 256×256 tiene 65.544 palabras y excede el campo de 16 bits. El lector interpreta píxeles como comandos, pierde textura y geometría posterior.

## Contrato observable
Cada comando conserva op, longitud y payload completos, inclusive n≥65.535. Los comandos pequeños conservan su representación. El estado objetivo exige que todos los lectores (replay, cola/carry e interp60) compartan el decodificador y respeten los límites del buffer. Un header extendido truncado no produce callbacks ni lecturas fuera del vector.

## Criterios de aceptación
- [x] CA1: roundtrip de tamaños 0, 65.534, 65.535 y 65.536, comando posterior intacto; puntero patchable apunta al payload. Test `test_d3d_ring_large.cpp`.
- [x] CA2: Device→Ring→Replayer→RefRaster carga textura 256×256 ARGB8888 y 512×256 RGB565; pixel esperado y Present ejecutado. Mismo test.
- [x] CA3: interp60 captura y reproduce frame con upload grande y draw grande sin cambiar payload ni offsets XY. Mismo test y suite interp60.
- [x] CA4: carry de un upload grande desde READY descartado conserva textura en Draw superviviente. Mismo test y suite frame_queue.
- [ ] CA5: tests D3D/bridge/interp60 verdes, build Vita y validación por pista con oráculo HW-067; ningún cierre de BUG-047 sin evidencia por pista.

## Diseño (bajo nivel)
El ring es memoria host propiedad exclusiva del productor hasta publish; lease impide reuso durante consumo y la cola sincroniza publicación. No cambian prioridades, afinidad, GIL, locks ni orden de memoria ARMv7. El snapshot de texturas conserva bytes guest antes de ceder el slot. No se modifica CPU guest, ABI PE32, caché I/D ni dynarec.

Se reserva longitud corta 0xFFFF como escape: header `(0xFFFF<<16)|op`, longitud u32 completa, payload. Los tamaños <0xFFFF siguen con un header; los grandes agregan una palabra (4 B por comando grande). Un decodificador común valida disponibilidad por resta antes de sumar y expone inicio del payload; interp60 calcula offsets desde ese inicio. `put_patchable` devuelve el payload, situado a +1 palabra para header corto o +2 para extendido. Los lectores manuales migrados son `validate_and_decode` y `build_interpolated`. No se fragmentan draws ni uploads, evitando modificar topología, identidad o generación de textura.

## No-objetivos
No agregar Z-buffer, perspectiva, indexed draws o estados sin evidencia de uso. No atribuir C1 a pistas específicas hasta medir. No consola real en Fase 1.

## Riesgos
Lectores manuales de headers en interp60 deben migrar juntos. El formato es interno/efímero, sin persistencia ni compatibilidad entre binarios. Un upload grande retenido por carry mantiene el costo de memoria previo más 4 B.

## Evidencia
Antes/después host determinista (conteo de comandos, bytes y píxeles); qemu cuando sea útil y build Vita. HW-067 registra Wine/RefRaster/Vita3K por pista; consola queda para Fase 2.

## Resultado parcial C1 (2026-10-21)
Spec `0ce7917` → test rojo `51eaf4e` (4 modos fallan) → fix `9da4281` (4 modos verdes). Tests host, ASan/UBSan y QEMU ARM en `hw-host-arm:24.04` PASS. CTest bridge/queue/interp60 3/3; golden D3D `0x6d85c1e26d7a5b3c` intacto. Build Vita PASS: 1.936.046 B, SHA256 fab6a3bd6204d18502daad7ed4428952066f4396a1421512358caef095a5a6c6. Review diseño/código rev-gfx APRUEBA (`coordination/review-HW068-c1.md`).

CA5 sigue abierta; no se atribuye C1 a BUG-047. Repartija del orquestador: sol-gfx ejecuta GXP/Vita3K (playa/control y pistas verificadas por HW-067); opus-gfx conserva qemu/RefRaster y Wine. Tabla/fases en `coordination/opus-gfx.md`, resultados GXP en `docs/bug047-gxp.md`; identidad inequívoca de pista requerida. Umbral estructural y escenas comparables lo define el oráculo SPEC-046 CA4/5 (no diff píxel exacto por RNG/timer). Sin consenso estructural playa/Wine el oráculo es inválido.
