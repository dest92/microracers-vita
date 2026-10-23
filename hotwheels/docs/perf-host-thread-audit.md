# HW-073 (perf-host) — auditoría de hilos, afinidad, prioridad y GIL (audio vs guest vs consumer)

Feature: HW-073 · Spec: `specs/054-perf-host-a9.md` · Fecha: 2026-10-21
Nivel de evidencia: **estático** (código y SDK `psp2/kernel/threadmgr/thread.h`, `psp2/kernel/cpu.h`).
Objetivo: explicar el **ruido de audio que sólo aparece bajo carga** (en carrera) auditando quién corre
en qué core, con qué prioridad, y si el GIL interviene. Pedido del orquestador; insumo para cc-audio (HW-069).

## Mapa de hilos (build Vita, `self_paced` = default ON)

| hilo | core (afinidad) | prioridad | fuente |
|---|---|---|---|
| guest: main + runners (`NativeScheduler`) | **USER_0 / core 0** | default | `runtime_vita.cpp:231-234` (comentario: "Fuera de USER_0 (runners guest + main)") |
| **consumer D3D (HW-053)** | **USER_1 / core 1** | **default** (nunca se cambia) | `runtime_vita.cpp:602-606` |
| **ds_emul mixer / productor** (`self_paced`) | **USER_1 / core 1** | `0x10000100` | `dsound_bridge.cpp:207-212` (hardcodeado, sin override) |
| **VitaSink feeder → `sceAudioOutOutput`** | **USER_1 / core 1** (default; `HW_SONCPU=2` lo mueve) | `0x10000100` | `vita_sink.cpp:137-138`; `mask_=0x00020000` en `:74` (open) y `:154` (miembro) |
| watchdog (heartbeat) | USER_1 / core 1 | `64` | `runtime_vita.cpp:231-234` |
| present interp60 | USER_2 / core 2 | default | `runtime_vita.cpp:728-733` |
| writer de capturas de imagen | USER_2 / core 2 | default | `runtime_vita.cpp:122-125` |
| sampler de perfil EIP (HW-051) | USER_1 / core 1 | `128` | `guest_eip_profile.cpp:120-123` |

## Hallazgo 1 (principal): el consumer D3D y el feeder DMA comparten el core 1

Durante la carrera el consumer D3D reproduce ~887 draws/frame más el armado de texturas y `vglSwap`
(`docs/60fps-techniques.md`, spec 003) — es el hilo más demandante del port — **y está clavado en el core 1**,
el mismo donde viven el productor de audio y el feeder que llama `sceAudioOutOutput`.

El feeder entrega bloques de `grain=512` frames ≈ **10.7 ms** a 48 kHz, y `sceAudioOutOutput` **bloquea hasta
que el driver consume el bloque** (`vita_sink.cpp:97-120`, `src/audio/output_contract.h`). La profundidad efectiva
del buffer del driver es chica (el hilo es el reloj de rellenado). Si el core 1 queda tomado por una ráfaga del
consumer más larga que ese colchón, el feeder llega tarde → **underrun a nivel DMA**: el hardware repite el último
bloque / entrega datos inválidos = **ruido, no silencio**. Como el consumer sólo se satura **en carrera**, esto
explica exactamente "offline no se nota, en carrera sí".

Evidencia estática del diseño esperado vs. lo implementado: `docs/perf-plan.md` palanca 7 fija
**"afinidad guest=core0, render=core1, audio=core2"**. La implementación deja **audio en core 1** (default de
`vita_sink.cpp` y hardcode en `dsound_bridge.cpp`). Hay un override `HW_SONCPU=2`, pero **no es el default** y
**no cubre al productor** (el th_entry de ds_emul ignora `HW_SONCPU`).

## Hallazgo 2: el GIL NO interviene en el camino de audio (refuta esa parte de la hipótesis)

El GIL (`winx86/src/runtime/gil.h`) serializa **sólo** lo que corre fuera del código traducido: cuerpos de shims,
intrinsics, dispatch SEH y estado del scheduler. El camino de audio en `self_paced` **no pasa por el guest**:

- productor: `dsound_bridge.cpp th_entry` → `audio_body` (ds_emul) → `mix_grain(nullptr)` → `EngineSink::write`
  → `DsoundBus::push`. No toca `d2rt::Cpu`/`Bridge`.
- feeder: `vita_sink.cpp thread_entry` → `Engine::thread_step` → `pump` → `Mixer::mix` → `DsoundBus::read`
  → `sceAudioOutOutput`. Tampoco.

Ninguno toma el GIL, así que el guest sosteniéndolo **no** puede hambrear al mixer ni al feeder.
Sí corren bajo el GIL los **shims DSound del guest** (Lock/Unlock/Play/SetFrequency), que son los que
*alimentan* al mixer; si el guest va lento, la fuente se seca, pero con `self_paced` el productor produce a
pared igual (contrapresión por `DsoundBus::wait_below`), así que el GIL no es la causa del ruido bajo carga.

## Hallazgo 3: a nivel dato, el underrun del bus da **silencio**, no ruido

`Mixer::mix` escribe **todos** los frames pedidos (`out[*] = clamp_s16(left/right)`, `mixer.cpp:76-77`);
si una fuente no tiene datos, `left/right=0` → silencio. `OutputBlocks::prepare` además rellena con cero hasta
`block` (`output_contract.h:55-72`). Por lo tanto el ruido **no** puede venir de "buffer viejo del mixer".
Refuerza que el ruido es **de planificación** (DMA) y no de mezcla. (Queda por separado la hipótesis de
carrera de datos DSound guest↔mixer, que es de cc-audio: `Lock/Unlock` sin ownership de cursor.)

## Hallazgo 4: prioridad — valor a confirmar en consola

El SDK dice "the lower the number the higher the priority" (`threadmgr/thread.h:144`) y su propio ejemplo usa
`0x10000100` como prioridad inicial (`thread.h:25`). Los hilos de audio piden `0x10000100`; el watchdog pide
`64` y el sampler `128` — numéricamente mucho menores. **No hay ninguna lectura de la prioridad efectiva**
(`sceKernelGetThreadPriority`) en los logs, así que la relación real audio↔consumer↔watchdog no está medida.
No afirmo un orden sin ese dato: lo dejo como telemetría de Fase 2 (abajo).

## Hallazgo 5: `SetFrequency` SÍ está sincronizado; los **bytes del buffer**, no

Pregunta del orquestador (voces de motor con `SetFrequency` continuo, 4745/25 s): ¿hay carrera de
`SetFrequency`/`Lock` con el mixer? Respuesta con evidencia estática (`ds_emul.cpp`):

- **`SetFrequency` NO es carrera.** `m_buf_setfreq` toma `g_mx` y escribe `v->rate` (`ds_emul.cpp:793-807`);
  `mix_grain` toma el mismo `g_mx` y hace *snapshot* de `v.rate` dentro del lock (`:236-250`). Escritor y
  lector se serializan: 190 llamadas/s × (trap + lock) es despreciable en CPU.
- **`Lock`/`Unlock` sincronizan cursor y metadatos, pero NO los samples.** `m_buf_lock` devuelve la VA guest del
  buffer **bajo `g_mx`** (`:694-718`); la app escribe los bytes **fuera** de `g_mx` y `m_buf_unlock` es un no-op
  (`:720`). El mixer guarda en el snapshot un **puntero a memoria guest** (`Voice::host` = "flat host view of the
  guest buffer", `:68`; `Snap.host`, `:222-223,249`) y **mezcla fuera del lock** (`:291` cierra la sección
  crítica; `:296-302` lee `s.host` con `mix_voice`).

Consecuencia: entre `Lock` y `Unlock` la app puede reescribir exactamente los bytes que el mixer lee. Peor:
`DSBLOCK_ENTIREBUFFER` entrega **todo** el buffer, incluida la región que suena (`:702`); sólo
`DSBLOCK_FROMWRITECURSOR` usa `write_cursor()` para saltear la región en reproducción (`:703-706`). El resampler
además **mira hacia adelante** (`pos..pos+8·align`), leyendo bytes que la app aún puede escribir → muestras
viejas/nuevas mezcladas = *click*; con más voces y rellenos (carrera) → más audible.

Honestidad: en DirectSound real el mixer también lee mientras la app escribe por delante del write cursor; la
fidelidad depende de que la app respete el cursor. Acá el emulador cubre el caso común (`write_cursor`) pero
**no** `ENTIREBUFFER` ni una deriva de cursor. Es **hipótesis con mecanismo**, no causa probada; se distingue de
la starvation (Hallazgo 1) con la telemetría de Fase 2 (wakeup tardío vs. glitches sin underrun).

## Propuestas para cc-audio (HW-069), por impacto y riesgo

1. **(cambio de default, medir) Afinidad de audio a core 2.** Poner el default de `vita_sink.cpp` y del
   productor `ds_emul` (`dsound_bridge.cpp`) en `SCE_KERNEL_CPU_MASK_USER_2` (0x00040000), dejando `HW_SONCPU=1`
   para volver a core 1 y medir A/B. Alinea con el diseño (perf-plan palanca 7) y saca al feeder del mismo core
   que el consumer D3D. Ojo: core 2 ya tiene el present interp60 y el writer de capturas; conviene prioridad del
   feeder > render durante la carrera y medirlo, no asumirlo.
2. **(telemetría, sin riesgo) Intervalo entre granos y wakeup tardío.** El feeder ya marca `hb::Slot::Audio`
   puntos 1/2 alrededor de `sceAudioOutOutput` (`vita_sink.cpp:104-106`). Agregar un contador de **máximo
   intervalo entre inicios de grano** y nº de veces que superó el presupuesto de bloque (≈10.7 ms) lo vuelca en
   la línea `metrics`/`fps-window`. Es la métrica que distingue "feeder tarde" (DMA) de "fuente seca" (guest).
3. **(telemetría, sin riesgo) Read-back de prioridad/afinidad.** Loggear `sceKernelGetThreadPriority` y
   `sceKernelGetThreadCpuAffinityMask` de cada hilo tras fijarlos (hoy sólo se loguea el rc del cambio en
   algunos). Resuelve el Hallazgo 4 en consola.
4. **(medición en host) Estrés guest-saturando + mixer.** Modelo del mecanismo: un hilo que satura un core
   (simula al consumer) + el feeder entregando granos cada 10.7 ms; medir el máximo intervalo y contarlo vs el
   presupuesto. En host reproduce el *efecto de scheduling* (no el DMA real de la Vita; nivel: host).
5. **(Hallazgo 5, si se confirma) Serializar los bytes leídos con las escrituras del guest.** Opciones, de
   menor a mayor invasividad: (a) seqlock por voz — `Lock`/`Unlock` incrementan un contador de secuencia y el
   mixer reintenta si cambió (barato, sin copia); (b) copiar el tramo leído (`cursor .. cursor+ventana`) a un
   scratch host **bajo `g_mx`** y mezclar de la copia (correcto, ~2 KB×voces por grano); (c) hacer todo el mix
   dentro de `g_mx` (simple, sube el tiempo de lock y puede demorar los shims del guest). Probar con **TSan**
   (ya se usa en `tests/run_log_buffer_concurrent.sh`) un test que corra el escritor guest + `mix_grain` en
   hilos distintos sobre el mismo buffer: hoy debería reportar carrera.

## Cómo medir en Fase 2 (consola)

VPK de medición con: (1) contador de wakeup tardío / max intervalo de grano; (2) read-back de prioridad y
afinidad por hilo; (3) captura del PCM entregado a `sceAudioOutOutput` (lo aporta cc-audio). Correlacionar con
`fps-window` (consumer_busy_pct) y con los logs de underrun del bus. A/B de afinidad audio=core1 vs core2
mientras se juega una carrera.

## No objetivos

No cambio la afinidad ni el código de audio desde acá (es el área de cc-audio y un cambio de scheduling exige
medición en consola). Esta auditoría es estática; sus números se confirman en Fase 2.
