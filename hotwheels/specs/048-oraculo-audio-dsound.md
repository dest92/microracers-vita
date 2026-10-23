# SPEC-048 — Oráculo offline DirectSound
- Feature: HW-069 · Milestone: M8 · Dueño: sol-audio · Estado: borrador

## Problema
BUG-031 sigue abierto: ritmo y underruns correctos no acreditan fidelidad. Hay dos remuestreos (voz→22050, bus→48000); hace falta separar error PCM/ganancias, aliasing, bordes de bloques y alimentación. El parche 0012 sólo probó interpolación sintética, sin oído en consola.

## Contrato observable
Captura diagnóstica opt-in `HW_DS_TRACE=<archivo>`: stream versionado, little-endian, acotado; registra formato/creación, duplicados/release, Lock/Unlock con bytes efectivos, frecuencia/volumen/pan, posición/Play/Stop, GetCurrentPosition y fronteras de mezcla con PCM de salida. Sin la variable no se crea archivo ni copia PCM. Datos del juego quedan ignorados por Git.
Replay host consume los mismos eventos en orden y usa **el mismo** `mix_voice` de ds_emul para PCM exacto a 22050. Este replay es sólo oráculo de **determinismo**, no de fidelidad. Un segundo camino independiente float64 con sinc Lanczos de 64 taps (offsets -31..32, ventana sinc(d/32), cutoff=min(1,out_rate/source_rate), normalización DC por muestra) aplica la ley DirectSound de amplitud `10^(mB/2000)` y pan que atenúa el canal opuesto. Informa errores exactos contra la captura, SNR, RMS, saturación, discontinuidades y energía espectral; THD sólo para fixture senoidal de frecuencia conocida (no llamar THD al ruido de un motor real). Exporta WAV sólo con `HW_WAV=1`.
La mezcla final 22050→48000 se valida aparte con entradas fragmentadas y completas idénticas. Conserva fase y lookahead entre lecturas sin bloquear esperando datos. El informe distingue discontinuidad natural, aliasing y defecto demostrado; no infiere calidad audible de un contador.

## Criterios de aceptación
- [ ] CA1 captura opt-in/cota/truncación y replay determinista: formato, Unlock partido, duplicate/release, cambios de frecuencia/fase y cursor → `tests/run_dsound_oracle.sh`.
- [ ] CA2 PCM mono/estéreo y formato 8 unsigned/16 signed, ganancias -600/-10000 mB, pan ±10000, suma y saturación → fixture sintético/oráculo. Formatos sin implementar se reportan, nunca se interpretan silenciosamente como válidos.
- [ ] CA3 comparador independiente sinc detecta mutantes vecino más cercano, dB /1000 y fase reiniciada; fixture senoidal mide SNR/THD/espectro/RMS/clipping → suite oráculo.
- [ ] CA4 resampler final produce salida independiente de cortes de pull/mix cuando hay datos; test debe fallar antes del fix y matar mutante que mantiene el último sample → `tests/run_audio_boundaries.sh`.
- [ ] CA5 captura menú+carrera del PE real en qemu/Vita3K; replay exacto compara cada muestra y documenta formatos, SetFrequency, cursores y escrituras solapadas. Tabla antes/después sobre **la misma captura**, con SNR, RMS, clipping, discontinuidades y energía espectral por etapa. En la etapa final se conserva el PCM de ds_emul y se compara Mixer previo/fijo contra sinc/polifásico. Evidencia mínima persistida sin artefactos efímeros completos.
- [ ] CA6 VPK candidato identificable por SHA256 y WAV comparativo corto listo para Fase 2; build D2Vita si cambia winx86; reviews resueltas antes de `done`.
- [ ] CA7 oído ≥60 s en consola con usuario, 0–1 underruns sin distorsión: **Fase 2, no se ejecuta autónomamente**. BUG-031 permanece abierto hasta esto.

## Diseño de bajo nivel
Guest y shims corren bajo GIL; el mixer realtime corre en su propio pthread, sin adquirir GIL, y usa `g_mx` para voces. Orden único GIL→g_mx; observación no introduce espera por otro hilo ni callbacks hacia guest (evita espera circular de Coffman). Eventos de estado y snapshots se ordenan bajo g_mx; PCM de cada grano se identifica por índice absoluto, no por reloj wall inestable. Captura es intrusiva y no sirve para medir rendimiento.
PCM guest es arena VA→host; el trace copia bytes al Unlock, no guarda punteros host persistentes. Escribir mientras se reproduce puede competir con lectura fuera de g_mx: registrar intervalos Lock/Unlock y marcar incertidumbre; no afirmar exactitud si ocurre. No se modifican SMC, MMU, dynarec, flags ni convenciones stdcall. El observer se agrega al final de HostOps y queda nulo para D2Vita.
Recorder usa buffering y cota de bytes para limitar E/S/memoria. Replay es host, sin carga en Vita. El resampler final es single-consumer; sus fuentes siguen siendo prestadas y el bus protege publicación con mutex (release/acquire de ARMv7). Lookahead se conserva con la fase; no se agrega hilo, bloqueo ni syscall por muestra.

## Regla de decisión y baseline
La aceptación algorítmica CA4 exige error máximo ≤1 PCM contra interpolación continua, **cero holds artificiales de frontera** con datos disponibles, ninguna violación ASan/UBSan y mismo resultado entre particiones (tolerancia ≤1 PCM por redondeo). Baseline host antes: pull512 22050→48000, max_error=16434, RMS_error=280.550; downsample ASan heap-buffer-overflow (log `out/hw069-boundaries-before.txt`). Después: 36 combinaciones, max_error≤1, sin sanitizer; revisión 6779ed8 APRUEBA.
En la captura real, el fix final sólo se acepta como mejora de fidelidad medida si clipping no aumenta y el error RMS/SNR vs referencia independiente mejora; delta <0.1 dB se declara **no separable** y no se atribuye calidad audible. Una caída ≥3 dB de error o desaparición de discontinuidades de frontera acredita efecto fuerte. Ningún umbral offline sustituye CA7. Para material arbitrario, los saltos >16384 y energía >0.4·Fs son indicadores, no defectos ni aliasing automáticamente.

## No-objetivos
No hacer sinc realtime en A9 sin medir costo; no cambiar pan a equal-power; no tocar consola ni plugins; no cerrar BUG-031 por métricas offline. Wine WAV adicional es opcional si hay captura de mezcla aislada disponible.

## Evidencia
Antes/después host con mutantes y fixtures; PE real qemu Docker o Vita3K con lock. Compilación Vita y D2Vita aislada si corresponde. Consola/escucha se entrega a Fase 2. Cada métrica conserva comando, tamaño y nivel, sin FPS inferidos de qemu.

## Ampliación del orquestador — 2026-10-21
- CA8: residuo por WAV/voz, categoría (motor/efectos/MCI), bandas 0–2/2–4/4–8/>8 kHz y relación temporal con Play/Stop/loop/frecuencia. Saturación de suma real vs referencia, sin confundir samples al límite con error de ganancia.
- CA9: barrido mismo stream con cúbico4, sincQ15 8/16 taps; SNR/RMS/saturación más costo host/qemu y estimación A9 **identificada como estimación**. No se habilita un algoritmo realtime sólo por el número host.
- CA10: tap HW_WAV=1 de **las dos fuentes reales** que consume Mixer (DS y música MCI), rates observados por muestra comprimidos por cambios, bloques mixtos y bytes enviados a sceAudioOut. Replay usa las mismas lecturas/vacíos/tasas, comprueba salida final bit a bit; DS entregado por bus coincide con prefijo del replay ds_emul. Música llega al mismo Mixer/puerto; metadatos del WAV fuente quedan registrados.
- CA11: contrato sceAudioOut (retorno>=0, rate48000 MAIN, frames múltiplo64, GetRestSample antes/después, un solo thread consumidor y cola acotada). Dos buffers alineados64 conservan bytes hasta que sean reutilizables; no se atribuye el síntoma a DMA sin medición.
- Capturas intrusivas cierran a los20s para dejar END/WAV válidos aunque el harness mate Vita3K; sólo con HW_WAV=1. Resampler de voces sigue siendo un prototipo offline hasta el gate de costo/semántica. Sin acceso a consola.

## Ampliación cc-audio — 2026-10-21 (sucesor de sol-audio)

### CA11, mecanismo (por qué el bloque debe medir el `len` del puerto)
`psp2/audioout.h` fija `len` al abrir el puerto: entero en [SCE_AUDIO_MIN_LEN=64, SCE_AUDIO_MAX_LEN],
**múltiplo de 64**. `sceAudioOutOutput` es bloqueante y consume SIEMPRE ese `len`, no la cantidad de
frames válidos del buffer (header + sink de winx86 `src/platform/vita_audio.cpp`, probado en D2Vita). El
sink de hotwheels no respetaba tres consecuencias:
1. `grain_` se recortaba a [1,2048] sin exigir múltiplo de 64: un grano no múltiplo hacía fallar
   `sceAudioOutOpenPort` → silencio total (no ruido). Ahora el `len` se alinea a 64 y se valida.
2. El buffer entregado era el `std::vector<int16_t> scratch_` de Engine (16 B de alineación). Se copia a
   un buffer `alignas(64)` (múltiplo de la línea L1 de 32 B de la plataforma). El SDK **no** documenta un
   requisito de alineación del puntero: la copia es **defensiva** (igual que en winx86, que lo marca
   como tal), no una corrección de un defecto demostrado.
3. Si el bloque trae menos de `len` frames válidos, el driver reproduce la cola del grano anterior; el
   resto queda en cero (observado y documentado en el sink de winx86).
Ownership: el SDK tampoco documenta el instante exacto de retiro del puntero; lo que hace seguro
reutilizar el bloque es que `sceAudioOutOutput` es bloqueante (~un grano), y el pingpong de dos buffers
deja la regla explícita (el anterior no se reescribe hasta dos envíos después) a costo cero. La cola
restante se mide con `sceAudioOutGetRestSample` antes y después de cada envío.
Puerto: MAIN es el del contrato; si no abre, el fallback BGM queda **registrado** en el log
(`tipo=MAIN|BGM`), porque el tipo decide la ruta de mezcla del sistema y el contrato observable no puede
variar en silencio.
Costo de la copia (medido, no calculado): 80 ns por grano de 2048 B en host x86
(`tests/run_audio_output_contract.sh`); antes el sink entregaba el vector de origen sin copiar. Es un
número de **host**, no de A9/Vita: el costo en destino queda pendiente de medición en consola.
`OutputBlocks` no asigna memoria: vive en el sink y la ruta realtime nunca llama a `malloc`.
Con `kGrain=512` (múltiplo de 64) los puntos 1 y 3 no se disparan en la traza medida: el cambio es
conformidad de contrato + elimina un fallo latente, **no un fix audible**. Nivel: estático.

### Fase 2 — telemetría y captura del PCM entregado a sceAudioOut (hipótesis dependientes de carga)
El oído descartó el mezclador offline (linear ≈ sinc16 "suenan bien"); el ruido aparece en consola bajo
carga. Objetivo: instrumentar el final de cadena y capturar el PCM real, para atacar las hipótesis
dependientes de carga (starvation del hilo de audio, carrera guest→mixer, deriva del self-paced):
- por grano, con timestamp: tiempo de mezcla, lateness del wakeup (vs período del grano), rc de
  `sceAudioOutOutput`, `sceAudioOutGetRestSample` antes/después, y readback de afinidad/prioridad del
  hilo (el kernel puede devolver rc=0 sin aplicar el pinning si no se re-hace en el propio hilo).
  El reloj del kernel (`sceKernelGetProcessTimeLow`) es de 32 bits y hace wrap: las restas se hacen en
  uint32 y recién después se ensanchan.
- **La captura y la telemetría son intrusivas**: el tap escribe con `fwrite` por grano (y refresca el
  header cada 64 KiB) y la telemetría abre/cierra `audio.log` en el hilo de audio. Una corrida con
  `HW_WAV=1` **no** sirve como benchmark de scheduling ni de calidad de audio: primero hay que medir el
  camino normal (sin captura) y comparar.
- tap `HW_WAV=1` en Vita: WAV del mix final (música incluida) entregado al sink, cerrado a los 20 s.
- Música (CA10, medido): la mezcla REAL del Engine host (48000 Hz, `HW_WAV=1`, misma corrida que la
  captura) contiene la pista MCI. `tools/hw069_music_probe.py` sobre los últimos 6 s: lag 1.310 s
  **consistente en ambos canales**, coef +0.331/+0.237 y −6.5/−7.5 dB de proyección (Track3), RMS
  −13.91 dBFS. WAV para el oído: `out/hw069-listen/mix-final-con-musica.wav`. Es presencia/nivel, no
  fidelidad (pista looped en offset desconocido + efectos no correlacionados). La contraparte **sin**
  música a 48 kHz sale del replay exacto del tap (CA10, pendiente).
- host: test de estrés (productor saturando + `Mixer` consumiendo) buscando tearing/underrun.

### Fase 2 — scheduling de los hilos de audio (afinidad/prioridad)
Evidencia estática (`docs/perf-host-thread-audit.md`, HW-073): el feeder de sceAudioOut y el productor
self-paced quedaban en USER_1, el mismo core que el consumer D3D (HW-053) — el hilo más demandante en
carrera. `sceAudioOutOutput` es el reloj del feeder (bloquea ~un grano); si una ráfaga del consumer pasa el
colchón del driver, el feeder llega tarde y el hardware repite el último bloque → ruido sólo bajo carga.
El plan de perf fija render=USER_1, audio=USER_2: se mueven **ambos** hilos de audio (feeder y productor) a
USER_2 y a prioridad `0x100000E0` (el SDK: *"the lower the number the higher the priority"*; el default del
consumer es 0x10000100, así que 0x100000E0 es más alta). El feeder pasa la mayor parte del tiempo bloqueado
en Output, así que su prioridad alta no le roba CPU al render; el **productor** self-paced no es igual:
mezcla CPU cada vez que `wait_below` lo despierta, y USER_2 ya aloja interp60 y el writer de captura, así
que puede retrasarlos de forma proporcional al costo de mezcla. Por eso "no le roba CPU al render" **no**
se sostiene sin el A/B de consola. Ambos hilos loguean el readback de mask y de la prioridad efectiva: si
el kernel no aplicó lo pedido, la línea dice `INVALIDA` y esa corrida no sirve como medición de scheduling.
La inversión de prioridad posible (feeder esperando `g_mx`/DsoundBus que toma un shim del guest bajo el
GIL) está acotada por secciones críticas de µs y respeta el orden único GIL→g_mx. `HW_SONCPU=1` vuelve a
USER_1 para el A/B.
Medición: `per_avg` vs `exp` y `rest_min` por grano (arriba), antes/después del cambio, en consola.

### Frente 2 — cursor DSound: verificación y mecanismo (estático)
Pregunta del orquestador: ¿el cursor (`GetCurrentPosition`) refleja la lectura real del mixer o el reloj de
pared? **Refutado que siga el reloj**: `m_buf_getpos` devuelve `v.cursor` (= posición de lectura del mixer)
y `write_cursor(v)` = `v.cursor + lead` (`ds_emul.cpp:671-679`, `:643-649`). `v.cursor` avanza **sólo**
dentro de `mix_grain`, bajo `g_mx`, por `frames*rate/kRate` frames de fuente por grano (`:268-286`). Si el
mixer se atrasa, el cursor avanza menos: **no** puede adelantarse sobre lo no leído. `write_cursor` es
derivado del cursor, no del reloj.
Mecanismo real (geometría, `tests/run_audio_ds_cursor_window.sh`): el mixer lee por grano
`K = floor(kGrain*rate/kRate) + 8` frames (8 = look-ahead del resampler, audit HW-073) → ventana
`K*blockAlign` bytes, **fuera** de `g_mx`; el guest escribe desde `write_cursor = cursor + min(2*kGrain,
len/4)`, también fuera de `g_mx`. Si `ventana > adelanto`, la escritura del guest cae dentro de la región
que el mixer todavía no consumió:
- voz sin pitch (rate=kRate), buffer 0.2 s @22050 estéreo (17640 B): adelanto 4096 B > ventana 2080 B → OK.
- voz de motor con pitch alto (peor `SetFrequency` de la captura: 32342 Hz) + buffer corto (8820 B): el cap
  `len/4` deja el adelanto en 2205 B < ventana 3032 B → **solape**. El factor `rate/kRate` (1.47×) sólo
  existe con voces con pitch (carrera), y los `Lock`/`Unlock` sólo reescriben bytes si el juego pide
  `DSBLOCK_ENTIREBUFFER` (entrega **todo** el buffer, incluida la región que suena, `:702`) o escribe más
  allá del cursor. Eso liga el ruido a la carrera: más voces con pitch + más rellenos.
Arreglo (aprobado: winx86 como parche aditivo en `hotwheels/patches`):
(a) seqlock o copia bajo `g_mx` **no alcanzan**: el guest escribe por el puntero de `Lock` **sin** tomar
`g_mx`, así que ningún lock del lado host lo excluye. La garantía tiene que ser geométrica.
(b) **Contrato de cursores**: `write_cursor(v)` debe estar a o más allá del final de la ventana que el mixer
lee en un grano, en bytes de la fuente y escalada por la frecuencia actual. Definición única en
`winx86/src/runtime/ds_cursor.h`: `read_window_frames = ceil(frames*rate/kRate) + lookahead` y
`cursor_lead_bytes = ventana + un grano de holgura para el relleno`, acotado a `[blockAlign, len -
blockAlign]` (mantiene `write_cursor != play_cursor`). Reemplaza `min(2*kGrain, len/4)`, que asumía
rate == kRate y no contaba el look-ahead. Si el buffer es tan chico que `len - blockAlign < ventana` no hay
lead estático posible: es un límite físico del buffer, y no se sacrifica audio bajando la ventana de mezcla.
Test: `tests/run_ds_cursor_contract.sh` contra la definición real (mutante = fórmula vieja, que el test
mata). El contrato elimina el solape, así que **no** se agrega copia de la ventana al grano.
`TSan` **no** puede ser el gate: el ring comparte bytes entre granos sin sincronización por diseño (la app
escribe adelante datos que el mixer reproduce después), así que TSan reporta carrera en cualquier
implementación correcta. El defecto es sólo el solape **dentro del mismo grano**, y eso es geometría, no
concurrencia: lo califica el test de contrato.

### Fase 2 — runbook (CA6/CA7)
VPK candidato: `hotwheels/out/hw069-build-vita/hotwheels-vita.vpk`, 1.949.839 B, SHA256
`6865a88667642473607f5aacba803e5d59b64f23f68c8e4b3c9e97ab5a2e02da` — incluye CA11, el tap (CA10), la
telemetría, el scheduling USER_2/prio 0x100000E0 y el contrato de cursores (winx86-0014). WAV corto para el
oído: `out/hw069-listen/` (FX 22050 + `mix-final-con-musica.wav` 48000).
En consola, con `HW_WAV=1`: el tap deja `ux0:data/hotwheels/dsound.wav` (20 s, PCM entregado a
sceAudioOut, música incluida). En `ux0:data/hotwheels/audio.log`: `vita_sink: hilo audio arriba rc_pin=…
rc_prio=… leido=0x…` (¿el kernel aceptó la prioridad/afinidad nuevas?) y `vita_sink: tel … per_avg/exp/
rest_min` cada ~10 s (`per_avg > exp` = el hilo llegó tarde; `rest_min = 0` con el puerto tocando =
underrun). A/B: `HW_SONCPU=1` (core 1, junto al consumer D3D) vs default (core 2). CA7 (oído ≥60 s, 0–1
underruns sin distorsión) se ejecuta **con el usuario**; BUG-031 sigue abierto hasta entonces.

### CA11 — seam del puerto: el contrato del sink, testeable en host
El test anterior sólo ejercitaba `OutputBlocks` (el copiador), no el contrato del
sink con el driver. Ahora el driver del puerto se inyecta (`audio/port_contract.h`:
`PortDriver` + `PortOut`) y el sink de Vita usa **ese mismo** camino, así que lo
que se prueba en host es lo que corre en la Vita. El test cubre: el len que se
pide al driver (múltiplo de 64, mínimo 64), el fallback MAIN→BGM con el tipo
registrado, el bloque que llega al driver (del tamaño del puerto, alineado a
64 B), la **reutilización** (pingpong: el bloque anterior no se reescribe), el
pad de la cola a cero y el `rc<0` como error.
Además, el grano se alinea en un único lugar (`output_block_frames`, que usan el
motor y el sink): con un grano de 500 frames el puerto consumía 512 y la mezcla
avanzaba 2,4 % más despacio que el audio (drift). Medido en Vita3K: tap de
20,0 s, `err=0`, `per_avg` ≈ 10,2 ms contra `exp` 10,67 ms.

### Frente 2 — A/B medido del contrato de cursores (respuesta al CAMBIOS de review-5fab693)
`tools/dsound_cursor_ab.py` sobre la captura real (`out/hw069-capture/dsound.bin`, 42 voces, 45 Lock):
- **El juego no rellena mientras suena**: los 45 Lock ocurren con `off=0` y **antes** del Play de su voz
  (0 Locks con offset distinto de 0 → no usa `DSBLOCK_FROMWRITECURSOR`) y no hay Unlock mientras playing.
  O sea: el write cursor **no influye** en el comportamiento de este juego → el cambio es **preventivo**,
  no corrige un defecto observado acá.
- **El replay offline no puede medir el efecto audible**: ignora el offset del Lock y usa los offsets
  grabados en el trace (`tools/dsound_replay.cpp`, `case DsLock: … (void)voice()`), o sea la fórmula no
  entra. El A/B audible necesita una corrida real del juego (consola con el usuario).
- **Costo del lead más grande, medido**: el room que le queda al guest (len − lead) baja de mediana
  1114.56 ms a 1114.51 ms; de los 45 buffers, **44** podrían hacer streaming (con la fórmula vieja su
  room ya era ≥ un grano de fuente) y para esos el room nuevo mínimo es **36.96 ms**; el único caso
  degenerado (buffer de 1788 B, room 0.05 ms) es un buffer de un solo llenado que con la fórmula vieja
  tampoco podía hacer streaming (room 30 ms < 1 grano = 2048 B). El costo, entonces, no toca a los
  buffers que hacen streaming.

### Fase 2 — requisitos de despliegue que NO cubre el deploy actual
Medido en Vita3K (30 s, HW_WAV=1): la música **no** sonaba porque el juego pide
`MUSIC\TrackN.wav` y el resolver de hotwheels prueba dos rutas: `game_root/MUSIC/...` y
`game_root/../../music/...`. En la Vita `game_root` es `ux0:data/hotwheels`, así que la primera ruta es
`ux0:data/hotwheels/MUSIC/` y la segunda `ux0:/music/`. Ni `tools/stage_data.sh` ni `tools/vita_deploy.py`
subían la música → en la consola real la música MCI quedaría en silencio. **Resuelto**: `vita_deploy.py`
sube `Track*.wav` a `ux0:data/hotwheels/MUSIC/` (la primera ruta del resolver; no se escribe en
`ux0:/music/` para no tocar la raíz del dispositivo del usuario) y avisa por log si no hay pistas.
Además, el juego tiene que **arrancar** por default: `HW_STOP_AT_FIRST_TRAP` quedó en `1` y el juego
paraba en el primer import sin shim (KERNEL32!GetVersion) sin llegar nunca a DirectSoundCreate. Ahora la
parada es opt-in (`HW_STOP_AT_FIRST_TRAP=1`) y el default es continuar, como el host: verificado en
Vita3K con un `env.txt` limpio (el juego corre y el motor de audio levanta). Para la Fase 2 hace falta
sólo `HW_WAV=1` en `ux0:data/hotwheels/env.txt` (el tap).

### Ampliación previa del orquestador (sin cambios)
- CA8: residuo por WAV/voz, categoría (motor/efectos/MCI), bandas 0–2/2–4/4–8/>8 kHz y relación temporal con Play/Stop/loop/frecuencia. Saturación de suma real vs referencia, sin confundir samples al límite con error de ganancia.
- CA9: barrido mismo stream con cúbico4, sincQ15 8/16 taps; SNR/RMS/saturación más costo host/qemu y estimación A9 **identificada como estimación**. No se habilita un algoritmo realtime sólo por el número host.
- CA10: tap HW_WAV=1 de **las dos fuentes reales** que consume Mixer (DS y música MCI), rates observados por muestra comprimidos por cambios, bloques mixtos y bytes enviados a sceAudioOut. Replay usa las mismas lecturas/vacíos/tasas, comprueba salida final bit a bit; DS entregado por bus coincide con prefijo del replay ds_emul. Música llega al mismo Mixer/puerto; metadatos del WAV fuente quedan registrados.
- Capturas intrusivas cierran a los20s para dejar END/WAV válidos aunque el harness mate Vita3K; sólo con HW_WAV=1. Resampler de voces sigue siendo un prototipo offline hasta el gate de costo/semántica. Sin acceso a consola.
