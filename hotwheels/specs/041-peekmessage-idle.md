# HW-064 — USER32 `PeekMessageA` idle seam

## Problema y alcance

En menú/carga el loop de WinMain sondea `PeekMessageA` cerca de 17.000 veces/s; la captura qemu de Opus contó 259.507 llamadas en unos 15 s. En carrera son aproximadamente una por frame. El shim genérico devuelve de inmediato cuando la cola está vacía, así que el guest conserva el core y el GIL mientras gira.

Agregar al shim USER32 de `winx86-dx` un callback host invocable sólo cuando `PeekMessageA` encuentra la cola vacía. Hot Wheels puede activarlo mediante configuración opt-in y ceder el hilo guest brevemente; la librería no fija umbral, duración, API Vita ni política de scheduler. La ruta desactivada conserva la conducta actual.

## Contrato observable

- `PeekMessageA` mantiene FIFO, `PM_REMOVE`, bytes del `MSG` y retorno Win32 existentes.
- Al hallar un mensaje no llama al callback. Al hallar la cola vacía lo llama una vez si el servicio fue configurado; sin servicio no llama nada.
- El callback es síncrono en la hebra guest y puede liberar/re-adquirir el GIL antes de retornar. La cola sigue siendo de dueño guest/GIL: ningún productor externo escribe en ella sin una mailbox/sincronización aparte.
- Hot Wheels configura la cesión sólo con `HW_PEEK_IDLE_US=N`, con `0 < N < 100000`; fuera de ese rango queda apagada. El adapter conserva el timestamp monotónico de la última lectura vacía (tomado después de una espera): si la siguiente lectura vacía ocurre dentro de `N` µs, cede 1 ms y actualiza el timestamp al volver. Así, un bucle continuamente ocupado duerme en cada vuelta después de detectar el polling; trabajo que ya tarda al menos `N` µs entre sondeos no duerme. Un mensaje disponible evita el callback y se retorna en esa llamada.
- El callback y cualquier estado que capture viven al menos tanto como los thunks registrados. El shim no crea ni posee hilos, no toma locks y no guarda punteros guest.

## Arquitectura, ownership y concurrencia

El shim genérico conoce `MsgQueue` y `BaseServices`, no `wx86_wait_sleep`, variables de entorno, prioridades/afinidad Vita ni reglas del juego. `BaseServices` entrega el callback; el campo `empty_peek` se agrega al final del aggregate y queda vacío por defecto, preservando inicializadores posicionales existentes. El adapter Hot Wheels decide si habilitarlo y cómo ceder el scheduler. El callback no debe reentrar en imports del guest ni acceder concurrentemente a `MsgQueue` mientras libera el GIL.

El guest corre en el hilo winx86 que adquiere el GIL por trap. La espera será cooperativa y breve (1 ms solicitado por la integración Hot Wheels); al dormir, el scheduler winx86 puede ejecutar otras hebras guest, que podrían entrar al mismo callback mientras la primera hebra espera. El timestamp compartido del adapter será atómico y monotónico; sólo publica un escalar, así que operaciones relaxed bastan y no requieren barrera DMB de publicación. No se crea un hilo nuevo ni se cambia afinidad/prioridad de los tres cores de usuario. No hay lock nuevo ni ciclo de adquisición que pueda formar deadlock Coffman. El scheduler del host no garantiza un plazo máximo: 1 ms es la duración solicitada, no una cota dura de latencia para procesar mensajes.

La hora monotónica/umbral sólo vive en el adapter. No se consultan contadores del guest ni se altera QPC/`timeGetTime`. Es memoria host del callback; los structs PE32 guest y su espacio de direcciones no cambian. Las operaciones de `MsgQueue` siguen serializadas por GIL; el timestamp puede compartirse entre callbacks que se solapan durante una espera y se coordina exclusivamente con el átomo relaxed descrito arriba. No hay otra publicación de datos entre hebras en esta ruta. Si en el futuro un host productor toca la cola, debe agregarse una mailbox con sincronización explícita y barreras/atómicos adecuados, fuera de este cambio.

## Criterios de aceptación

1. Test-first del shim: una llamada vacía invoca el servicio exactamente una vez; cola no vacía no lo invoca y conserva bytes/retorno; ausencia del servicio mantiene el resultado previo.
2. `hotwheels/tests/run_all.sh` pasa y `tools/build_vita.sh` enlaza el VPK. En qemu, baseline y opt-in completan el smoke de carrera con `missing=0`, cierre limpio y PNG normalizados; comparar cantidad de frames (diferencia ≤5 % en la misma duración). No se exige coincidencia de hashes por índice porque la fase temporal de animación puede variar con el scheduler.
3. Comparación qemu de menú/carga, con el mismo exe y escena, al menos 3 pares intercalados de 15 s: tasa de `PeekMessageA` vacía cae ≥80 % respecto del baseline medido (~17k/s). Reportar carga del host y CPU del proceso por segundo. En la escena carrera la tasa y `ms CPU/frame` no empeoran más de 2 % (ruido medido/pares).
4. Con mensaje de activación o quit ya encolado, el callback no se ejecuta y el evento se entrega en esa llamada. Un mensaje que llegue mientras el guest cede se observa en el siguiente sondeo; el 1 ms solicitado no es una cota dura porque el scheduler del host puede tardar más. El test cubre el camino determinista con el mensaje encolado; el smoke qemu verifica shutdown y continuidad de frames, sin atribuirle una garantía de latencia en tiempo real.
5. Variable ausente, inválida o cero: comportamiento previo byte/resultado idéntico. No se marca done sin reviews de código y diseño.

## No-objetivos

- Cambiar `WaitMessage`, el scheduler winx86, el timer QPC, las reglas de entrada/mapeo, resolución o modos de pantalla.
- Hacer `MsgQueue` thread-safe o implementar una fuente host de eventos.
- Activar la pausa por defecto o aplicar política específica de Hot Wheels dentro de `winx86-dx`.
- Prometer fps en carrera: la hipótesis medible es reducir CPU/contención en menú y carga.
