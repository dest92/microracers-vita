# SPEC-058 — BUG-054: presupuesto de shutdown por progreso del invitado (no por plazo fijo)

- Feature: HW-073 (perf-host, por asignación del orquestador) · Milestone: M8 · Dueño: perf-host · Estado: borrador
- Insumos: `hotwheels/bugs/BUG-054-shutdown-presupuesto-fijo-bajo-carga.md`, `coordination/brief-comun-m8.md`,
  `hotwheels/src/host/watchdog_shutdown.h`, `hotwheels/src/host/host_main.cpp`,
  `d2vita/D2Vita/third_party/winx86/src/runtime/gil.h`.

## Problema

El apagado del host es **cooperativo**: el watchdog pide `request_shutdown` (bajo el GIL) y el invitado
tiene que alcanzar un **safepoint** (una trampa) para soltar el GIL y observar el pedido. El watchdog
espera con un **plazo fijo de 3 s de tiempo de pared** y, si se cumple, mata el proceso con
`salida forzada`.

Ese plazo mide *starvation del planificador*, no *liveness del invitado*: con el host saturado
(loadavg ≈ 10 sobre 3 cores útiles) el invitado puede no ser despachado durante más de 3 s, y el
watchdog lo mata con el guest sano. Medido por oc-qa (BUG-054): 4 FAIL y 3 OK en 7 corridas **del mismo
sha**, con los FAIL concentrados donde la carga era alta. `missing=0` en todas: no se perdió un frame,
el invitado simplemente no llegó a un safepoint a tiempo.

Es el caso clásico de un *timeout* usado como predicado de liveness: un plazo de espera sobre un recurso
compartido confunde **deadlock** (el invitado nunca más llega al safepoint) con **starvation** (el
invitado es ejecutable pero el host no lo despacha). El predicado correcto es el **progreso**: se renueva
el presupuesto mientras el invitado progresa y sólo se declara el cuelgue cuando no hay progreso.

## Contrato observable

1. El watchdog **renueva su presupuesto mientras el invitado progrese** y fuerza la salida sólo tras una
   ventana **sin progreso**.
2. La señal de progreso es el **contador de adquisiciones del GIL** (`gil::Probe::acq`): el invitado toma
   el GIL en cada trampa de import, y una trampa es exactamente el safepoint donde el planificador puede
   observar el pedido de apagado. Si `acq` avanza, el invitado *vuelve* a un punto donde puede ver el
   pedido ⇒ el apagado va a poder completarse.
3. `probe()` **no bloquea** (no toma el GIL): es obligatorio, porque el GIL es justamente el recurso
   saturado que se quiere observar.
4. El pedido de apagado **se reintenta** mientras no se haya entregado: hoy, si los 100 intentos de
   `request_shutdown_bounded` fallan, el pedido **nunca** se entrega y el invitado no se detiene nunca.
5. Un cuelgue real (el invitado no vuelve a una trampa) **sigue detectándose**: tras la ventana sin
   progreso el watchdog fuerza la salida y el veredicto sigue siendo FAIL.
6. El mensaje de "GIL ocupado" **deja de contener la frase `salida forzada`**: con el reintento el pedido
   puede entregarse más tarde y la corrida puede terminar limpia. La frase queda sólo en la salida
   forzada real, que es la que el veredicto (`smoke_qemu_verdict.sh`) debe detectar.

## Criterios de aceptación (cada uno ⇒ un test)

- [ ] **CA1** La decisión del watchdog es una función pura y testeable: el progreso renueva el presupuesto
      (una ventana sin progreso *no* fuerza la salida mientras el invitado progrese), la ausencia de
      progreso fuerza la salida tras la ventana, la primera muestra es línea base (no es progreso), la
      adquisición **propia** del watchdog (entrega del pedido) no cuenta como progreso del invitado, y
      hay un tope duro que corta aunque el invitado progrese. Test host x86: `tests/test_watchdog_shutdown_gil.cpp`
      (CTest `watchdog_shutdown_gil`).
- [ ] **CA2** El lazo de espera, contra el **GIL real**: un hilo que toma el GIL periódicamente (como un
      invitado que sigue atrapando) mantiene viva la espera más allá de la ventana; un invitado silencioso
      fuerza la salida. Test host x86: mismo test que CA1.
- [ ] **CA3** Cableado en el host: `build_host.sh` compila (ARM) y el smoke de qemu sigue dando veredicto
      OK en una corrida sana. El watchdog **no** fuerza la salida en una corrida sana bajo carga.
      Test: `tools/run_host.sh 20` + `tools/smoke_qemu_verdict.sh` (nivel qemu; el motivo ya imprime
      `load=` del host, `53f6376`+`12f70f3`).

## Diseño (bajo nivel: hilos, memoria, tiempos)

**Hilos.** El watchdog es un hilo *host*detacheado (`host_main.cpp`); el invitado corre en el runner del
`NativeScheduler` (core del host, `USER_1` en Vita). El GIL serializa al invitado contra las llamadas
host→guesto. El watchdog **no** es un hilo invitado: no habla con el invitado salvo por el GIL y por el
contador `acq`.

**Señal de progreso: por qué `acq` y no los frames ni el IP del invitado.**

- *Frames*: `st.frames` (presentaciones) deja de avanzar si el invitado no dibuja (menú estático: el
  heartbeat lo anota, "el guest presenta sólo cuando cambia algo") ⇒ declararía cuelgue a un invitado
  sano. Descartado.
- *IP del invitado* (`cpu->ip_ptr()`, que el muestreo de EIP ya lee cross-thread): avanza en bordes de
  bloque, así que prueba que el invitado *ejecuta*, pero no que vaya a *ceder* el GIL. Un invitado
  girando en un bloque sin imports ejecuta para siempre sin liberar el GIL ⇒ el apagado **nunca** puede
  completarse: eso es un cuelgue y `acq` sí lo detecta (no hay trampa ⇒ no hay adquisición).
- *`acq` (adquisiciones del GIL)*: avanza exactamente cuando el invitado vuelve a una trampa, o sea
  exactamente cuando puede volver a ver el pedido de apagado. Además es la misma métrica que ya usa el
  detector de inanición de winx86 (`arm_famine`, contador de frames) y la misma que documenta
  `gil.h`: "two consecutive samples with the same `acq` counter prove the acquisition has lasted at least
  the interval between the samples".

**Memoria y orden (ARMv7).** `acq` es un `std::atomic<uint32_t>` *relaxed* de escritor único (el tenedor
del GIL): `gil.cpp` lo publica con `load+store` relajados, sin RMW. En ARMv7 un `ldr`/`str` relajado de
palabra alineada es atómico (sin *tearing*) y sin costo. El watchdog lo lee con `probe()`, que es
*lock-free*. No hace falta barrera: el contador es una **pista** de progreso; que se pierda una
actualización sólo retrasa la detección hasta el próximo muestreo (250 ms), nunca la adelanta.

**Entrega del pedido.** `request_shutdown_bounded(ns, 100)` intenta 100 veces tomar el GIL (10 ms entre
intentos). Si falla, **el pedido no se entrega**: el invitado no se detiene nunca y el watchdog sólo
puede matarlo. Por eso el lazo reintenta (1 intento por muestreo) hasta entregarlo. La adquisición
**propia** del watchdog incrementa `acq` (`try_lock` llama a `pub_take`) ⇒ no puede contarse como progreso
del invitado: tras cada entrega se re-Referencia la línea base. Sin ese cuidado, un watchdog que toma
el GIL parecería un invitado vivo.

**Presupuesto contra el reloj del runner.** `run_host.sh` mata la etapa qemu con
`timeout $((SECS + 20))` = 40 s y el invitado ocupa 20 s ⇒ el watchdog tiene 20 s de aire. Ventana sin
progreso = **12 s** y tope duro = ventana + 3 s = **15 s**: entra con 5 s de margen. Una ventana mayor no
entra en el presupuesto del runner. Con el host saturado el invitado recibe CPU en ráfagas de
milisegundos, así que 12 s es holgadamente suficiente para que avance `acq` una vez.

**Scheduler cooperativo (`HW_SCHED=coop`).** No hay GIL ⇒ `probe()` devuelve `active=false` y `acq` queda
en 0 ⇒ no hay progreso ⇒ se fuerza la salida tras la ventana (12 s en vez de los 3 s de antes). El gate
usa el scheduler nativo; el camino cooperativo conserva la semántica (forzar la salida), cambia el plazo.

**Sin cambios en winx86.** `gil::probe()` ya existe y es público: no se toca el GIL, ni su ABI, ni el
código generado. D2Vita comparte el árbol de winx86 y no debe ver regresiones de build.

## No-objetivos

- No se cambia el criterio del veredicto (`tools/smoke_qemu_verdict.sh`): sigue exigiendo `base`,
  `frames>0`, apagado limpio y ausencia de `salida forzada`.
- No se cambia la semántica de apagado del invitado ni el GIL.
- No se promete que el gate sea inmune a la carga: se elimina el FAIL **falso** por starvation; un
  invitado que de verdad deja de progresar se sigue detectando (más tarde).
- No se toca la consola ni el port a Vita: el fix es del host de qemu.

## Evidencia (cómo se valida)

- **Host (x86)**: CTest `watchdog_shutdown_gil` (CA1, CA2) — decisión pura + lazo contra el GIL real.
- **qemu (docker `hw-host-arm:24.04`)**: `tools/run_host.sh 20` + `tools/smoke_qemu_verdict.sh` (CA3); el
  motivo ya imprime `load=` del host, así que la evidencia queda con la carga con que se midió.
- **Estático**: los caminos nuevos en `watchdog_shutdown.h` y `host_main.cpp`.
- **Consola**: no aplica (el watchdog de 3 s es del host de qemu).
