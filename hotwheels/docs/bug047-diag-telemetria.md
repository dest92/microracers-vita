# BUG-047 — telemetría del VPK de diagnóstico (SPEC-052, HW-068)

Qué mide la línea `[hw] diag`, cómo se produce y qué dio la primera corrida real.
Contrato: `specs/052-vpk-diagnostico-bug047.md` (opus-gfx) · anexo de implementación:
`specs/052-vpk-diagnostico-implementacion.md` (este repo).

## 1. La línea

Una línea cada 10 s y una al salir, en `ux0:data/hotwheels/hotwheels.log`:

```
[hw] diag context=<ctx> window_ms=10000 ring_bad=4 ring_verts=232071 ring_maxn=32776 ring_edge=2156.2 \
  vgl_ram_free_min=0 vgl_vram_free_min=0 circ_peak=0 circ_overrun=0 circ_wraps=1022270 circ_slot=1 \
  circ_off=105536 vgl_alloc_fail=0 gl_err=0 tex_live=25 tex_bytes=6553600 tex_up=25 \
  tex_del_inflight=0 tex_up_inflight=0 q_dropped=0 q_carried=0 canary_bad=544 \
  pool_enter_ram=0 pool_enter_vram=0
```

- Los contadores son **acumulados desde el arranque** (el hilo que reporta no los reinicia: así no hay
  carrera con el hilo consumer). El usuario saca los deltas entre dos líneas.
- `ring_*` sólo se llenan con `HW_DIAG_RINGSTAT=1` (muestreo 1 de cada 4 escenas; el resto son
  contadores enteros baratos).
- `circ_*`, `vgl_alloc_fail` y `canary_bad` vienen del **parche de vitaGL**
  (`patches/vitagl-c707ff00-diag.patch`), que sólo existe en los VPK de diagnóstico (V0/V1/V2).
  En el VPK normal (contra el SDK) salen en 0: los símbolos son *weak* y no están.
- `ring_edge` va en punto fijo ×10 (mismo decimal que se imprime).

## 2. VPK de diagnóstico

`hotwheels/tools/build_diag_vpk.sh V0 V1 V2` clona vitaGL `c707ff00` en `out/diag-vgl-<V>/`, aplica el
parche, compila con los flags de la variante y enlaza ese `libvitaGL.a` (`HW_VITAGL_LIBDIR`).
Manifiesto con SHA de VPK, de lib, del parche y de los flags: `hotwheels/out/diag-vpk/MANIFEST.txt`.

| variante | flags | SHA256 del VPK |
|---|---|---|
| V0 | producción | `5d617a348aec5c19f9a9e3dd9e82bedec550829ab62c7ab9fd08477bb9b3b807` |
| V1 | +`SAFE_DRAW` `SAFE_UNIFORMS` | `78a30f264b02b048e6f817a4e023fc4947be067e4b8294526214b1a45a0e79d8` |
| V2 | +`NO_CIRCULAR_POOL` | `5b8d1f4b9bcf2daaad7118216bfd8cbccb4c191ebd74640c9ec03c561a92e0a4` |

patch `5102ea7de13e090883966429e3b89e1e0629e19dd0749d01d92777b66970e802` · vidaGL `c707ff00`.

## 3. Qué mide el canario (lo importante)

En `c707ff00` la porción del pool circular **rota y su puntero se resetea antes** de que exista la
fence del frame que la usó. El canario sella el checksum del área usada en el `scene_end` (el submit:
ahí nace la fence) y lo vuelve a calcular cuando la porción se recicla:

- si el checksum **cambió**, alguien escribió en el área mientras la GPU todavía podía leerla ⇒
  familia (1) de BUG-047 ("polígonos que saltan");
- no demuestra que la GPU haya dejado de leer: sólo que el área cambió.

## 4. Primera corrida real (Vita3K, V0, pista WAVE)

**El canario detecto escrituras en vuelo:**

| pierna | ring_verts | circ_wraps | canary_bad |
|---|---|---|---|
| A | 232.071 | 1.022.270 | **544** |
| A | 185.311 | 807.530 | **473** |
| A | 224.798 | 941.058 | **559** |

(3 corridas con `HW_DIAG_RINGSTAT=1 HW_DIAG_CANARY=1`; las tres con el mismo VPK y la misma pista.)
Esto es el primer detector **directo** de la familia (1): el área del pool circular se modifica mientras
la GPU puede estar leyéndola. En la consola real puede ser más frecuente (la GPU va más lenta).

Ojo: `canary_bad` **no** es "la GPU leyó basura": es "el área cambió entre el submit y el reciclado".

## 5. Costo de la telemetría (A/B de CA5)

Vita3K dibuja por software ⇒ la métrica es el **guest Hz** (no el consumer fps). Piernas intercaladas
A (telemetría cara: `HW_DIAG_RINGSTAT=1` + `HW_DIAG_CANARY=1`) / B (sin toggles), misma pista (WAVE),
mismo VPK, 3 por lado. Guest Hz = mediana de las ventanas de 10 s con la carrera en curso (>5 Hz).

| lado | piernas (guest Hz, mHz) | media | ruido interno |
|---|---|---|---|
| A | 30852 / 31250 / 32840 | 31647 | 1988 (6,3 %) |
| B | 29556 / 30440 / 34030 | 31342 | 4474 (14,3 %) |

Diferencia entre lados: **305 mHz = 0,97 %** ⇒ ≤ 2 % **y** dentro del ruido entre corridas iguales
(6,3 % en A, 14,3 % en B). **CA5 (costo) PASSED.**

El ruido de este emulador (6-14 %) dwarfs el 2 %: el criterio aplicable es el de "dentro del ruido
entre corridas iguales", y se cumple con holgura. En la consola real hay que repetirlo (el ruido ahí
es otro).

## 6. Límites de esta evidencia

- `vgl_ram_free_min=0` en las corridas con canario y `~3,3 GB` en las otras: el renderer por software
  de Vita3K no informa los pools como la Vita real ⇒ ese clue **no sirve en emulador** (sólo en
  consola).
- `tex_del_inflight=0` y `tex_up_inflight=0` en WAVE: el juego no borra texturas que un frame no
  presentado esté usando (al menos en esta pista).
- `circ_peak=0` en estas corridas: el contador del backend (bytes de vértices por frame) se cableó
  después (`72255f9`/`4f7bbed`); estas VPKs son anteriores.
- La pista usada fue WAVE (control sin glitch). Falta medir OFFICE/SLUMBER ( Fase 2, con el usuario).

## 7. Qué falta para la Fase 2 (consola)

1. Rebuild de V0/V1/V2 con los últimos commits (los de esta tabla son previos a `circ_peak`).
2. Repetir el A/B en la consola real (el ruido ahí es otro).
3. Correr el protocolo de `specs/052-vpk-diagnostico-bug047.md` §Protocolo con el usuario presente.
