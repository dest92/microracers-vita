# SPEC-059 — Siesta del giro de QPC que **no** mueve el `dt` del invitado (HW-056 / HW-072)
- Feature: HW-072 (M6) · Dueño: perf-cpu · Estado: en progreso
- Cierra el pendiente que dejó SPEC-055 (§"Costo del sueño en el `dt`") y el P2/P3 de los reviews
  de `4835664` / `17d430f`: HW-056 compra CPU **a cambio de** un `dt` mayor y dependiente del host.

## Problema

`timerWaitFrame40Hz` (`0x412df0`) espera 25 ms **girando** sobre `QueryPerformanceCounter` y publica el
`dt = trunc(delta · 163,84)` en punto fijo 4.12 (`0x1000` = 25 ms). El giro corta cuando `dt >= 0x1000`.
Sin siesta el giro lee QPC cada pocos µs: el cruce de los 25 ms ocurre **dentro de una iteración** y el
`dt` queda pegado a `0x1000` (medido: 99,1 % de 9.150 esperas, [qemu]).

HW-056 (`HW_QPC_IDLE_US`) hace que `GuestClock::qpc()` duerma **1 ms incondicional** cuando la lectura
anterior fue hace `< N µs`. Entonces el reloj avanza **1 ms por iteración** y el último tramo dormido que
cruza el umbral **se le suma al `delta`**: el `dt` sube y se dispersa (medido con `=300`: p50 4.207,
p99 4.673 ⇒ +2,7 %/+14 %; 564 valores distintos vs 53, [qemu], `out/hw072-dt-evidence/dt-profile-ab.txt`).
El `dt` es la unidad de la física y de la animación, y el guion de entrada (`race_enter.script`) avanza
**por frame**: cambiarlo cambia el determinismo.

Respuesta a la pregunta operativa: **¿está activo por default en Vita? NO.** El shim sólo lo arma si
existe la variable de entorno `HW_QPC_IDLE_US` (`hotwheels/src/shims/install.cpp:42`); en Vita las
variables sólo entran por `env.txt` (`src/runtime_vita.cpp:325-337`, `load_env_txt`) y
`apply_console_release_defaults` (`src/runtime/release_profile.h`) **no** la define. Es decir, el VPK que
se corre hoy usa el giro continuo (y el `dt` queda exacto); HW-056 sólo actúa cuando alguien la pide.

## Fix: dormir hasta el deadline menos un margen, y el último tramo en giro real

La CPU se cede **mientras el juego todavía no llegó a su deadline**; el tramo final —el único que decide
el valor del `dt`— lo hace el invitado con lecturas finas, **igual que sin siesta**. Así el QPC que el
invitado lee al salir (y por lo tanto el `delta` y su `dt`) es el mismo; lo único que cambia es cuánto
tiempo de CPU se cede antes.

El deadline **no se adivina**: es un dato del propio invitado. `dt >= 0x1000` ⇔ `delta >= 25000 µs`
contados desde el `prev` del juego, y `prev` es literalmente el global `DAT_00468f70` que el
temporizador actualiza al terminar cada espera. El motor queda portable —no lee memoria del invitado—
porque recibe el deadline como **callback**:

```
// winx86-dx (motor, portable)
std::function<uint64_t()> spin_deadline; // deadline del giro, en µs del reloj; vacío = legado
uint32_t idle_margin_us = 0;             // tramo final que queda en giro real

bool GuestClock::spin_has_room(uint64_t raw) const {
    if (!spin_deadline) return true;                     // legado: dormir dentro de todo el giro
    const uint64_t dl = spin_deadline();
    // Diff con signo sobre 32 bits: el giro dura ≤ 25 ms, así que no hay ambigüedad de módulo 2³²
    // y la comparación es segura al cruzar el wrap del contador de µs.
    return int32_t(uint32_t(dl) - uint32_t(raw)) > int32_t(idle_margin_us);
}

uint64_t GuestClock::qpc() {
    uint64_t raw = now(); ++qpc_calls_;
    if (spin_idle_us && have_last_qpc_ && raw - last_qpc_raw_ < spin_idle_us && idle &&
        spin_has_room(raw)) { idle(); ++idles_; raw = now(); }
    ...
}
```

Consumidor (`install.cpp`): `HW_QPC_IDLE_DEADLINE_US=25000` arma el callback con
`prev = cpu->read_u32(exe_base + 0x68f70)` (el global del temporizador, RVA de
`runtime/guest_dt_profile.h`) y fija el margen (default 2000 µs, ajustable por `HW_QPC_IDLE_MARGIN_US`).

## Diseño y razonamiento de bajo nivel

- **De quién es el hilo y qué se toca.** El giro corre en el hilo del invitado con el GIL tomado; la
  siesta es `wx86_wait_sleep(1)` → `ThreadScheduler::wait(never_event, 1)` → **libera el GIL** (es el
  punto de HW-056). El invitado sigue siendo el único escritor de `dt`/`prev`; el shim sólo decide
  cuándo cede el hilo. No hay sección crítica nueva: el reloj ya es mono-hilo y el callback es
  síncrono, no reentrante, en el mismo hilo.
- **Por qué el deadline es el del juego y no una constante.** El `prev` que el juego usa para su
  `delta` es el mismo que el shim toma como base ⇒ la condición `raw + margin < prev + 25000` es
  exactamente "al invitado todavía le falta más del tramo final para cortar el giro". Ninguna
  inferencia: si el invitado cambia la cadencia (frame lento ⇒ `dt > 0x1000` y sin giro) la condición
  se apaga sola y no se duerme nada.
- **Por qué el margen tiene que cubrir la siesta y su jitter.** La última siesta arranca cuando
  `raw + margin < deadline`. Si `margin` fuese menor que la siesta real (~1,027 ms en qemu: 1 ms
  nominal + jitter del scheduler), esa siesta empezaría a menos de 1 ms del deadline y lo cruzaría ⇒
  el invitado vería `delta = 25000 + jitter` ⇒ `dt = 0x1001` o más. `margin = 2000 µs` deja ~970 µs de
  aire sobre el jitter observado; el invitado hace su salida fina dentro de la ventana de truncado
  (`delta ∈ [25000, 25006,3)` ⇒ `dt = 0x1000`).
- **Lectura de `prev`: una palabra alineada de 32 bits.** Es el dword bajo del contador de µs; la
  comparación es módulo 2³², así que leer sólo ese dword **no puede producir una siesta de más**: si la
  lectura cae en el borde de la actualización o en el wrap (~71 min), a lo sumo se duerme una espera de
  menos. Nunca se escribe nada en el invitado.
- **Costo.** Cada lectura apretada (gap < `spin_idle_us`) consulta el callback: una lectura de memoria
  del host, despreciable frente a la trap que ya costó la lectura de QPC.

## Criterios de aceptación

- [ ] **CA1 — Test host: el `dt` es el mismo con y sin siesta.** En `tests/test_shims.cpp`, con fuente
  falsa, se emula el giro del juego (`dt = trunc(delta·163,84)`, corta en `0x1000`) sobre 6 esperas y se
  comparan tres brazos: (a) sin siesta, (b) siesta legada (sin deadline), (c) siesta con deadline = el
  `prev` emulado + 25 ms. La siesta avanza la fuente `1 ms + jitter` (0..30 µs por llamada). Criterio:
  (a) y (c) `dt == 0x1000` en **todas** las esperas, (b) tiene al menos un `dt > 0x1000` (reproduce el
  defecto), y (c) sigue durmiendo (`idles` ≫ 0). Antes del fix el test **falla**.
- [ ] **CA2 — Legado intacto**: sin callback de deadline el comportamiento de `qpc()` es el de hoy
  (siesta en cada lectura apretada) → los tests A6 existentes siguen verdes.
- [ ] **CA3 — A/B medido en qemu-arm** (`race_enter.script`, mismo árbol): base (sin siesta) vs
  `HW_QPC_IDLE_US=300` vs `+HW_QPC_IDLE_DEADLINE_US=25000`, con `HW_DT_PROFILE=1`. Reporta `qpc_calls/s`,
  `idles`/s y la distribución de `dt` (fracción de `dt == 0x1000`, percentiles, valores distintos).
  Criterio: el `dt` con deadline vuelve a la distribución de la base (≈99 % exacto) y la CPU cedida
  (`idles`) se mantiene en el orden del brazo legado.
- [ ] **CA4 — Docs**: `FINDINGS.md` (hechos + nivel de evidencia), `docs/perf-cpu-072.md`,
  `coordination/perf-cpu.md`.

## No-objetivos

- No se toca el patrón de `__ftol` ni su HLE.
- No se cambia el guion de entrada ni el orden de frames.
- No se busca fps: el objetivo es ceder CPU **sin** mover el `dt`. La ganancia de fps en consola se
  mide en la Fase 2.

## Riesgos

- Si otro código del invitado girase sobre QPC con otro deadline, el shim usaría el `prev` del
  temporizador; con `prev` ya vencido la condición apaga la siesta (degrada a giro puro, no a siesta
  indebida) ⇒ el efecto en otro giro es nulo.
- En Vita la granularidad/jitter de `sceKernelDelayThread` es mayor que en qemu; el margen es
  configurable (`HW_QPC_IDLE_MARGIN_US`) y la verificación en consola queda para la Fase 2.

## Evidencia

- estático (desensamblado del exe vía `docs/re-hot-functions.md` §2.4), host (test de `test_shims.cpp`),
  qemu-arm (A/B con `guest_dt_profile` de SPEC-055 CA2). Cada número con escena, ventana y unidades.
