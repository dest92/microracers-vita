# HW-038 — métricas DSound para BUG-031

## Problema

El reporte de consola describe audio de carrera muy distorsionado. `DsoundBus` ya cuenta underruns/overruns y el test host de BUG-031 bajó de 5,197 a 3 underruns, pero `audio.log` no expone esos contadores. Sin ellos no se puede separar falta de PCM por jitter del productor, descarte por overrun ni un fallo del dispositivo `sceAudioOut`.

## Contrato observable

- `audio.log` escribe una línea acumulativa de métricas al primer bloque de PCM y luego como máximo una cada 10 s mientras DSound siga recibiendo PCM.
- Cada línea incluye tiempo monotónico, frames PCM entregados y contadores acumulativos de underrun/overrun del bus.
- No se agregan escrituras por frame ni se cambia el tamaño/semántica del ring.
- El muestreo lee contadores bajo el mutex existente del bus; no mantiene locks durante `alog` ni `sceAudioOut`.

## Criterios de aceptación

1. Test determinista del limitador: permite la primera muestra, suprime muestras antes de 10 s y permite una al llegar exactamente al período.
2. `run_audio.sh` conserva BUG-031 verde (≤2 huecos largos, underrun según el fixture) y la nueva suite del limitador pasa.
3. En Vita, una carrera de 30 s con `race_enter.script` crea al menos dos líneas de métricas si hay PCM de efectos; sus contadores son monotónicos y cada línea está separada por ≥10 s. Capturar `audio.log` y `hotwheels.log` con `CONSOLE_BUSY`.
4. Comparar con el último reporte físico del usuario: anotarlo como evidencia cuantitativa de continuidad, sin declarar validación subjetiva de sonido si nadie puede oír esa corrida.

## Razonamiento de sistemas

El productor es el hilo guest y entrega ráfagas al ritmo de sus frames; el consumidor `hw_audio` corre aparte y despierta por `sceAudioOutOutput` cada grano de 512 frames (~10.7 ms a 48 kHz). El mutex del ring serializa `push`/`read`/snapshot y puede causar contención breve; el reporte se hace fuera de esa sección crítica y como máximo cada diez segundos. Una línea durable cada diez segundos agrega E/S acotada y permite atribuir si los silencios coinciden con starvation del productor (`underrun`) o descartes por llenado (`overrun`). El contador mide el bus de efectos, no el canal MCI de música ni la distorsión analógica del dispositivo.

## No objetivos

- Cambiar el algoritmo adaptativo de prefill de HW-036 o la tasa de `sceAudioOut`.
- Afirmar que contadores cero garantizan buena calidad perceptual; la validación de oído requiere escucha humana.
