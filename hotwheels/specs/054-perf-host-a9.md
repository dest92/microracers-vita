# SPEC-054 — perf-host: presupuesto A9 del resampleo de audio y costo por fase del consumer D3D

- Feature: HW-073 (perf-host) · Milestone: M6 · Estado: borrador
- Insumos: `coordination/brief-perf.md`, `hotwheels/docs/perf-plan.md`, `docs/guest-profile.md`,
  `coordination/sol-audio.md` (candidato sinc16 de cc-audio), `hotwheels/specs/037-d3d-producer-consumer.md`,
  `hotwheels/specs/052-vpk-diagnostico-bug047.md`.

## Problema

El objetivo M6 es 40 Hz de guest + interp60 en consola, y no hay medición de fps de carrera en consola
(qemu y Vita3K no predicen el tiempo de un Cortex-A9). El lado **host** tiene dos incógnitas concretas que sí
se pueden acotar antes de la Fase 2:

1. **Presupuesto de audio en el A9.** cc-audio propone subir el resampler de voz de lineal (actual) a **sinc16 Q15**
   (+12.8 dB de fidelidad, `out/hw069-listen/sweep.json`), pero su banco sólo dio **ns de host x86** (linear 3.7, cubic 5.3,
   sinc8 5.6, sinc16 9.8) — no ciclos de A9. Sin ese número no se puede decidir si sinc16 cabe en el core de audio
   junto al mezclador. Ítem pendiente explícito de su handoff.
2. **¿El consumer D3D es el cuello de botella o lo es el guest?** El `fps-window` actual reporta `consumer_hz_milli`
   y `min_consumer_1s_hz_milli`, pero no separa **tiempo ocupado del consumer** de **tiempo esperando un anillo**.
   Con un sólo core de render (USER_1, HW-053) un consumer al 100 % limita el fps; un consumer ocioso apunta al guest.

## Contrato observable

### A. Presupuesto A9 del resampleo (herramienta, evidencia qemu + estático)

- `tools/a9_budget.py` convierte una cuenta de instrucciones por frame de salida a **fracción de un core A9**:
  `insn_s = insn_frame · out_rate · voices`; `cores = insn_s · CPI / 444e6` (reloj de release, `runtime_vita.cpp`).
  IO: `insn_frame`, `out_rate` (Hz), `voices`, `cpi`; la salida es estable y parseable (una línea `k=v`).
- `tools/a9/run_a9_budget.sh` mide **instrucciones dinámicas reales** con `qemu-arm -singlestep -d exec`
  (conteo de TB = 1 instrucción por TB bajo `-singlestep`), restando dos longitudes de corrida para aislar el
  costo por frame (sin arranque/teardown). Cubre: producción (lineal), lineal, cubic, sinc8, sinc16 y el prototipo
  `sinc16_hoisted.h`. Emite `insn_frame` por kernel.
- **Bitexactitud**: el prototipo hoisted debe dar el **mismo checksum** que el kernel de producción sobre una
  entrada determinista; de lo contrario el ahorro medido no es comparable (cambió la semántica).

### B. Costo por fase del consumer D3D (código, host+qemu+Vita)

La línea `fps-window` (cada 10 s, `src/d3d/bridge.cpp`) suma, por ventana y por frame completado:
`consumer_busy_us_per_frame` (dentro de `consume_frame`), `consumer_wait_us_per_frame` (bloqueado en
`wait_take` sin anillo) y `consumer_replay_us_per_frame` (dentro de `Replayer::play`/`present_frame`);
`consumer_busy_pct_milli` (ocupación de un core por el hilo consumer); y el **trabajo por frame**
`consumer_draws_per_frame_milli` y `consumer_state_ops_per_frame_milli` (ops `Draw` y `State`/`Tss`/`Texture`
del ring, contadas en el mismo recorrido que ya cuenta uploads). Con eso, en Fase 2 el VPK de medición responde
si el render satura el core 1 (y por lo tanto si puede hambrear a los hilos de audio co-ubicados, ver
`docs/perf-host-thread-audit.md`) y cuánta carga por frame ejecuta. Los campos son **aditivos**: no cambian el
pipeline ni el número de draws.

**Unidades (contrato).** La familia `*_milli` de la línea `fps-window` es siempre *el valor en milésimas de su
unidad base*: `guest_d3d_hz_milli`/`consumer_hz_milli` son mili-hertz (`Hz × 1000`); `consumer_busy_pct_milli`
es la fracción de la ventana ocupada en milésimas (`pct × 10`); y **todos los conteos por frame**
(`tex_uploads_per_frame_milli`, `tex_backend_uploads_per_frame_milli`, `consumer_draws_per_frame_milli`,
`consumer_state_ops_per_frame_milli`) son **promedio por frame × 1000** — `910` draws/frame ⇒ `910000`.
Los conteos van multiplicados por 1000 y no en unidades crudas por dos razones:
(a) el promedio por frame es **fraccionario** (910,5 draws/frame) y truncarlo a entero pierde la fracción que
distingue dos corridas (aquí 910 vs ~887 documentados = 2,6 %);
(b) los conteos de draws/state_ops se leen **junto a** `tex_uploads_per_frame_milli` en la misma línea y con el
mismo denominador (`frames completados` de la ventana), así que comparar "uploads por frame" contra "draws por
frame" —la redundancia de HW-040/065— exige la misma escala en los dos, o la comparación queda mal dimensionada.
Ésta es la razón por la que BUG-051 se corrige en el test y no en el impl.

## Criterios verificables (cada uno ⇒ un test)

- [ ] **CA1** `tools/a9_budget.py` calcula `insn_s`, `cycles_s` y `cores` con la fórmula de arriba; caso borde
      `voices=0`/`insn_frame=0` ⇒ `cores=0`; monotónico en `voices` e `insn_frame`. Test host `tests/test_a9_budget.py`.
- [ ] **CA2** `tools/a9/run_a9_budget.sh` corre en la imagen `hw-host-arm:24.04`, mide los seis kernels y reproduce,
      dentro de ±3 %, los `insn_frame` registrados en `docs/perf-host-a9-budget.md`. Nivel: qemu (conteo de instrucciones;
      **no** ciclos A9).
- [ ] **CA3** El prototipo `sinc16_hoisted.h` es bitexacto contra el kernel de producción sobre la entrada
      determinista (mismo checksum) y su `insn_frame` es menor. `tools/a9/run_a9_budget.sh` construye y corre ambos
      y **falla** si los checksums difieren (verificado: 789.3 → 301.0 insn/frame, checksum idéntico).
- [ ] **CA4** El reporte por ventana del consumer (`src/d3d/consumer_phase.h`) calcula los promedios por frame,
      la ocupación y los conteos por frame sin dividir por cero, clasifica las ops del ring (Draw vs State/Tss/
      Texture) y formatea los seis campos con nombre estable. El test fija además la **escala** de los conteos
      (1 draw/frame ⇒ `1000` milésimas; 900 draws/frame ⇒ `900000`), que es la del contrato "Unidades" de más
      arriba. Test host `tests/test_consumer_phase.cpp` (`tests/run_consumer_phase.sh`). El cableado en
      `bridge.cpp` lo compila `build_host.sh` y la línea `fps-window` lleva los campos en qemu (verificado:
      draws/frame ≈ 910, contra ~887 documentados).
- [ ] **CA5** Sin regresión de build: `build_host.sh` (qemu) y `build_vita.sh` PASS; `run_d3d.sh` y `run_audio.sh` PASS.

## Razonamiento de bajo nivel (OS/hardware)

- **Hilos y cores.** El guest corre en el core 0 bajo el GIL del runtime (scheduler nativo, `winx86/src/runtime/gil.h`):
  todo lo que corre *fuera* del código traducido (shims, intrinsics, despacho de traps) se serializa; el código traducido
  corre *sin* el GIL. El consumer D3D (HW-053) vive en USER_1/core 1 y es el único dueño de Replayer/vitaGL/`vglSwap`.
  El audio `self_paced` (BUG-031) corre en USER_1 también si comparte máscara — **hay que fijarlo a USER_2/core 2** para
  no competir con el consumer: dos hilos CPU-bound en el mismo core A9 se reparten ciclos y el `consumer_busy_pct` lo delata.
- **Ownership CPU↔GPU de buffers.** Un anillo (`Ring`) pasa `WRITING→READY→IN_FLIGHT→FREE`; el consumer lo reproduce y
  recién libera al terminar el replay+present. El tiempo caro del consumer es por tanto el **replay** (armar/consumir
  vértices y texturas) más el **present/swap**; medirlos por separado distingue "CPU lenta armando el frame" de
  "GPU/driver limitando el swap". Ésta es justamente la distinción que el VPK de la Fase 2 necesita.
- **Concurrencia y memoria.** Los contadores nuevos los escribe **sólo** el hilo consumer, así que no requieren atómicos;
  `fps-window` se emite en el consumer y hoy ya es así. No se lee memoria guest desde el consumer (se copió al anillo).
- **Costo del resampleo (A9 ≠ x86).** El A9 es **in-order, dual-issue**, con L1D 32 KiB / L2 512 KiB y **sin** NEON para
  las operaciones de enteros de 64 bits encadenadas. El prototipo sinc16 hace, por cada uno de 16 taps, un guard
  `p<0 || p+width>len` **y** una acumulación `int64 += int16*int16`; el guard por tap impide vectorizar y mete 16 ramas
  por canal por frame (mispredicts). La tabla de coeficientes por corte es 256·16·2 B = 8 KiB ⇒ entra en L1 si una voz usa
  un corte a la vez; el costo NO es de caché sino de **instrucciones y ramas**.
- **Bordes.** `pos` en loop: la ventana `pos-7·align .. pos+8·align` toca el borde sólo en ±8 frames por vuelta; el resto
  es interior. `rate==out_rate` ⇒ `f==0` y `bank==0` ⇒ se salta el filtro (no se paga sinc en voces no resampleadas).
  Una voz one-shot que pasa el final devuelve `false` y el resto del grano queda en silencio (semántica actual, no se toca).

## No objetivos

- No se cambia el kernel de producción ni se habilita sinc16 en el runtime (decisión de cc-audio con este presupuesto).
- No se toca interp60, ni la física, ni la política de filtrado, ni la afinidad de hilos existentes.
- No se afirma una mejora de fps en consola: el presupuesto es estático+qemu y la ocupación del consumer se mide en
  la Fase 2 con la consola presente.

## Evidencia

- Estático: A9 (in-order dual-issue, L1D 32 KiB), `gil.h`, `frame_queue.h`, `gl_state_cache.h`, `runtime_vita.cpp` (clocks).
- qemu: conteo de instrucciones dinámicas por frame de cada kernel (`tools/a9/run_a9_budget.sh`).
- Host: test de fases del consumer; checksum de bitexactitud del prototipo.
- Consola: pendiente (Fase 2, VPK de medición con el usuario).
