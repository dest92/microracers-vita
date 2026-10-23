# Review sonnet-rev — HW-075 código: `978db12` (hooks audio) + `d19ab13` (diag/thread_cpu.h, runtime, tests); specs `4060c44`/`11e3a79`
**Veredicto: CONFIRMA (primer corte sólo diagnóstico).** Sin hallazgos de corrección ni de concurrencia graves. Un P2 de gate y tres P3. No evalúo perf: el corte no la afirma.

## Reproducido (snapshot de HEAD, sin tocar tus rutas)
- Test host `-O2 -Wall -Wextra -Werror -pthread` PASS; con `-fsanitize=thread` (`setarch -R`) PASS, 0 warnings.
- **Mutantes sobre `thread_cpu.h`:** sin incremento de generación, sin chequeo de generación, sin `CounterReset`, `wall` con `<` en vez de `<=`, `rc` final ignorado, sin `ChangedTid` → **todos mueren**. Cambiar release/acquire por relaxed (CAS y `token`) **sobrevive**: inevitable en x86 (TSO); lo cubro con el codegen (abajo).
- **ARMv7 (arm-vita-eabi-g++ -O2 -mcpu=cortex-a9):** `set()` = `ldrexd` + `dmb ish` + bucle `ldrexd/strexd`; `token()` = `ldrexd` + `dmb ish`. Es la forma correcta de release-CAS / acquire-load de 64 bits (single-copy atomic con LDREXD; el DMB antecede al store, sigue a la carga). `static_assert(alignof(atomic<u64>)==8)` y `is_always_lock_free` pasan. Como el token es el único dato publicado (TID+generación juntos, indivisibles), el ordenamiento no es crítico: relaxed bastaría; release/acquire sobra pero es inocuo.
- Build Vita (tu log `out/hw075-build.log`): VPK 1.967.015 B PASS; no lo reproduje (sin consola/Vita3K).

## Foco obligatorio
- **Runtime/dynarec:** N/A. Sin cambio de codegen ni de traducción; el guest sólo registra su TID al iniciar.
- **GIL/locks:** ninguno nuevo. El muestreo corre en el hilo de métricas (`sceKernelGetThreadInfo` ×5 + tiempo cada ventana de 10 s), fuera de guest/audio/render; sin llamadas por draw/grano. Registrar/retirar es una CAS por vida de hilo, sin locks ni getenv (el opt-in sólo gobierna el muestreo, como explicaste).
- **TID reutilizado / generación:** cada `set()` suma 1 a la generación (también `set(0)`), así retirar y registrar el mismo TID cambia el token; `sample_thread` relee el token **después** de la syscall y descarta (`rc=-1`) si cambió; `thread_delta` invalida por tid, generación, rc, tiempo no creciente y contador decreciente, y nunca convierte error en cero. Correcto.
- **Lifetimes:** `ThreadRegistration` RAII en `th_entry`/`thread_entry`/`decoder_entry` limpia al salir (también en EOF del decoder). Los respawn están serializados por join (Music: `stop_decoder` antes de crear; feeder/dsound por `close`/`stop`), así que no hay dos vidas solapadas por rol. Render se registra en el callback del propio consumer y lo retira `main` tras `shutdown_d3d()` (consumer ya unido). `guest_cpu_registration` se declara antes del `std::thread` de métricas (se destruye después). Inicialización estática: `inline` global con `slots_{}` en almacenamiento estático ⇒ cero.
- **Campos/opt-in:** `HW_THREAD_CPU=1` estricto; sin él no hay filas ni muestreo; `scene=unverified` fijo y `scene_declared` truncado a 80 caracteres; `delta_raw=unavailable` si no es válido; `cpu_us`, `%` y "saturat" ausentes (test).

## Hallazgos
1. **P2 — el test no entra al gate.** Sólo existe `tests/test_thread_cpu.cpp`; no hay `tests/run_thread_cpu.sh` ni entrada CMake, y `run_all` descubre `tests/test_*.sh`/`run_*.sh`. CA1 se cumple a mano pero un regreso futuro no lo ve nadie. Agregar un `run_thread_cpu.sh` (c++ -O2 -Werror + la variante TSan).
2. **P3 — `size=0x0` en cada fila:** `format_thread_window` recibe `width=0,height=0` desde el runtime (no se conoce la resolución ahí). Publicar `0x0` como si fuera dato confunde la lectura del log; omitir el campo o decir `unknown`.
3. **P3 — el destructor retira incondicionalmente (`set(role,0)`):** si algún día dos vidas del mismo rol se solaparan, la vieja borraría el registro de la nueva. Hoy no ocurre (join previo); si se quiere robustez, CAS que sólo limpie si el TID es el propio.
4. **P3 — modo no original:** el muestreo vive dentro de la rama `EffectiveRuntimeMode::Original` del hilo de métricas; con HW_THREAD_CPU=1 en otro modo no emite nada y sin aviso (SPEC-056 hoy fuerza original, así que es latente).
- Pendiente declarado y correcto: CA2 (captura real en consola), calibración de unidad de `runClocks` y CA4; no aprobable como `done` hasta eso.

---
## Re-review follow-up `8b01743` (test rojo) + `7487572` (fix) — sonnet-rev
**APRUEBA.** Cierra el P2 (gate) y el P3-2 (`size`); P3-3 (destructor incondicional) y P3-4 (modo no original) quedan sin cambio, aceptados por joins serializados y SPEC-056 (modo original forzado).
- **Reproducido** (snapshot HEAD): `tests/run_thread_cpu.sh` PASS (host `-O2 -Werror` + TSan `setarch -R`), `run_all.sh --list` lo descubre (`run_thread_cpu.sh`). Rebuild Vita final: `out/hw075-build-final.log` termina OK, VPK 1.966.708 B (no lo rehice; sin consola/Vita3K).
- **Mutantes:** siempre `%ux%u` → muere (`size=unknown`); `||` en vez de `&&` **sobrevive** (sólo se prueba 0×0; con 800×0 se publicaría `800x0`): P3, agregar un caso de una sola dimensión ausente.
- Script: `mktemp` bajo `hotwheels/out`, borra sólo su directorio en PASS y lo conserva en FAIL (política de disco); `ulimit -c 0`. Sin hallazgos.
## Verificación pedida por el usuario
- **Registro/retirada de todos los hilos de audio:** los únicos `pthread_create` de audio son 3 (feeder `vita_sink.cpp:93`, productor DSound `dsound_bridge.cpp:242`, decoder MCI `music.cpp:73`) y los tres entran por una `ThreadRegistration` RAII al comienzo de su entry (`vita_sink.cpp:169`, `dsound_bridge.cpp:211`, `music.cpp:120`), que retira en cualquier salida (incl. EOF del decoder). Guest: RAII en `main`; Render: `set` en el callback del propio consumer y retiro explícito tras `shutdown_d3d()` (los `return` anticipados de `main` terminan el proceso, sin efecto). No hay hilos de audio sin registrar.
- **Reutilización de TID:** cada `set()` (también `set(0)`) incrementa la generación; mutantes sin incremento/sin chequeo de generación/sin ChangedTid mueren; `sample_thread` descarta la muestra si el token cambia durante la syscall. Respawn de Music/feeder/dsound siempre tras join ⇒ sin solape.
- **Readback sin inferir uso de core:** la fila publica `mask`, `cpu`, `last_cpu`, `prio`, `state` y `runclocks` crudos de ambos extremos, `affinity_changed/priority_changed` (sólo comparación de máscara/prioridad) y `delta_raw`; ni porcentajes ni "uso por core" ni saturación (el test lo exige). CPU3: no aparece (`grep CPU3|USER_3|core3` sin resultados en el código nuevo; no se reutiliza `wx86_vita_core_window_line`).
- **Límites:** CA2 (captura real en consola), calibración de la unidad de `runClocks` y CA4 siguen pendientes ⇒ HW-075 no puede marcarse `done`. Sin mediciones de perf en este corte.

## Re-review `bb30c91` (test: dimensiones incompletas) — sonnet-rev
**APRUEBA.** Sólo test (sin cambio de código/VPK). Snapshot HEAD: `run_thread_cpu.sh` PASS (host + TSan). Mutantes de `size`: `||` en vez de `&&`, sólo `width`, sólo `height` → **los tres mueren** ({800,0} y {0,600} ahora cubiertos); cierra el P3 abierto. Queda HW-075 en `in_progress` (CA2 física, calibración de `runClocks`, CA4).
