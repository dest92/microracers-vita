# SPEC-053 — Costo guest/dynarec por instrucciones ARM ejecutadas (HW-072)
- Feature: HW-072 · Milestone: M6 · Dueño: perf-cpu · Estado: en progreso

## Problema
En consola la carrera corre a ~24–30 guest Hz (objetivo 40 + interp60) y **no hay medición de fps en consola**.
qemu-arm y Vita3K **no predicen tiempo en Cortex-A9**: el A/B de HW-052 (HLE de spans, `__ftol`, raster AOT) dio
neutro o incluso peor porque qemu emula VFP por software y domina el costo en ambos lados; el ms de CPU por pata
también está contaminado por la carga del host (loadavg). Sin una métrica **independiente del host** no se puede
priorizar ni comparar cambios del lado guest/dynarec.

HW-051 dejó la *distribución temporal* por función (muestreo del EIP, `docs/guest-profile.md`): dice **dónde** se
gasta el tiempo, no **cuánto trabajo** se hace. El brief de HW-072 pide justamente la métrica que falta:
**instrucciones ARM ejecutadas por frame** + estimación de ciclos, por función del top del perfil.

## Idea y por qué es válida
El binario ARM que corre bajo `qemu-arm` es **el mismo** que correría en la A9: el runtime del port (C++ ARM) más
el código ARM que **genera el dynarec en runtime** (los bloques del guest x86). Bajo `-d exec,in_asm` qemu registra
cada bloque traducido (TB) que ejecuta y sus bytes; contar `bytes/4` por ejecución da el **número exacto de
instrucciones ARM ejecutadas**, sin importar la velocidad del host. Ese conteo **transfiere a la A9** (misma
secuencia de instrucciones); lo que cambia entre hosts es el CPI, no el conteo. Es determinista y por eso sirve
para A/B de cambios del JIT donde el ms no sirve.

## Contrato observable
`hotwheels/tools/qemu_insn_count.py` lee un log de `qemu-arm -d nochain,exec,in_asm` (archivo o stdin, en streaming)
y emite un resumen parseable:
```
qemu-insn: tb_unique=N tb_exec=M arm_insns=T blocks_unknown=0
```
más una tabla opcional (con `--per-tb`) de `addr ejecuciones instrucciones` por TB, ordenada por instrucciones
descendente. Reglas del parser:
- Un bloque `in_asm` es `IN: <sym>` + `0x<addr>:  ` + uno o más `OBJD-T: <hex>`; el tamaño en instrucciones es
  `sum(len(hex))/2/4` (ARM de 32 bits, `-marm`). La **dirección no identifica al bloque**: el cajón de código del
  dynarec reusa direcciones, así que la misma dirección puede alojar otro bloque (otro tamaño) más tarde. Cada
  ejecución se cuenta con el tamaño **vigente en ese momento**, y `tb_unique` cuenta direcciones distintas, no
  bloques distintos.
- Una línea `Trace <cpu>: 0x<hostpc> [<a>/<guestpc>/<b>/<c>]` es **una ejecución** del TB en `guestpc`.
- **Borde duro**: una ejecución de un `guestpc` sin bloque `in_asm` es un **error** (`blocks_unknown>0` ⇒ exit≠0 y
  mensaje), nunca un conteo silencioso: significaría que el trace está incompleto (p.ej. `nochain` no aplicado) y
  subestimaría justo los bucles calientes encadenados.
- Log vacío ⇒ total 0 (exit 0); `Trace` con formato no reconocido ⇒ error, no se ignora.

`hotwheels/tools/arm_cost_model.py` combina ese conteo con la distribución por función (salida `pcprof-game` de
HW-051) y los frames de la corrida: reparte instrucciones ARM por función y estima ciclos/frame en la A9 a una
frecuencia dada, con `cycles_per_insn` explícito. Emite por función: instrucciones/frame, % y ciclos/frame
estimados; y el **techo de fps** implicado por los ciclos del frame (frame_time = ciclos/frecuencia).

## Criterios de aceptación
- [ ] CA1 — Parser unitario (fixture sintético, host): dedupe de bloques reimpresos, `OBJD-T` multilínea, bloques
  con nombre y sin nombre, y **error** ante ejecución sin `in_asm`/línea malformada. → `tests/test_qemu_insn_count.py`.
- [ ] CA2 — Oráculo diferencial (real): un programa ARM con un bucle de cuerpo conocido corrido bajo
  `qemu-arm -d nochain,exec,in_asm` **dos veces** con distinta cantidad de iteraciones; el conteo debe diferir en
  `(iters2−iters1) × instrucciones_del_cuerpo`, con el cuerpo leído de `objdump`. Corre cuando hay gcc/qemu ARM
  (imagen `hw-host-arm`); si faltan, `SKIP 77` (no falso verde). → `tests/run_qemu_insn_oracle.sh`.
- [ ] CA3 — Modelo de costo unitario (host): reparto por función, ciclos/frame y techo de fps con entradas
  sintéticas; bordes (0 frames ⇒ sin división; % que no suman 100 se reporta, no se oculta). →
  `tests/test_arm_cost_model.py`.
- [~] CA4 — Corrida real (qemu) produce el conteo con build/hash, escena, ventana y artefacto registrados, y el
  análisis de cada función >3 % (causa + propuesta) queda en `docs/perf-cpu-072.md` y FINDINGS. **Parcial**: el
  mecanismo se validó sobre el juego real (ventana boot/carga, 421 M instrucciones ARM, `5f53e9a`), pero la
  **ventana de carrera no es alcanzable** con `-d exec`: frena y perturba al guest ~200× (286 frames/12 s sin trace
  vs 9 presents/60 s con trace) y el guion navega por frame (~1.200 frames hasta la carrera). Caminos para cerrarlo:
  (a) qemu con plugins TCG, (b) contador en `DBGetBlock` con `WX86_CHAIN=0` (sitio ya muestreado 1/64 por
  D2_JITPROFILE), (c) telemetría de consola Fase 2. El análisis >3 % usa el perfil fiel de HW-051 y costo estático.

## Diseño y razonamiento de bajo nivel
- **Dónde corre cada cosa.** La medición corre en qemu-arm/TCG sobre x86 (host de medición); el objetivo es A9
  444 MHz. El *programa ARM* es el mismo en ambos: runtime del port + bloques que emite el dynarec. Por eso el
  conteo de instrucciones es una cota de trabajo, y el tiempo es `Σ instrucciones × CPI(clase)`. El A9 tiene CPI≥1
  por instrucción, memoria L1 de 32 B, y un VFP con latencias de varios ciclos: la estimación de ciclos usa una
  tabla por clase y se declara como estimación (no medición), nunca como "fps de consola".
- **Por qué `nochain`.** Con encadenamiento directo TB→TB, la transición no pasa por el bucle de TCG y **no se
  registra**. Eso sesgaría el conteo exactamente en los bucles calientes del JIT (los que más se encadenan) → es un
  sesgo sistemático a la baja. `-d nochain` preserva la semántica (sólo desactiva la optimización de salto directo)
  y hace el trace **completo**; no cambia el resultado del juego ni el conjunto de instrucciones ejecutadas.
- **Conteo vs. instrumentar el JIT.** El contrato del profiler de dynarec (D2_JITPROFILE, `dyn86.h`) prohíbe tocar
  la generación de código: un contador por bloque en el prólogo cambiaría el codegen y sesgaría lo que mide. Por eso
  el conteo se hace **fuera** del guest (qemu), sin parchear winx86 ni el port: cero riesgo de ABI/semántica x86 y
  cero efecto en lo que se mide.
- **Atribución por función y hilos.** El reparto por función parte de la distribución temporal de HW-051: el
  muestreador publica el EIP con un *store* atómico relejado leído desde otro hilo; asumimos `muestras ∝ ciclos`
  (válido a primer orden). El hilo muestreador no toma el GIL ni toca registros del guest. La limitación (no separa
  CPI entre funciones) se documenta; el total de instrucciones sí es exacto.
- **Costos que el modelo NO captura (declarados).** Stalls de memoria (L1/L2 vs working set), mispredicts de rama,
  latencia de VFP, coherencia I/D al generar código (flush D + invalidate I), syscalls/E-S. Se listan para no
  confundir "instrucciones" con "tiempo".
- **Bordes.** 0 frames; ventana sin llegada a carrera (counts de menú); trace truncado (parser falla, no adivina).

## No-objetivos
Optimizar (esto sólo mide y prioriza). Tocar winx86 o el código del port. Afirmar fps de consola. Reemplazar la PMU
de la A9 (HW-033) ni el perfil EIP (HW-051): los complementa. El VPK de telemetría de Fase 2 es entregable conjunto
con perf-host (HW-073) y se documenta aparte.

## Riesgos / preguntas abiertas
- Volumen del log `exec`: se consume en streaming (sin archivarlo) para no llenar disco ni RAM. `nochain` y el
  logging agregan overhead, así que se mide una **ventana corta**; el conteo no depende del wall time.
- El conteo incluye también el runtime ARM (libc/port), no sólo el guest; el reparto por función es del guest y
  quedará un resto "otro runtime" que se reporta por separado, no se esconde.
- Si un A/B cambia la cantidad de frames que el guest alcanza en la ventana, se normaliza por frame.

## Evidencia y reporte
estático (objdump de los TB y del binario ARM), qemu (conteo exacto de instrucciones y distribución), y estimación de
ciclos declarada como tal. Cada número: build/hash, escena, ventana, frames, y CPI usado. Los hallazgos refutables
van a `FINDINGS.md`.
