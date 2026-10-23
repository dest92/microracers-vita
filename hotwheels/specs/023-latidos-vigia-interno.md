# Spec 023 — Latidos por hilo + vigía interno (BUG-035)

## Problema
En consola una instancia quedó quieta en menú tras ~6,600 presents (12:11 reloj
consola) y sobrevivió a `quit all` (BUG-035). `hotwheels.log` y `audio.log` se
detuvieron juntos y no hay timestamps ni estado de hilos: no se puede distinguir
entre deadlock de locks de usuario (vita_gl_mutex, LogBuffer, DsoundBus, GIL),
un syscall que no retorna (vblank/GXM/sceAudioOut/sceIo) o agotamiento de un
recurso acumulado por frame.

Teoría: un proceso que no muere con `quit all` tiene al menos un hilo bloqueado
dentro del kernel (un deadlock puro de mutex de usuario se puede destruir). Si el
último punto de un hilo está *dentro* de un syscall y su edad crece, ese syscall
es el sospechoso; si está esperando un mutex, el dueño es el otro hilo en estado
"dentro". Una fuga se ve como memoria libre decreciendo por frame.

## Contrato
1. `src/runtime/heartbeat.h` (header-only, sin locks ni heap): slots fijos
   (guest DDraw present, hilo interp60, audio, sink del logger). `mark(slot,
   point, now_ms)` guarda punto/tiempo/contador con atómicos relajados. Punto
   impar = dentro de una llamada potencialmente bloqueante; par = fuera.
2. `stalled(slot, now, thr)`: contador > 0 y (slot periódico, o punto impar) y
   edad > thr. `format()` arma una línea `hb t=… slot=punto@edad_ms#contador`
   con `*` en los trabados, en un buffer del llamador.
3. Vita: hilo vigía (prioridad por defecto, duerme 1 s) que evalúa; cada 30 s y al
   detectar un trabado nuevo (umbral 5 s) agrega a `ux0:data/hotwheels/hb.txt`
   la línea + memoria libre (kernel user/cdram/phycont y vitaGL RAM/VRAM) con
   sceIoOpen/Write/Close directo, nunca vía LogBuffer. Al arrancar rota `hb.txt` → `hb.prev.txt` (evidencia del cuelgue previo).

## Criterios de aceptación
- Test host: formato, detección (periódico por edad; aperiódico sólo con punto
  impar), sin falso positivo antes del primer latido.
- Consola: `hb.txt` crece cada 30 s en menú; build Vita y run_all verdes.

## No-objetivos
- Corregir el cuelgue (se hará con la evidencia que dé esta instrumentación).
