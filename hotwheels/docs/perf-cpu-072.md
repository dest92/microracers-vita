# HW-072 — costo guest/dynarec por instrucciones ARM ejecutadas (SPEC-053)
- Dueño: perf-cpu · Fecha: 2026-10-21 · Estado: herramienta lista; ventana de carrera bloqueada por el entorno (abajo).

## Método y por qué instrucciones ARM
qemu/Vita3K no predicen tiempo en Cortex-A9 (el A/B de HW-052 dio neutro porque qemu emula VFP por software) y el
ms de CPU por pata lo mueve la carga del host. La métrica que sí transfiere es el **conteo de instrucciones ARM**:
el binario ARM bajo `qemu-arm` es el mismo que correría en la A9 (runtime del port + bloques que **genera el dynarec**),
y qemu registra cada TB ejecutado (`-d exec`) con sus bytes (`-d in_asm`). Contar `bytes/4` por ejecución da el número
exacto de instrucciones ARM, independiente del host (`tools/qemu_insn_count.py`, SPEC-053 CA1/CA2).

`-d nochain` es obligatorio: con encadenamiento directo la transición TB→TB no pasa por el bucle de TCG y **no se
registra**, subestimando justo los bucles calientes del JIT. `nochain` no cambia la semántica (sólo desactiva la
optimización de salto). El conteo se hace **fuera** del guest: no se parchea winx86 ni el port (el contrato de
D2_JITPROFILE prohíbe tocar el codegen).

Detalle que obligó a corregir el parser (commit `5f53e9a`): **la dirección de un TB no identifica un bloque**. El
cajón de código del dynarec reusa direcciones (misma dirección, otro bloque y otro tamaño más tarde). Cada ejecución
se cuenta con el tamaño vigente en ese momento; la primera versión abortaba ante ese caso y, al cerrarse el pipe,
bloqueaba a qemu (2 frames en 12 s).

## Medición real (qemu, ventana de 60 s, boot/carga)
- Build: worktree `.wt-perfcpu` @ `5f53e9a`; `hotwheels-host` ARMv7 hardfloat; imagen `hw-host-arm:24.04`; qemu 8.2.2.
- Comando: `tools/arm_insn_measure.sh 60 hotwheels/out/arm-insn-menu` (nochain+exec+in_asm, `HW_QPC_IDLE_US=300`).
- Resultado: **421.591.745 instrucciones ARM**, 64.188.599 TB ejecutados, 31.359 direcciones (nivel: qemu-arm).
- Top por dirección, simbolizado contra el ELF exacto (`out/arm-insn-evidence/count-menu-60s-tb.txt`):

| # | dirección | instrucciones | qué es |
|---:|---|---:|---|
| 1 | `0x000428b8` | 49.185.120 | `hw::ddraw::PngPresenter::present` |
| 2 | `0x00041f28` | 44.277.336 | `hw::ddraw::crc_update` (CRC del PNG) |
| 3 | `0x0004277c` | 31.964.465 | `PngPresenter::present` |
| 4 | `0x4dd06a88` | 30.291.426 | **código generado por el dynarec** (arena alta; símbolo n/d) |
| 5 | `0x0006a004` | 23.460.060 | `winx86::dx::audio::Mixer::sample` |
| 6 | `0x003577bc` | 8.278.632 | `memset` |
| 7 | `0x0004332c` | 7.378.146 | `ddraw::Device::blt_fast` |

**Artefacto de medición (no es costo de Vita):** el escritor PNG del port (present + CRC + adler + blt) domina este
config; en consola no existe (render = vitaGL). El audio mezcla en su propio hilo durante toda la ventana. El código
**del guest traducido por el dynarec** aparece como una sola entrada de 30,3 M (7 %), porque el conteo por dirección
agrupa todo el JIT.

## Limitación medida: la ventana de CARRERA no es alcanzable así
`-d exec` imprime ~1 línea por ejecución de TB (~1,2 M/s) y **frena y perturba** al guest ~200×: sin trace, 12 s
producen 286 frames; con trace, 60 s producen **9 presents** (y el juego queda en un lazo de arranque, sin avanzar).
El guion de entrada navega **por frame** y la carrera llega tras ~1.200 frames ⇒ serían horas de trace.
Conclusión (nivel: qemu): el conteo sirve para ventanas sin *pacing* (boot) o con un contador fuera de qemu; para la
carrera hace falta **(a)** un qemu construido con plugins TCG (`-plugin libinsn.so`), **(b)** un contador del lado del
dynarec, o **(c)** la telemetría de consola de la Fase 2. Esta build de qemu **no** trae plugins (`-d plugin` ausente).

Contador del dynarec (propuesta, no implementada): con `WX86_CHAIN=0` todo bloque entra por `DBGetBlock`; en el sitio
que D2_JITPROFILE ya muestrea 1/64 se podría acumular `db->size` por EIP guest ⇒ **instrucciones ARM ejecutadas por
función a velocidad de carrera**, sumando sobre el total muestreado. Es aditivo en winx86 y necesita su prueba
diferencial.

## Top del perfil EIP en CARRERA (>3 %) — causa y propuesta (nivel: qemu, HW-051 `docs/guest-profile.md`)
El reparto por función viene del perfil fiel de HW-051 (muestreo del PC host). Para cada >3 %:

| función | % | por qué es caro (mecanismo) | propuesta / estado |
|---|---:|---|---|
| `__ftol` `0x4521c4` | 6,06 | Conversión x87 ST0→int (`FISTP m64`): en el dynarec cruza una frontera de bloque y llama al helper `arm_fist64_3` (soft-float `__aeabi_d2lz` + saturación), con sincronización de la pila x87 (TOP/caché VFP) al entrar y salir. | **Hecho**: winx86-0008 inlinea `FISTP` con `VCVT.S32.F64` para RC=truncar → −4,6 % CPU/frame en qemu (sombra: 18,73 M casos, 0 diferencias). En A9 debería rendir más (el helper usa `trunc()`+saturación vs VCVT en HW). **Contrato del HLE de patrón completo cerrado (SPEC-055 CA1)**: el control word queda **neto nulo** (`FSTCW`+`FLDCW` de restauración) ⇒ se puede reconocer la secuencia entera; y el `#IA` del `FISTP` **no lo observa nadie** (401 sitios de llamada auditados: 0 con los condition codes del FISTP legibles, 0 que consulten el flag sticky IE) ⇒ el HLE **no tiene que modelar el status word**, sólo el **valor** del borde (`0x8000000000000000`) y el **pop** del stack x87. Ver §"Auditoría del status word". |
| `arm_fist64_3` (helper) | 5,92 | Es el cuerpo del helper anterior; se elimina cuando el camino inline aplica. | Igual que arriba; el caso que cae al helper es NaN / saturación (±2³¹) — con el inline actual, el HLE de patrón completo tiene que **producir el mismo valor** en ese borde (el `#IA` es invisible, el valor no). |
| `FUN_00450af3` (span FT3) | 4,19 | Bucle de span escrito a mano en x86 (textura indexada por bl/bh, `add ecx,ebp`+`adc bl,dl`); se ejecuta por cada span del raster SW. | HLE/AOT medidos **neutros** en qemu (VFP emulado domina) — `docs/hle-prep.md`. Reevaluar en consola con PMU. |
| `swRasterFT3` `0x41d5a0` | 3,70 | Rasterizador SW de triángulos (setup x87 + spans). Cota superior medida con `HW_DIAG_SKIP_FT3`: ≈11–12 % del CPU/frame en qemu. | HLE a nivel triángulo descartado por Amdahl en qemu; candidato real en A9. |
| `timerWaitFrame40Hz` `0x412df0` | 3,03 | Busy-wait sobre QPC hasta 25 ms (`0x412df0`). Bajo el perfil con `HW_QPC_IDLE_US=300` el giro duerme ⇒ este 3 % es con holgura; **sin** yield, en A9 el giro consume hasta ~25 ms/frame de instrucciones (≈11 M a 444 MHz), muy por encima del trabajo útil. | **Hecho**: HW-056 (`HW_QPC_IDLE_US`, `shims/install.cpp`). **Contrato verificado (SPEC-055 CA2/CA3)**: el `dt` que ve el juego es `trunc(delta × 163840/freq)` con umbral `0x1000`; **sin** el sueño el `dt` es **exactamente `0x1000`** (99 % de las esperas) y **con** el sueño de 1 ms **crece** (p50 4207, p90 4340, p99 4673 ⇒ +2,7 %/+6 %/+14 % del frame) ⇒ HW-056 compra ~150× menos traps de QPC a cambio de un `dt` mayor y más disperso. Falta repetirlo en consola (granularidad del sueño y cadencia del guion). Ver §"Contrato de dt". |
| despacho: `DBGetBlock`/`isprotectedDB`/`rb_get_end` | 2,38+1,37+1,32 | Búsqueda del dynablock en el cajón (hash + rbtree) por cada bloque que llega al dispatcher; crece el costo cuando el encadenamiento no cubre la transición. | A/B ya hecho (`WX86_CHAIN=0/1`, HW-034): +34 % frames qemu con direct-link. Caché indirecta direct-mapped **bloqueada** por BUG-045 (vida del puntero). |
| shims del reloj (`GuestClock::qpc/now/take_stats`) | ~1,5 | Cada lectura de QPC del busy-wait es un **trap** al host (cruza el GIL); el juego lee QPC miles de veces por frame. | HW-056 lo ataca por el lado del sueño; un HLE de QPC no cambia la semántica pero evita traps de ida y vuelta. |

Nota: el reparto es **proporción de tiempo muestreado**, no ciclos; para pasar a ciclos se usa
`tools/arm_cost_model.py` con un CPI declarado (el techo de fps = `clock / (instr/frame × CPI)`).

## Contrato de dt del temporizador del invitado (SPEC-055 CA1–CA3)

### Qué calcula el invitado (verificado en el desensamblado, [estático])
`timerWaitFrame40Hz` `0x412df0` (HotWheels.exe, ImageBase `0x400000`):
`actual = QPC(&current)`; `delta = actual − prev` (64 bits con préstamo); `dt = trunc(delta × escala)` con
**escala = `163840.0 / freq`** (`FILLD` de la frecuencia + `FDIVR` de `163840.0` ⇒ `fdivrl`, que divide al revés;
la ruta alternativa por `timeGetTime` usa `163,84`); **repite mientras `dt < 0x1000`** (25 ms); guarda de frame
largo: si `dt > 0xf000` y el `dt` previo era `≤ 0x1000` ⇒ `dt = 0x1000`; al salir `prev = actual`.
El juego llama al temporizador **dos veces seguidas** por vuelta del lazo (`0x411d52`, `0x411d57`) ⇒ **2 esperas
de 25 ms por frame**. Publica el `dt` en `DAT_00468f8c` y el contador previo en `DAT_00468f70`.

### Cómo se midió (fuera del invitado, sin tocar el codegen)
`src/runtime/guest_dt_profile.{h,cpp}`: un hilo del host muestrea cada 1 ms los dos globales del invitado
(`cpu->read_u32`) y arma la **secuencia de `dt` por espera** (fin de espera = cambio del contador previo) con su
histograma y percentiles. El orden de lectura es **contador primero, `dt` después**, porque el invitado escribe el
`dt` **antes** que el contador: así, cuando se detecta el cambio, el `dt` leído ya es el de la espera terminada.
No se escribe nada en el invitado ⇒ la medición no altera lo que mide. Activo con `HW_DT_PROFILE=1`.

### Resultado (qemu-arm, carrera; [qemu]) — `out/hw072-dt-evidence/dt-profile-ab.txt`

| | brazo A: `HW_QPC_IDLE_US=0` (giro continuo) | brazo B: `HW_QPC_IDLE_US=300` (sueño de 1 ms) |
|---|---:|---:|
| esperas detectadas | 9.150 | 7.430 |
| `dt` medio | 4.102,22 | **4.226,46** |
| `dt` p50 / p90 / p99 | **4.096 / 4.096 / 4.096** | **4.207 / 4.340 / 4.673** |
| `dt` = 4.096 exacto | 9.070 de 9.150 (**99,1 %**) | 44 de 7.430 (0,6 %) |
| valores distintos | 53 | 564 |
| traps de QPC en carrera | 42.9k–70.3k/s | **275–381/s** |
| `idles` (sueños) | 0 | 2.360–3.431 por 10 s |

**Conclusión (el dato que faltaba de HW-056).** El mínimo teórico del `dt` es `0x1000` = 4.096 = 25,000 ms exactos.
- **Sin** el sueño, el invitado ve `dt = 4.096` exacto en el 99 % de las esperas ⇒ el reloj del shim y el umbral del
  juego encajan al ciclo.
- **Con** el sueño de 1 ms, el `dt` **crece y se dispersa**: p50 +2,7 %, p90 +6 %, p99 +14 % del frame. La causa es la
  granularidad del sueño: el cruce de los 25 ms ocurre en un múltiplo de ~1 ms, y el último de esos tramos es el que
  excede el umbral.
- HW-056 compra **~150× menos traps al host** (42.9k–70.3k/s → 275–381/s de QPC) a cambio de un `dt` mayor y disperso.
  En la A9 el intercambio es aún más favorable (el giro continuo son hasta ~11 M instrucciones por frame a 444 MHz),
  pero **el `dt` mayor es un costo semántico real**: el juego avanza ~2,7 % menos frames por segundo de lo que
  haría sin el sueño, y la dispersión (564 valores distintos frente a 53) hace que la cadencia **dependa del host** ⇒
  el guion de entrada por frame (`tools/race_enter.script`) ve una cadencia variable.
- **Pendiente para consola (Fase 2)**: repetir el A/B allí, porque la granularidad del sueño y la perturbación del
  scheduler son distintas; el VPK de Fase 2 debería publicar la distribución de `dt` (o al menos p50/p99) junto con
  los guest Hz.

### Límites de la evidencia
- Los números son de **qemu-arm** (host x86-64 emulando ARMv7), no de la A9: la *granularidad* del overshoot depende
  del scheduler del host. El valor del `dt` **mínimo** (4.096) sí es host-independiente (lo fija la aritmética del
  invitado).
- El muestreo (1 ms) puede añadir ~1 preempt del scheduler por espera; como el `dt` mide tiempo de reloj, un preempt
  **agranda** el `dt` ⇒ el sesgo del muestreo tiene el **mismo signo** que el efecto del sueño y es ≪ a lo medido
  (el brazo A, con el mismo muestreador, da 4.096 exacto). El sesgo **subestima** la diferencia, no la infla.
- El `dt = 0` (1 caso de cada brazo) es el valor inicial del global: la ruta de init del invitado escribe `prev`
  antes que `dt`.

## Auditoría del status word x87 para el HLE de `__ftol` (SPEC-055 CA1)
`tools/ftol_status_audit.py` (test: `tests/test_ftol_status_audit.py`, 11 casos) sobre el desensamblado del exe:

```
ftol-status: readers=109 call_sites=401 cc_observable=0 ie_observable=150 cc_checked=150 ie_checked=0
             cw_net_zero=1 ftol_instrs=15
```
- **Control word neto nulo**: `__ftol` guarda el CW (`FSTCW`), fuerza RC=truncar y lo **restaura** ⇒ la secuencia
  completa es equivalente a "truncar y hacer pop" y **se puede reconocer como patrón** en el codegen.
- **401 sitios de llamada** a `__ftol` (35 funciones llamantes). En **ninguno** los condition codes C0/C2/C3 que
  puede dejar el `FISTP` llegan a un lector: siempre hay una comparación x87 en medio (el idioma de MSVC es
  `FCOMP` → `FNSTSW` → `test ah,0x1`/`SAHF`).
- De los 150 sitios con lector reachable, **todos** consultan bits de `AH` (C0/C2/C3) y **ninguno** consulta
  `AL` bit 0 (el flag sticky `IE`) ⇒ **el `#IA` del `FISTP` es invisible para el invitado**.
- Consecuencia para el HLE: **no hace falta modelar el status word**; sí hacen falta el **valor** del borde
  (NaN / fuera de rango ⇒ `0x8000000000000000`) y el **pop** del stack x87 (TOP + tag). Es lo mismo que exige el
  camino inline actual (winx86-0008/0009), así que la regla de patrón completo no agrega riesgo de status.
- Corroboración independiente: `docs/re-hot-functions.md` §5 (REA/Ghidra) llega a la misma conclusión por censo de
  *listing*; este análisis aporta el barrido **por sitio de llamada** (401 sitios, no sólo el del `FISTP` único).

## Entregable conjunto con perf-host (HW-073) para Fase 2 (consola)
`fps-window` ya publica guest Hz, consumer Hz, presents/ventana y mínimo 1 s. Falta para la Fase 2: **ms por fase**
(guest / render / audio) y **ocupación por core**, más la PMU por hilo (HW-033). Coordinar en
`coordination/perf-cpu.md`; correr junto al VPK de diagnóstico de BUG-047 (SPEC-052).

## SPEC-059 — la siesta del giro **sin** mover el `dt` (cierra el costo semántico de HW-056)

El `dt` mayor del brazo B (arriba) era un costo del **mecanismo**, no del objetivo: la siesta de 1 ms era
**incondicional dentro del giro**, así que el último tramo dormido que cruzaba los 25 ms se le sumaba al `delta`.
El fix cede la CPU **mientras el juego no llegó a su deadline** y deja el tramo final en **giro real** (lecturas
finas, como sin siesta): el `QPC` de salida —y por lo tanto el `dt`— es el mismo.

El deadline es un dato del invitado, no una constante adivinada: `dt >= 0x1000` ⇔ `delta >= 25000 µs` desde su
`prev`, que es el global `DAT_00468f70` del temporizador. El motor (`winx86-dx`) recibe el deadline por callback
(`GuestClock::spin_deadline`, en µs del reloj) y sólo duerme si `raw + margen < deadline`; el dueño del juego lo
inyecta en `install.cpp` (`HW_QPC_IDLE_DEADLINE_US=25000`, margen default 2000 µs y ajustable). Sin callback, el
comportamiento es el previo (siesta en cada lectura apretada). Spec: `specs/059-idle-qpc-dt-exacto.md`.

### Medición (qemu-arm, carrera SW, `race_enter.script`, 40 s; evidencia `out/hw072-idle-dt-evidence/`)

`dt` del invitado (`dt-profile`, `0x1000` = 25 ms):

| brazo | esperas | `dt == 0x1000` | media | p50/p90/p99 | distintos |
|---|---:|---:|---:|---|---:|
| base (sin siesta) | 1270 | 97,4 % | 4108,4 | 4096/4096/4403 | 34 |
| `HW_QPC_IDLE_US=300` (legado) | 1246 | 0,7 %¹ | 4197,8 | 4182/4257/4291 | 207 |
| `+ HW_QPC_IDLE_DEADLINE_US=25000` | 1274 | **98,7 %** | **4106,3** | **4096/4096/4183** | **18** |
| igual con margen 200 µs | 1255 | 55,0 % | 4158,7 | 4114/4232/4378 | 184 |

CPU del proceso qemu y traps (30 s por brazo, misma escena; `user+sys`):

| brazo | CPU (s) | Δ vs base | `qpc_calls/s` |
|---|---:|---:|---|
| base | 30,54 | — | 87 k–111 k |
| legado | 25,54 | −16,4 % | 218–286 |
| **con deadline (margen 2000)** | **27,15** | **−11,1 %** | 31 k–34 k |
| con deadline, margen 1000 | 26,14 | −14,4 % | — |
| con deadline, margen 4000 | 29,45 | −3,6 % | — |

¹ cota superior: el histograma del log se corta a 64 valores (el brazo legado tiene 207 distintos).

**Lectura.** El `dt` vuelve a la distribución de la base (p50 = p90 = `0x1000`, media 4106 ≈ 4108) y con menos
dispersión (`18` valores distintos vs `34`), con **−11 % de CPU** frente a −16,4 % del legado: el tramo final en
giro real es exactamente lo que compra el `dt` exacto. El barrido de margen es monótono y `200 µs` ya rompe el
`dt` (55 % exacto) ⇒ el margen tiene que cubrir la siesta real + su jitter; se eligió **2000 µs** como default
conservador (la granularidad del `sceKernelDelayThread` en Vita es mayor y la verificación en consola queda para
la Fase 2). Sigue siendo **opt-in**: sin `HW_QPC_IDLE_US` el VPK usa giro continuo.

## Evidencia
- `hotwheels/out/arm-insn-evidence/count-menu-60s-tb.txt` (resumen + tabla por dirección; build `5f53e9a`).
- `hotwheels/out/hw072-dt-evidence/dt-profile-ab.txt` (A/B de `dt` del invitado: línea `dt-profile` + histograma +
  ventana de traps del reloj, ambos brazos).
- `hotwheels/out/hw072-idle-dt-evidence/` (SPEC-059: `README.md` con las tablas + `*/metrics.txt` (40 s, `dt` y traps)
  y `*/metrics-cpu.txt` (30 s, CPU del proceso qemu), brazos base / legado / deadline / margen).
- `hotwheels/tools/qemu_insn_count.py`, `hotwheels/tools/arm_cost_model.py`, `hotwheels/tools/arm_insn_measure.sh`.
- `hotwheels/tools/ftol_status_audit.py` (auditoría del status word x87), `hotwheels/src/runtime/guest_dt_profile.{h,cpp}`
  (muestreador de `dt` desde el host).
- Tests: `tests/test_qemu_insn_count.py` (6), `tests/test_qemu_insn_oracle.py` (oráculo diferencial exacto),
  `tests/test_arm_cost_model.py` (5), `tests/test_ftol_status_audit.py` (11), `tests/test_guest_dt_profile.cpp` (6 casos).
- Nivel: qemu-arm para conteos y distribución; estático para causa y propuestas; **no hay medición de consola**.
