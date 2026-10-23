# HW-073 (perf-host) — presupuesto A9 del resampleo de audio

Feature: HW-073 · Spec: `specs/054-perf-host-a9.md` (CA1/CA2/CA3)
Fecha: 2026-10-21 · Nivel de evidencia: **qemu (conteo de instrucciones) + estático (A9)**

## Qué mide y qué NO

Se cuenta **instrucciones dinámicas** por frame de salida del kernel `mix_voice` del `ds_emul`
(`d2rt::dsound`), con `qemu-arm -singlestep -d exec` sobre el binario ARMv7 (Cortex-A9, NEON).
`-singlestep` fuerza un TB por instrucción, así que cada línea `Trace` es una instrucción retirada.
Se corren dos longitudes (400 y 800 frames) y se resta para aislar el costo por frame.

**Esto NO son ciclos de la Vita**: qemu no modela el pipeline del A9. La conversión a fracción de
core usa un CPI declarado (default 1.0) en `tools/a9_budget.py`, con el reloj de release (444 MHz,
`runtime_vita.cpp`). Es una cota de **trabajo de CPU**, no una medición en consola.

## Instrucciones por frame de salida, por voz (rate 32342 → 48000, estéreo)

Reproducible con `bash tools/a9/run_a9_budget.sh` (usa la imagen `hw-host-arm:24.04`).

| kernel | insn/frame | ×lineal | fidelidad vs lineal (sweep cc-audio) |
|---|---:|---:|---:|
| `prod_linear` (actual) | 76.9 | 1.0 | 0 dB (referencia) |
| `cubic` (Catmull-Rom Q16) | 349.7 | 4.5 | +2.4 dB |
| `sinc8 Q15` | 456.0 | 5.9 | +7.9 dB |
| `sinc16 Q15` (candidato) | 789.3 | **10.3** | **+12.8 dB** |
| `sinc16_hoisted` (prototipo, ver abajo) | 301.0 | 3.9 | +12.8 dB (bitexacto) |

## Presupuesto de core A9 (48000 frames/s de salida × voces; CPI = 1.0)

`% de UN core` = `insn_frame · 48000 · voces / 444e6`. Con CPI 1.5 (peor caso conservador) multiplicar por 1.5.

| kernel | 1 voz | 4 voces | 8 voces | 16 voces |
|---|---:|---:|---:|---:|
| `prod_linear` | 0.8 % | 3.3 % | 6.7 % | 13.3 % |
| `cubic` | 3.8 % | 15.1 % | 30.2 % | 60.5 % |
| `sinc8` | 4.9 % | 19.7 % | 39.4 % | 78.9 % |
| `sinc16` | 8.5 % | 34.1 % | **68.3 %** | **136.5 %** (no cabe) |
| `sinc16_hoisted` | 3.3 % | 13.0 % | 26.0 % | 52.1 % |

Lectura: `sinc16` cuesta **~10× la lineal** y con **~8 voces resampleando** ya se lleva **~2/3 de un core A9**
(el core de audio, que además debe entregar los granos a `sceAudioOut`). El conteo de voces activas reales del
juego lo tiene cc-audio (`active_voices_*` de `dsound_residue.py`); si el pico supera ~8 resampleando, sinc16 no cabe.

### Memoria de la tabla

`sinc16`: 32 bancos × 256 fases × 16 taps × 2 B = **256 KiB** totales, **8 KiB por corte**. Una voz que usa un
corte a la vez cabe en L1D (32 KiB); 8 voces con cortes distintos = 64 KiB ⇒ L2. El costo medido **no** es de
caché (el prototipo es puro cómputo/ramas), pero conviene no asumir L1 con muchos cortes activos.

## Causa raíz del costo del prototipo y fix propuesto (medido, bitexacto)

El prototipo `sinc16` evalúa `p<0 || p+width>len` **en cada uno de los 16 taps** y acumula en `int64`. El guard
por tap impide vectorizar (NEON) y mete 16 ramas por canal por frame (mispredicts en el A9, in-order).

`tools/a9/sinc16_hoisted.h` decide la guarda **una vez por frame**: si la ventana `pos−7·align .. pos+8·align`
cabe entera (el caso común: en una voz en loop sólo ±8 frames por vuelta tocan el borde), los 16 taps son
contiguos y se recorren sin ramas; si no, cae a la ruta segura idéntica al prototipo.

- `789.3 → 301.0 insn/frame` (**2.6×**), con **checksum idéntico** (bitexacto, CA3 verificado por el script).
- `sinc16` a 8 voces: **68.3 % → 26.0 %** de un core A9.

Recomendación para cc-audio: **si se elige sinc16, adoptar la guarda izada** (o equivalente) antes de medirlo en
consola; el costo del prototipo actual no es representativo del kernel optimizado. `cubic` (4.5×, +2.4 dB) sigue
siendo la opción barata si el presupuesto aprieta; con la hoisted, `sinc16` (3.9×, +12.8 dB) queda al alcance.

## Reproducir

```bash
cd ~/vita-ports
bash hotwheels/tools/a9/run_a9_budget.sh          # mide y verifica bitexactitud
python3 hotwheels/tools/a9_budget.py --insn-frame 789.3 --voices 8 --taps 16
python3 hotwheels/tests/test_a9_budget.py          # CA1 (modelo)
```

Salida de `run_a9_budget.sh` (2026-10-21): ver `hotwheels/out/a9-budget/report.txt`.
