# SPEC-055 — Contrato del dt del invitado, espera de 25 ms y status word x87 (HW-072 / HW-056)
- Feature: HW-072 (M6) ·Dueño: perf-cpu · Estado: en progreso
- Complementa SPEC-053 (conteo de instrucciones ARM): aquí se mide la **semántica** del temporizador del
  invitado, no su costo en instrucciones.

## Problema

Dos huecos que la RE de las funciones calientes dejó abiertos (`docs/re-hot-functions.md` §3):

1. **`timerWaitFrame40Hz` (3,03 % del race)** espera 25 ms girando sobre `QueryPerformanceCounter`.
   HW-056 (`HW_QPC_IDLE_US`) hace que el shim de QPC **duerma 1 ms** cuando la lectura anterior fue hace
   `< N µs`. Hasta ahora sólo se midió el efecto en CPU/traps: **nadie midió el `dt` que ve el juego**.
   El `dt` es la unidad de tiempo del juego (física, animación) y el guion de entrada
   (`tools/race_enter.script`) avanza **por frame**: si el `dt` cambia, cambia el determinismo.
2. **`__ftol` (6,06 % del race)** es el candidato #1 de HLE. La RE dejó claro que se puede reconocer el patrón
   completo (`FSTCW`/`OR AH,0xC`/`FLDCW`/`FISTP`/`FLDCW`) porque el control word queda neto nulo, pero
   dejó pendiente una verificación: **¿el invitado lee el status word x87?** Si lo lee, el `#IA` que puede
   levantar el `FISTP` **no es irrelevante** y la regla no puede ignorar el status.

## Contrato del temporizador del invitado (verificado en el desensamblado del exe)

`timerWaitFrame40Hz` = `0x412df0` en `HotWheels.exe` (ImageBase `0x400000`, `.data` en `0x45f000`):

- Init (una vez, `DAT_0045f2a4 == -1`): `QueryPerformanceFrequency(&freq)`; si **falla** → modo `timeGetTime`
  con escala `163,84` (`0x47ae147b_40647ae1`); si funciona → `QueryPerformanceCounter(&prev)` y
  **escala = `163840.0 / freq`** (`FILLD` de la freq + `FDIVR` de `163840.0`, es decir `fdivrl`, que
  **divide al revés**: `ST0 = 163840.0 / freq`).
- Espera: `actual = QPC(&current)`; `delta = actual - prev` (64 bits con préstamo); `dt = trunc(delta × escala)`
  en x87 + `__ftol`; **repite mientras `dt < 0x1000`** (25 ms con escala `0,16384`).
- Guarda de frame largo: si `dt > 0xf000` (375 ms) y el `dt` **previo** era `<= 0x1000` → `dt = 0x1000`;
  si el previo era `> 0x1000` → `dt = dt_previo`.
- Al salir: `prev = actual`; el `dt` final queda en `DAT_00468f8c` (`0x468f8c`) y su copia en
  `DAT_00468f90` (`0x468f90`). El contador `prev` queda en `DAT_00468f70` (`0x468f70`).
- El juego llama al temporizador desde 3 sitios (`0x411d52`, `0x411d57`, `0x412085`); los dos primeros
  son **consecutivos** en el lazo principal → **2 esperas de 25 ms por vuelta del lazo**.

Consecuencia con el shim: `GuestClock::qpc()` (`winx86-dx/src/shims/base.cpp`) devuelve **µs** y con
`spin_idle_us` duerme 1 ms si la lectura anterior fue hace `< spin_idle_us`; el reloj del invitado avanza
**1 ms por iteración del giro** en vez de por nanosegundos → el cruce del umbral de 25 ms ocurre con
granularidad de ~1 ms → el `dt` del invitado **crece** (overshoot) y la vuelta del lazo se alarga.

## Criterios de aceptación

- [ ] **CA1 — Auditoría estática del status word x87** (`tools/ftol_status_audit.py`, test propio):
  sobre el desensamblado del exe, (a) contar los lectores del status word (`FNSTSW`/`FSTSW`),
  (b) confirmar que la secuencia de `__ftol` deja el control word **neto nulo**,
  (c) para cada sitio de llamada a `__ftol`, determinar si el `#IA` del `FISTP` **puede ser leído**
  antes de que otra instrucción x87 sobrescriba los condition codes.
  Salida: número citable de sitios observables/no observables + el intervalo de referencia.
  Criterio de éxito: el parser corre sobre el exe real y el informe nombra el caso de borde
  (`NaN`/fuera de rango → `0x8000000000000000`, `#IA`).
- [ ] **CA2 — Muestreador del `dt` desde el host** (`src/runtime/guest_dt_profile.{h,cpp}` + test host):
  lee `DAT_00468f8c` (dt) y `DAT_00468f70` (contador previo) del invitado y arma la **secuencia de `dt`
  por espera** (fin de espera = cambio del contador previo) + histograma + tasa de esperas.
  Contrato: lectura **fuera** del invitado (no se parchea ni el exe ni el codegen); el lector se inyecta
  para poder testearlo sin invitado; el `dt` se registra **con el valor que el juego usó**.
  Bordes: invitado que no escribe los globales (todo 0) → 0 esperas, sin falso "dt = 0" espurio;
  `dt` fuera de `[0x1000, 0xf000]` → contador de fuera de rango (el invitado puede tener otro valor
  antes de la 1.ª espera).
- [ ] **CA3 — A/B medido en qemu-arm** (`HW_QPC_IDLE_US=0` vs `=300`): distribución de `dt` (media, mín,
  máx, percentiles, histograma), tasa de esperas y diferencia en instrucciones ARM por frame
  (métrica host-independiente de SPEC-053). Conclusión explícita sobre si el sueño **cambia el `dt` que
  ve el juego** y, por lo tanto, el determinismo del guion de entrada.
- [ ] **CA4 — Documentación**: `docs/perf-cpu-072.md` (análisis >3 % de `timerWaitFrame40Hz` y `__ftol` con
  la evidencia nueva), `FINDINGS.md` (hechos con nivel de evidencia), `coordination/perf-cpu.md` (estado).

## Diseño y razonamiento de bajo nivel

- **Quién escribe y quién lee el `dt`.** El hilo del invitado (scheduler nativo) ejecuta el código x86
  traducido; escribe `dt` y `prev` en su memoria. El muestreador es **otro hilo del host** que lee esos
  dos globals. La lectura es una palabra de 32 bits alineada: la carrera es benigna (a lo sumo se
  empareja un `dt` con la espera siguiente). **El orden de lectura importa**: se lee `prev` **antes**
  que `dt`, porque el invitado escribe `dt` **antes** que `prev`; así, cuando se detecta el cambio de
  `prev` en la muestra siguiente, el `dt` leído es el de la espera que acaba de terminar.
- **Frontera de espera.** El invitado actualiza `prev` al **terminar** cada espera (no durante el giro),
  así que un cambio de `prev` marca exactamente una espera terminada. Como las esperas duran ≥ 25 ms y
  el muestreo es de 1 ms, no se pueden perder dos esperas entre muestras.
- **Por qué el muestreador no perturba la medición.** No escribe nada en el invitado ni en su reloj: el
  `dt` que mide es el que el juego usó. El coste del muestreo (dos lecturas cada 1 ms) cae fuera de la
  ventana de la espera salvo que el hilo del invitado sea preemptado por el muestreador; en ese caso el
  `dt` **crece**, y por eso el A/B se hace con y sin muestreador (el control del sesgo).
- **Costo del sueño en el `dt`.** Con `spin_idle_us` el shim duerme 1 ms **por iteración** del giro: el
  cruce de los 25 ms ocurre en un múltiplo de ~1 ms → el `dt` excede `0x1000` en hasta ~163 unidades
  (4,12 → ~4 %). Sin el sueño, el giro es continuo y el excedente es el de **una iteración** (trap + x87 +
  `__ftol`), de orden decenas de µs → el `dt` queda pegado a `0x1000`. La medición lo confirma o lo
  refuta; la hipótesis es **refutable** y queda escrita antes de medir.
- **El status word x87 y el HLE.** El exe tiene lectores del status word (`FNSTSW %ax`, el idioma de
  MSVC para comparar `double`). El `FISTP` de `__ftol` puede levantar `#IA` (NaN o fuera de rango) y
  deja `C0=C2=C3=1` **más** el flag sticky `IE`. Si ningún lector del status word puede ver esos flags
  antes de que los sobrescriba otra instrucción x87, el HLE puede ignorar el status; si puede, el HLE
  debe reproducirlo (o delegar en el helper, como ya hace hoy `winx86-0009`).
- **No-objetivos**: no se modifica winx86 (el shim compartido y D2Vita quedan intactos); no se implementa
  el HLE de `__ftol` (esto lo *prioriza y acota*, no lo implementa); no se afirma fps de consola; no se
  toca el guion de entrada.

## Riesgos

- El invitado puede no haber llegado a la carrera: la ventana medida será la del boot/menú → se declara
  la escena en el informe.
- El reloj del shim es de µs (`kFreq = 1e6`): la escala del invitado da 25 ms exactos
  (`163840/1e6 = 0,16384`; `0x1000 / 0,16384 = 25.000 µs`).
- Si otro código del invitado leyera o escribiera `DAT_00468f70`, el conteo de esperas sería wrong;
  se verifica por desensamblado que `prev` sólo lo escribe el temporizador.

## Evidencia
- estático (objdump del exe), qemu-arm (A/B con la métrica de SPEC-053), y estimación de ciclos declarada
  como tal. Cada número con build/hash, escena, ventana y unidades.
