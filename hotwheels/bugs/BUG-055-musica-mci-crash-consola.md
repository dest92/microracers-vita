# BUG-055 — Con MUSIC/ presente el juego crashea en el menú en consola real
- Severidad: crash · Estado: fix de carrera host; causa de consola aún no confirmada · Feature: HW-069 · Dueño: fix-luna · Nivel: consola (síntoma, 2026-10-22), host/estático (fix, 2026-10-22)
## Síntoma
VPK HEAD `b5e3fda` (sha256 VPK `5d00bd81…`) + `vita_deploy.py` que ahora sube `Track*.wav` (~50 MB c/u) a
`ux0:data/hotwheels/MUSIC/`. El juego llega al menú, `audio.log` registra `music: MUSIC\Track2.wav -> ux0:data/hotwheels/MUSIC/Track2.wav`
y a ~16 s (present frame ~700) se corta. Dos dumps `psp2core-1792710849/1792710883-…psp2dmp.tmp` truncados (no parseables).
## Discriminante
Renombrar `MUSIC/` → `MUSIC.off` en la consola: el usuario jugó una carrera completa en OFFICE sin crash.
## Hipótesis (sin verificar)
Ruta de streaming MCI de WAV grandes en Vita: lectura/buffer/memoria, o hilo de música vs mixer. Vita3K con música no crasheó
(smoke de cc-audio), así que es sólo-hardware o depende de tiempos.

## Análisis y fix host (2026-10-22)
El código actual ya streamea: `WavFileSource::read` usa `int16_t tmp[4096*2]` (16 KiB) y no carga el payload de ~50 MiB.
El MCI va por `mciSendCommandA` → `Cdaudio::send_command` → `MusicProxy` → `MusicPlayer`, sin pasar por `CreateSoundBuffer`.
En Vita, el `VitaSink` dedica un pthread `hw_audio` que bombea 512 frames; `Mixer::more` llama `MusicPlayer::read`, que
antes hacía `fread` del WAV en ese hilo. En paralelo el hilo guest ejecuta `play/stop/pause`, así que `src_` podía
destruirse durante `WavFileSource::read`. Ése es el punto de crash concreto que sí reproduce el host: ASan mostró
**heap-use-after-free** en `WavFileSource::read`/`MusicPlayer::read` con control concurrente. No hay stack de consola
parseable que confirme que sea la causa del corte a 16 s.

Fix: el pthread productor posee `WavFileSource` y lee chunks de máximo 1024 frames; el `hw_audio` consumidor sólo copia
del ring de 8192 frames (32 KiB). `play/stop` serializan comandos y `stop` despierta/hace join al productor antes de
destruir el source. El productor duerme si el ring está lleno. Stack solicitado: 64 KiB. El decoder no pasa por DirectSound
ni su límite de 4 MiB de `CreateSoundBuffer`.

Pruebas host: `tests/run_audio.sh` ejecuta ASan y TSan (TSan no-PIE bajo `setarch -R`); ambas PASS. Dos mutantes de wrap
del ring (offset de escritura y segunda copia de lectura omitida) mueren en CA16, que compara muestras exactas de WAV rampa
por pulls irregulares cruzando wrap en loop y sin loop. Un WAV sparse lógico de 50 MiB se reprodujo desde `MusicPlayer`
con pull de 512 frames; `ru_maxrss` no creció 8 MiB o más. Es medición host, no Vita. Build Vita PASS en el VPK
1.965.120 B (no instalado). `read` toma mutex para copiar hasta 32 KiB, sin prioridad heredada; `play` espera el primer
chunk en hilo guest y `stop` puede esperar I/O de un chunk activo. Son límites explícitos y no medidos en consola. Sin
dump parseable ni ejecución con música en consola, BUG-055 queda abierto. Diseño: `hotwheels/specs/061-mci-stream-safety.md`;
re-review independiente pendiente.
