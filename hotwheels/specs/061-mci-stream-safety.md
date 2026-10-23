# SPEC-061 — Seguridad del streaming MCI
- Feature: HW-069 · Milestone: M8 · Dueño: fix-luna · Estado: borrador
## Problema
BUG-055 asocia el crash con la música. La hipótesis de leer el WAV completo se contradice con el lector vigente: `WavFileSource::read` usa un buffer local de 4096 frames (16 KiB para estéreo PCM16). En cambio `MusicPlayer::read` comparte `src_`, `playing_`, `paused_` con `play/stop/pause` sin sincronización, y ejecuta `fread` desde el hilo del mixer.
## Contrato observable
Play/stop/pause y pulls del mixer pueden concurrir sin data race ni uso de `WavFileSource` después de liberarlo. Cada pull lee como máximo los frames pedidos y los chunks de WAV siguen procesándose acotados. El estado observable de música se lee sin carreras.
## Criterios de aceptación
- [ ] CA1: prueba concurrente host de pulls frente a play/stop/pause bajo TSan en `tests/run_audio.sh`.
- [ ] CA2: WAV sintético de al menos 50 MiB se reproduce por pulls pequeños con aumento de RSS <8 MiB; compara muestra a muestra reads irregulares cruzando wrap, en loop y sin loop → `tests/test_audio.cpp`.
- [ ] CA3: suite audio previa sigue verde.
## Diseño (bajo nivel)
En Vita `VitaSink` crea `hw_audio` con pthread; cada `pump` mezcla 512 frames y llama `MusicPlayer::read`. El guest ejecuta MCI en su hilo. Un pthread productor separado (stack solicitado 64 KiB) es dueño de `WavFileSource` y hace `fread` de bloques de hasta 1024 frames (4 KiB). El ring fijo almacena 8192 frames estéreo (32 KiB; ≈186 ms a 44,1 kHz), sin crecimiento con el WAV. `play/stop` serializan ciclo de vida con `command_mutex`; el productor lee fuera del lock, publica frames con `mutex`/condition variable y `stop` señala, despierta y hace join antes de destruir el source. El worker no toca el guest, GIL ni buffers DSound.

`read` no hace I/O, pero toma `std::mutex` para copiar como máximo el bloque solicitado (normalmente 512 frames/2 KiB en Vita; cota de `Mixer` 8192 frames/32 KiB). Ese mutex no tiene herencia de prioridad; si el productor queda preemptado dentro de la copia, el hilo de audio puede esperar esa sección crítica. El productor no configura afinidad/prioridad y hereda el contexto pthread del caller; esto no está medido en Vita. `play` espera la primera publicación (I/O de hasta 4 KiB) en el hilo guest, posiblemente con GIL; `stop` hace join y puede esperar la lectura activa. No hay buffers compartidos con GPU ni memoria guest en este camino.
## No-objetivos
No rediseñar el mixer como productor/consumidor ni cambiar el formato WAV o la mezcla.
## Riesgos / preguntas abiertas
El mutex evita carrera/UAF, pero no prueba que el crash de consola sea exclusivamente esa carrera; dumps existentes no son parseables. El costo de I/O síncrona en el hilo de audio sigue abierto.
## Evidencia
Estático + host/TSan; la consola sólo con el protocolo y aviso descritos en el brief, no se usa durante esta tarea.
