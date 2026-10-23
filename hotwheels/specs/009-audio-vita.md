# Spec 009 — Backend de audio Vita (HW-018)

- Feature: HW-018 · Milestone: M4 · Dueño: ports · Estado: implementada (tests host CA1–CA7 verdes), pendiente review
- Depende de: HW-015 (`hw::winmm::MusicSink`, spec 005). Coordina con HW-006 (registro de
  `DirectSoundCreate`/`ds_emul` en el Bridge) y HW-009 (boot/threads en Vita).

## Problema
El juego tiene **dos fuentes de audio independientes**:
1. **Efectos** por DirectSound (`dsInit` 0x44efd0): primario PCM **22050 Hz, 16 bit, estéreo**
   (`re-engine.md` §DSound), ~45 WAV, buffers secundarios estáticos, sin 3D ni streaming.
2. **Música** por MCI `cdaudio` (Red Book → `MUSIC/Track{1..4}.wav`, **44100 Hz estéreo**,
   `render-options.md` §7.2), con `from/to` TMSF y loop.

winx86 ya emula la capa COM de DirectSound (`ds_emul.cpp`) y trae un sink `sceAudioOut`
(`platform/vita_audio.cpp`), pero **no reproduce la música** (D2Vita usa Storm/codecs propios; Hot
Wheels usa MCI). Hace falta un backend que:
- mezcle los buffers PCM de DirectSound con el stream de música,
- entregue al `sceAudioOut` a **48000 Hz** (la tasa que exige el puerto MAIN),
- **no bloquee el hilo del guest** (`sceAudioOutOutput` bloquea; el juego es monohilo),
- tenga un backend host que **escriba WAV** como evidencia offline (patrón D2Vita).

## Contrato observable
Módulo `hw::audio` en `hotwheels/src/audio/`, portable host/Vita salvo `vita_sink.cpp`:

### `pcm.h`
- `constexpr int kSampleRate = 48000;` `constexpr int kChannels = 2;` (S16 intercalado).
- `struct PcmSource { virtual int read(int16_t* out, int frames)=0; virtual int rate() const=0; }`
  — `read` devuelve frames estéreo producidos (0 = sin datos por ahora/EOF).
- `int16_t clamp_s16(int v)`.

### `mixer.h` — mezclador con resampleo
- `class Mixer`:
  - `void add(PcmSource* s, float gain = 1.0f)` / `void clear()`.
  - `int mix(int16_t* out, int frames)`: produce `frames` estéreo a `kSampleRate`, resampleando
    **cada** fuente desde `s->rate()` con interpolación lineal (16.16 o double), suma con ganancia y
    satura a `[-32768, 32767]`.
  - Invariante: `mix()` **nunca** produce más de `frames`; una fuente sin datos aporta silencio.

### `wav.h` — lectura streaming + escritura (host)
- `class WavWriter`: `open(path, rate=kSampleRate, ch=2)`, `write(pcm,frames)`, `close()`; RIFF/WAVE
  PCM 16-bit; refresca el header cada 64 KiB para que un run cortado quede legible (igual que
  `audio_sink_host.cpp`). `const char* name()` = `"wav"`.
- `class WavFileSource : PcmSource`: abre un `.wav`, parsea `fmt `/`data` (PCM 16-bit, 1–2 ch),
  `read()` **streamea** desde archivo (no carga el archivo entero: las pistas pesan ~50 MB),
  convierte mono→estéreo; `rate()` = tasa del archivo; soporta `stop_frame` (0 = sin límite/loop).

### `music.h` — reproductor MCI
- `class MusicPlayer : public hw::winmm::MusicSink, public PcmSource`:
  - `play(int track, const std::string& path, uint32_t from_tmsf, uint32_t to_tmsf)`: resuelve
    `path` con un `Resolver` inyectado, abre el WAV, convierte TMSF→frame y arranca. Si el WAV no
    existe o no es PCM 16-bit: **silencio, no excepción ni bloqueo** (CA11 de HW-015).
  - `stop()` cierra; `pause()` deja de producir conservando posición.
  - `read()`/`rate()`: delega en el `WavFileSource` mientras `playing && !paused`.
  - `using Resolver = std::function<std::string(const std::string& win_path)>;` (tests inyectan uno
    falso; en el port resuelve por el `Vfs` de HW-006).

### `dsound_bus.h` — bus de efectos
- `class DsoundBus : public PcmSource`: ring S16 estéreo con `push(const int16_t*, frames, int rate)`
  (productor = adaptador del sink de `ds_emul`) y `read()` (consumidor = `Mixer`). `rate()` = la del
  productor. Es el "buffer DSound" que mezcla el `Mixer`.

### `engine.h` — integración
- `class Engine`:
  - `Engine()`; `~Engine()`.
  - `bool start(const EngineConfig&)`: crea sink (`WavWriter` host / `VitaSink` Vita), arranca el
    hilo (Vita) o deja el pump manual (host).
  - `MusicSink* music()`: para cablear `hw::winmm::Cdaudio` (HW-015).
  - `DsoundBus& dsound()`: para el adaptador de `ds_emul`.
  - `void pump(int frames)`: mezcla `frames` y los manda al sink — usado por los tests host y por
    `frame_pump` (el reloj del guest decide cuántos frames).
  - `void stop()`.
  - `EngineConfig { int out_rate = kSampleRate; std::string wav_path; bool vita = false; }`.

### `vita_sink.cpp` (solo `__vita__`)
- `class VitaSink`: `sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, grain, 48000,
  SCE_AUDIO_OUT_MODE_STEREO)` (fallback BGM), hilo propio (ver Diseño). Fuera de `__vita__`,
  `make_vita_sink()` devuelve `nullptr`.

### `ds_bridge.{h,cpp}` + `integration/dsound_bridge.{h,cpp}` — integración con `ds_emul` (HW-020)
- `hw::audio::ds_backend_start(EngineConfig)`: arranca el Engine global (host WAV / Vita sceAudioOut).
- `hw::audio::ds_backend_feed(pcm, frames, src_rate)`: mete el PCM de efectos en `DsoundBus`; en host
  además mezcla+vuelca (`pump`), en Vita sólo encola (el hilo del Engine saca por `sceAudioOut`).
- `ds_backend_music()`: devuelve el `hw::winmm::MusicSink*` para el MCI/cdaudio de HW-015.
- `hw::integration::hw_register_dsound(br, cpu, DsoundConfig, err)`: instala `d2rt::dsound::install`
  (winx86) con `HostOps{va_alloc, tick_ms, write_root, mix_rate=22050}` y **define** los símbolos
  `d2rt::audio::{make_wav_sink,make_null_sink,make_vita_sink,thread_*,wav_check}` para enrutar el sink
  de ds_emul al Engine (los `.o` del port van antes de `libwinx86`). Llamar tras los stubs/ddraw,
  antes de `br.link()`.
- `hw::integration::hw_dsound_music()`: sink para cablear `Cdaudio(MusicSink&)` de HW-015.
- Handoff del punto de enganche en `integrate.cpp` (de integ): `coordination/integ.md`.

## Criterios de aceptación (cada uno ⇒ un test)
Test host `hotwheels/tests/test_audio.cpp` (runner `tests/run_audio.sh`; no requiere vitasdk):
- [ ] **CA1** `Mixer` suma dos fuentes con ganancia y **satura** a ±32767; una fuente vacía no
  rompe ni altera a la otra.
- [ ] **CA2** Resampleo 22050→48000 con rampa conocida: `out[n] ≈ in[p]`, `p = n*22050/48000`,
  interpolación lineal (tolerancia ±2 LSB); 22050 frames de entrada → 48000 de salida (±1).
- [ ] **CA3** `WavWriter` escribe un RIFF/WAVE válido: `rate=48000`, `ch=2`, `bits=16`, y
  `data` == `frames*4`; reabrir y verificar el header.
- [ ] **CA4** `MusicPlayer` implementa `MusicSink`: `play(2, "MUSIC\\Track2.wav", …)` con un
  `Resolver` que devuelve un WAV sintético; `read()` produce señal; `pause()` la corta; `stop()` la
  cierra. Path inexistente ⇒ `play` no lanza y `read()` da 0 (silencio).
- [ ] **CA5** `Engine` end-to-end host: `DsoundBus::push` de un tono 22050 + `MusicPlayer` de un WAV
  44100; `pump()` → WAV de salida con señal, sin clipping y con aporte de **ambos** buses (se
  compara contra music-only y ds-only).
- [ ] **CA6** `resample_ratio(22050,48000)` / `resample_ratio(44100,48000)` documentados y usados;
  el WAV de salida SIEMPRE es 48000 aunque las fuentes sean 22050/44100.
- [ ] **CA7** Seam de integración (`tests/run_ds_bridge.sh`): alimentar `ds_backend_feed` con 1 s de
  efectos 22050 + `ds_backend_music()->play(...)` produce un WAV 48000 **no vacío** (rms > 0).
  En host `ds_backend_start(vita=true)` falla (no finge tener un device).

## Diseño (bajo nivel: memoria, threads, ABI, costos)
- **Un solo puerto, software mixer @48000.** Decisión: el `Mixer` combina DSound + música y sale por
  **un** `sceAudioOut` MAIN a 48000. Alternativa descartada (dos puertos, `render-options.md` §7.2):
  el hardware mezclaría sin CPU, pero ata la mezcla a 2 puertos y la música (44100) y los efectos
  (22050) no comparten reloj; con un mixer propio el control es único y testeable en host. **Riesgo:**
  resamplear 22050→48000 cuesta CPU; se mide granularidad 512 (~10.7 ms) y se reserva USER_1.
- **Hilo dedicado `hw_audio`**: `sceKernelCreateThread("hw_audio", body, 0x10000100, 64*1024, 0,
  0)`; **auto-pin** en su primera instrucción a **USER_1** (`0x00020000`) por default, configurable
  `HW_SONCPU=1|2` (`0x20000`/`0x40000`). Nunca USER_0 (ahí corren los runners del guest). Prioridad
  **relativa** `0x10000100` (ventana legal `DEFAULT-32..DEFAULT+31`). El hilo hace:
  `mixer.mix(grain) → sceAudioOutOutput(port, buf)`. La llamada bloqueante vive **sólo** en ese hilo.
- **Guest nunca bloquea**: en Vita el guest sólo hace `DsoundBus::push` (no bloqueante) y arranca/para
  MCI; el `write` del device ocurre en `hw_audio`. En host no hay hilo: `pump()` es empujado por el
  reloj del guest (WAV con duración = tiempo de juego aun si qemu corre lento, patrón D2Vita).
- **Resampleo**: 16.16 fixed-point para el mapeo de posición (`pos += srcRate<<16 / dstRate`) y
  interpolación lineal con el residuo; el `Mixer` mantiene un buffer de 4096 frames por fuente y
  conserva el último frame para continuidad entre bloques.
- **Latencia/grano**: `grain=512` frames @48000 ≈ 10.7 ms; doble buffer del `sceAudioOut` ≈ 21 ms.
- **Starvation/errores**: contar `sceAudioOutGetRestSample` (0 = hambre) y RC negativos de
  `sceAudioOutOutput` (si el driver rechaza, el bloqueo desaparece y el hilo gira: anti-busy-wait con
  `sceKernelDelayThread(grain_ms)` tras 4 fallos consecutivos). Volumen a 0 dB, reafirmado.
- **Teardown**: bajar flag → join (capped) → recién ahí `sceAudioOutReleasePort`; nunca cerrar el
  puerto con el hilo todavía bloqueado en `sceAudioOutOutput` (patrón `ds_emul::shutdown`).
- **Integración con `ds_emul`**: el engine provee las definiciones de
  `d2rt::audio::make_vita_sink()` / `make_wav_sink()` / `thread_*` en `src/audio/` (reemplazan a las
  de winx86 `platform/` + `runtime/audio_sink_host.cpp` por **orden de enlace**, los `.o` del port
  van antes que `libwinx86_vita.a`). `HostOps.mix_rate = 22050` (tasa real del juego); el resampleo a
  48000 es del sink del port. **Plan B si el override resulta frágil:** parche aditivo a winx86
  `HostOps::sink_factory` (+ `hotwheels/patches/winx86-000N-*.patch`), con autorización del
  orquestador. `timeGetTime` para `HostOps::tick_ms` lo aporta HW-006 (`GuestClock`).
- **Música**: `Cdaudio(MusicSink&)` de HW-015 se construye con `engine.music()`; el player resuelve
  `MUSIC\TrackN.wav` con el `Vfs`.

## No-objetivos
- Emular el codec Storm/ADPCM de D2Vita (Hot Wheels usa WAV PCM).
- DirectSound3D, captura, streaming de efectos (el juego no los usa).
- Resampleo de alta calidad (sólo lineal; lineal vs sinc se decide si hay artefactos en consola).
- Validar en Vita3K (no hay audio ALSA/Pulse en el contenedor; ver `docs/vita3k.md`). CA Vita queda
  como build + prueba posterior en consola (no bloquea `done`).
- Modificar `ds_emul.cpp` (sólo override de enlace o parche aditivo autorizado).

## Riesgos / preguntas abiertas
- `mix_rate` real de DirectSound del juego (22050 asumido por los SFX; lo confirma HW-012/Wine).
- ¿El puerto MAIN acepta 48000 siempre? RetroArch `psp2_audio.c` lo usa así; fallback BGM.
- Override de símbolos vs parche a winx86 (ver arriba).
- ¿La música debe loopear? `render-options.md` §7.2 dice ~5 min por pista; el player loopeará por
  default hasta `MCI_STOP`/`CLOSE` (TOC real pendiente).

## Evidencia (cómo se valida)
- `[host]` `tests/run_audio.sh` (CA1–CA6) + `tests/run_ds_bridge.sh` (CA7) + WAV de salida verificable (RIFF/tasa/no silencio).
- `[Vita/compila]` `vita_sink.cpp` compila con `arm-vita-eabi-g++` en `tools/build_vita.sh`.
- `[consola]` posterior: `ux0:data/hotwheels/audio.wav` o el puerto sonando; no bloquea `done`.
