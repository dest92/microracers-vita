# HW-050 — Diagnóstico de cambios de modo DirectDraw

## Problema

BUG-032 tiene una diferencia entre la sonda inicial 640×480 y los presents de carrera 320×240 en consola, pero el log Vita no identifica cada llamada guest a `SetDisplayMode`. Sin esa llamada no se distingue una elección de resolución del juego de un tamaño de salida alterado después.

## Contrato observable

- `HostOps` expone un callback opcional de cambio de modo con ancho, alto, bpp, aceptación y modo vigente.
- DirectDraw lo invoca una sola vez por `SetDisplayMode`, después de validar la solicitud, y también para solicitudes rechazadas. El callback observa el modo vigente después de la llamada.
- El cableado Vita se compila desde `src/host/integrate.cpp` en `tools/build_vita.sh`; el callback usa `HostConfig.log`, cuyo sink Vita actual persiste la línea `[hw] ...` en `ux0:data/hotwheels/hotwheels.log` (o la envía por UDP si `HW_TRACE_UDP=1`). La instrumentación no cambia el HRESULT ni el estado del modo.
- El test host cubre transición 640×480→320×240 y rechazo 800×480; verifica argumentos, resultado y modo vigente.
- En Vita3K, con `race_enter.script`, la traza muestra solicitudes aceptadas 640×480 inicial y 800×600 de carrera antes de `D3D CreateDevice ... size=800x600`; los presents D3D de esa escena siguen usando ese target. Una solicitud rechazada, si ocurre, deja explícito el modo efectivo anterior.

## No objetivos

- No interceptar acceso a `settings.dat`, resolver la política de saves ni forzar una resolución.
- No inferir causa en consola desde Vita3K. El log sólo provee instrumentación reutilizable a integ.

## Razonamiento de sistemas

El callback corre sincrónicamente en el contexto que despacha el shim DDraw. `Adapter` ya es estado compartido del puente y conserva su disciplina de ejecución actual; este cambio no añade hilo ni acceso asíncrono. La instrumentación depende de que el runtime serialice los shims DDraw como hoy lo hace el dispatcher guest; si esa garantía cambia, el dueño del bridge deberá sincronizar también el estado base. El log se delega por `HostOps` para no acoplar el puente COM al ABI Vita ni al logger host. Los enteros copiados al callback son valores, sin retener punteros guest ni memoria de superficies. Las llamadas rechazadas se registran con el estado previo, lo cual permite distinguir modo vigente de solicitud. El sink Vita existente serializa sus escritores mediante `LogBuffer`.
