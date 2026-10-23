# SPEC-023 — Preservar bursts cortos en el buffer anti-jitter
- Feature: HW-036 · Dueña: luna · Estado: en progreso (re-review del pull real Mixer 4096 pendiente)

## Problema
`DsoundBus` evita underruns reteniendo hasta 120 ms antes de abrir el consumo. Si un efecto entrega menos de ese umbral y el productor queda inactivo, `read()` devuelve cero indefinidamente: el buffer conserva datos pero nunca reproduce el burst. El test actual sólo usa producción continua y no cubre ese caso.

## Contrato observable
- Con el colchón habilitado, un burst finito menor al umbral inicial debe llegar a la salida después de una espera acotada; nunca puede quedar retenido para siempre.
- Una cola vacía no inicia ni acumula esa espera. Medir tiempo de pared con `steady_clock`, desde el primer PCM luego de vacío/rearme; no inferirlo de `read(frames)`, porque `Mixer::more()` pide capacidad de bloque (4096) y no consumo real.
- La fuente puede retornar 0 temporalmente. `Mixer` debe hacer como máximo un intento vacío por fuente y por `mix()`; el siguiente bloque vuelve a probar. Evita polling por cada muestra y hace estable el coste del reloj.
- El gate vuelve a armarse cuando un read deja el ring exactamente vacío, incluso si devolvió todos los frames pedidos; el burst posterior no hereda el estado primed.
- El patrón sostenido con jitter debe conservar el resultado ya medido: luego del warm-up, como máximo 2 huecos de >24 muestras cero en 10 s y adaptación del colchón tras underrun.
- No agregar allocaciones por `read`/`push`; el acceso concurrente productor-consumidor sigue serializado por el mutex existente.

## Medición y aceptación
- Test host integrado con el `Mixer` real, mix blocks de 512 a 48 kHz y pulls fuente de 4096 a 22.050 Hz. Verificar que pulls vacíos antes del burst no adelantan su salida y que cada bloque mezcla hace como máximo un read vacío. Probar bursts de 100/40/5 ms; provocar adaptación 360→400 ms y repetir 5 ms. Límite ≤450 ms medidos desde `push` hasta primer PCM no nulo (400 ms de colchón + un grano de salida y tolerancia host).
- Repetir `tests/run_audio.sh`: CA1..CA13 y stress de jitter sin regresión (>2 huecos warm / 10 s).
- Evidencia host; el oído/latencia en consola queda para integ y no se infiere desde el test sintético.

## No objetivos
- Cambiar el scheduling del motor, migrar producción a `self_paced`, corregir latencia percibida de todos los efectos ni afirmar calidad audible en hardware.

## Razonamiento de bajo nivel
El ring es una cola SPSC protegida por mutex: el productor añade ráfagas y el hilo de audio drena a tasa fija. El umbral fijo prioriza continuidad (seguridad contra underrun), pero rompe liveness para una cola corta e inactiva. La espera se decide por ocupación o tiempo monotónico transcurrido desde el primer PCM; el estado de un ring vacío no acumula tiempo. `read(frames)` recibe capacidad y no consumo: el mixer almacena hasta 4096 frames, así que usar ese argumento como reloj sobreestima hasta 8× y puede sumar en un loop de una muestra. El latch por `mix()` limita esos intentos y el número de lecturas de reloj. Mutex acquire/release publica PCM; no hacen falta barreras ARM manuales ni cambios ABI.
