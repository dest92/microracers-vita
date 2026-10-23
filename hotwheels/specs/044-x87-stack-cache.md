# HW-054 — caché de pila x87 y precisión controlada

- Proyecto: Hot Wheels Micro Racers
- Dueño: runtime
- Estado: en progreso
- Feature: HW-054

## Problema

El perfil PC host QEMU atribuye a arm_fist64_3 589/9.944 muestras (5,92 %) en carrera y 274/9.958 (2,75 %) en menú. Eso ubica una ruta x87 relevante, pero no demuestra que la caché de registros x87 sea el costo dominante ni que una secuencia larga reduzca spills. La ruta Box86 mantiene una pila lógica x87 de ocho entradas y un cache de registros NEON a través de instrucciones traducidas; cambios deben preservar estado guest observable.

## Contrato observable

- A/B deshabilita/habilita cualquier fast path, mismo exe/guion/escena/build salvo knob y compara estados x87, resultados en memoria, flags/status word y comportamiento de excepciones enmascaradas. Sólo se comparan traps entregados al guest si el runtime soporta su entrega para esa instrucción; la prueba debe documentar la capacidad actual.
- FLDCW con PC=24 reduce la precisión de la significanda a 24 bits, pero conserva el rango de exponente extendido; no equivale a operar en IEEE binary32. Un camino que convierta operandos/resultados a float sólo se admite si el PE diferencial prueba iguales resultados, status y excepciones para todos los casos; PC=53/64 o modos no admitidos conservan fallback exacto.
- TOP, tags, push/pop, FXCH, FLD/FSTP, FLDCW, valores extendidos, NaN/Inf, denormals, overflow/underflow y límites de pila mantienen semántica IA-32.
- Cualquier cambio a superbloques sólo extiende bloques para secuencias probadas; las fronteras por branch, trap, llamada C/shim, fault y scheduler materializan estado antes de escapar.

## Criterios de aceptación

- [ ] CA1: desensamblar top EIP guest y host samples x87 para relacionar arm_fist64_3 con opcode/sitio; medir tasa de hits/misses y stores/loads del cache en cada escena. Reportar ruido/overhead.
- [ ] CA2: un PE fixture x86 propio corre sin cambios con Wine x86 como oracle y con el runtime ARM candidato. Escribe en un buffer versionado resultados, FNSTENV/FSTENV (control/status/TOP/tags) y valores guardados; un checker host compara campos. Incluye push/pop, rotación TOP, swap, conversiones, FLDCW PC 24/53/64 y varios RC. Excepciones enmascaradas deben tener estados comparables; traps sólo se prueban para instrucciones cuya entrega guest esté soportada y documentada. Baseline igual al oracle antes de optimizar; el checker debe detectar una divergencia inyectada.
- [ ] CA3: cada optimización lleva knob WX86_* default off hasta demostrar equivalencia; una prueba de bins límites (sNaN/qNaN, ±0, ±Inf, denormal, overflow/underflow, pila vacía/llena) no debe divergir.
- [ ] CA4: A/B intercalado al menos cuatro pares por escena en QEMU con HW_QPC_IDLE_US=300, misma input/settings; confirmar correctness/frames y comparar CPU ms/frame. Rechazar cambio si mediana no mejora o la dispersión supera el efecto. QEMU no mide ciclo absoluto A9; consola queda pendiente y coordinada.
- [ ] CA5: patch aditivo/condicional en serie winx86; torture host/QEMU PASS, TARGET=vita de la lib pasa y D2Vita sigue compilando desde checkout limpio.

## Diseño y razonamiento

Los registros x87 son una pila lógica (ST(0) relativo a TOP) con 80-bit extended y control/status propios. ARM VFP NEON ofrece binary32/binary64 pero no replica directamente 80-bit: cualquier almacenamiento intermedio puede perder precisión, cambiar excepciones o alterar RC. FLDCW modifica PC y RC durante ejecución; PC=24 reduce precisión de significanda pero conserva exponente extendido, por lo que no equivale a binary32. La decisión debe respetar control word dinámico y sus cambios.

La neoncache es estado de compilación que mapea registros ST a D registers y rastrea TOP/swaps/pares combinados; los spills/materializaciones tocan el working set y el código generado. Audit estático y ensamblador ARMv7 antes de cambiarla. En concurrencia, el cache es local al bloque compilado, no compartido entre hilos; no añadir estado mutable global. Llamadas a helper/C, traps y exits deben materializar estado antes de ceder el GIL o planificador.

El perfil QEMU PC mide distribución del PC de guest traducido y helpers del runtime, no PMU, clocks ni VitaGL. Para oracle, el PE fixture corre sin cambios en Wine x86 y el runtime ARM, guarda una estructura serializada con versión/endianness fija y el checker host compara campos; no asumir que Wine captura traps no implementados. El A/B informa hash/configuración, escena, pares, medias/mediana/desvío; no atribuir diferencia pequeña si hay cambio de escena o carga host.

## No objetivos

- Emular x87 completo de nuevo, alterar contratos de flags o cambiar la política global de superbloques sin medida.
- Activar precisión simple para PC=24 sin demostrar semántica exacta de cada operación.
- Usar Vita3K como evidencia temporal de PMU o abandonar fallback de extended precision.
