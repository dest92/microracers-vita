# SPEC-021 — Serializar LogBuffer concurrente

## Problema y contrato

El runtime Vita registra desde el hilo guest/D3D y desde el hilo de presentación/FPS. `LogBuffer::write()` y `flush()` mutan una cadena, temporizadores y contadores compartidos; ambas rutas llaman al mismo objeto. La clase debe aceptar escritores concurrentes sin data races, pérdida/mezcla de registros ni llamadas simultáneas al sink. `flush()` externo queda serializado con `write()`.

## Aceptación verificable

- Una prueba host con varios productores escribe registros críticos identificables y conserva exactamente una copia de cada uno tras los `flush()`.
- ThreadSanitizer no informa data races en la prueba concurrente. Si el runtime no puede reservar shadow memory (fatal de mapping o SIGSEGV antes de entrar a `main`), el runner ejecuta un stress check sin instrumentación que exige preservar todos los registros y no falla el suite por limitación de plataforma; debe informar claramente que usó el fallback. Cualquier fallo después de iniciar `main`, incluida una data race, sí falla.
- Las pruebas funcionales de buffering, límite, timeout, críticas y flush conservan su comportamiento.

## Fundamento de SO y límites

Un mutex protege el estado completo desde el append hasta completar el sink, no sólo `buf_`: separar append y flush permitiría que un writer vacíe datos mientras otro arma el lote o modifica `last_ms_`. Es una sección crítica con espera acotada por el I/O ya síncrono del sink; el lock no debe ser spinlock porque abrir/escribir/cerrar memory card puede dormir. El sink Vita actual no vuelve a llamar a `LOG()`, requisito para no introducir reentrada/deadlock al mantener el mutex durante el flush. Esto no cambia el ABI guest ni el orden de planificación del emulador, pero serializa el I/O de log entre hilos.
