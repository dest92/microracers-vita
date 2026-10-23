# Cómo construimos este port: harness, metodología y trabajo con IA

Este documento resume **cómo** se hizo el port de *Hot Wheels Micro Racers* (PC, Win9x, Direct3D 6) a PS Vita, no **qué** hace el código (para eso están el [README](../README.md) y [PORTING.md](PORTING.md)).

En resumen: una persona dirigiendo un equipo de agentes de IA, con una metodología de ingeniería estricta, verificación contra hardware real y un harness que reparte el trabajo entre varias máquinas. Ninguna línea entró "porque la IA dijo que andaba".

## Números del proyecto (monorepo de desarrollo, ~3 semanas)

| | |
|---|---|
| Commits | ~1.500 |
| Specs (SDD) | 80 |
| Bugs documentados con causa raíz | 55 |
| Tests / scripts de verificación | ~170 |
| Code reviews escritas | ~490 |

## 1. Metodología: SDD + TDD + milestones

- **Spec-Driven Development.** Todo cambio no trivial empieza por una spec (`specs/NNN-*.md`): problema, evidencia, contrato, criterios de aceptación y cómo se verifica. El código se escribe *contra* la spec, no al revés.
- **TDD.** Los criterios de aceptación se convierten en tests antes de implementar (tests de host en x86-64, smoke tests en QEMU ARM, harness en el emulador Vita3K y, al final, consola real).
- **Milestones.** El trabajo se organiza en hitos (boot, menús, carrera, rendimiento, calidad). Un ítem del `feature_list` sólo pasa a *done* con evidencia y review aprobada, y sólo el orquestador puede marcarlo.
- **Bugs con autopsia.** Cada bug tiene su archivo con síntomas, hipótesis descartadas, causa raíz y fix verificado. Ejemplo: los glitches gráficos en las pistas (polígonos que "saltaban") se aislaron con videos A/B hasta probar que la causa era la interpolación de frames a 60 Hz; la decisión fue volver al modo original de 40 Hz, sin interpolar.

## 2. El harness multi-agente

Un **orquestador** (un agente de IA de alto nivel) planifica, reparte y decide; varios **agentes trabajadores** implementan, investigan, miden y revisan en paralelo. Todo corre en terminales visibles y controlables (un multiplexor de terminal para agentes, *herdr*), así que se puede mirar en vivo qué hace cada uno, interrumpirlo o redirigirlo.

- **Modelos según la tarea.** Modelos grandes (Claude Opus, GPT de alta capacidad) para lo complejo: dynarec, concurrencia, render. Modelos más baratos para investigación, mediciones e implementaciones acotadas. Un modelo distinto al autor hace la review.
- **Coordinación por archivos, no por memoria.** Cada agente recibe un *brief* escrito (objetivo, alcance, qué no tocar, cómo reportar). Hay colas de review y de trabajo, un registro de decisiones y un *handoff* para que el rol de orquestador pueda pasar de un modelo a otro sin perder contexto.
- **Control de costo.** El presupuesto de tokens se gestiona como cualquier recurso: equipos chicos, lecturas acotadas, modelos caros sólo donde rinden.

## 3. Cómputo distribuido por SSH

La máquina de desarrollo (Linux) no alcanzaba para todo: emulación, builds y análisis en paralelo.

- Una **PC Windows** de la red local se sumó como *worker* por SSH: los agentes le mandan jobs (builds, corridas del emulador Vita3K, grabación de video, ejecución del juego original), con locks para no pisarse y guardas de disco.
- **Lección aprendida:** un runner remoto llenó el disco del sistema copiando el repo en cada corrida. Se limpió, se movió el entorno Linux de esa PC (WSL) a otro disco y desde entonces existe una regla explícita y una guarda automática de espacio libre.
- **La PS Vita real** es parte del harness: los builds se suben por FTP verificando SHA-256 y los logs de runtime se bajan para analizar. La consola es la fuente de verdad final; el emulador no alcanza (en él no se veían ni el audio con ruido ni el render por software causado por un save viejo).

## 4. Ingeniería inversa

- **Ghidra** y **REA** (un framework de ingeniería inversa operable por agentes) para entender el ejecutable original: funciones críticas, formato de pistas, audio.
- El **juego original corriendo en Windows**, instrumentado con Frida, como referencia de comportamiento (qué llama, cuándo, a qué ritmo) para comparar contra nuestro runtime.

## 5. Por qué esto no es *vibecoding*

*Vibecoding* es pedirle código a una IA, ver que "parece andar" y seguir. Acá pasó lo contrario:

1. **Evidencia antes que opinión.** Ningún fix se da por bueno sin medición: logs de la consola, telemetría de frame time, videos A/B, tests. Las reviews rechazan conclusiones causales que los datos no sostienen.
2. **Review adversarial obligatoria**, con checklist propio centrado en lo que la IA suele hacer mal: concurrencia, ownership de memoria entre CPU y GPU, orden de memoria (x86 TSO vs ARMv7 débil), tiempo real, *use-after-free*. Cerca de 490 reviews escritas.
3. **Pensar a nivel sistema operativo.** Hilos y afinidad por core (juego, render y audio en cores separados), colas acotadas, presupuestos de tiempo por frame (25 ms para 40 Hz) y medición de dónde se va cada milisegundo.
4. **Humano en el loop en las decisiones.** La persona define objetivos, prioridades y restricciones ("que funcione como el original", "cero interpolación", "no tocar el disco C"), juega en la consola real y reporta. La IA ejecuta, mide y propone; no decide sola qué es aceptable.
5. **Higiene legal y de publicación.** No se versionan datos del juego original; el repo público pasó una review de licencias, atribuciones y datos privados antes de publicarse.

## 6. Qué aprendimos

- Los agentes rinden mucho más con **contratos escritos** (specs, briefs, checklists) que con conversación.
- La **diversidad de modelos** en la review encuentra errores que el autor no ve.
- El **hardware real** desmiente al emulador más seguido de lo que uno espera.
- El **costo** (tokens, disco, tiempo del humano) es un requisito de diseño más, no un detalle.

## Estado

Experimental: el juego arranca y se pueden correr carreras completas en la consola, sin glitches gráficos, a una cadencia todavía por debajo de los 40 Hz objetivo. El cuello de botella medido es la CPU que ejecuta el código x86 traducido; el trabajo actual apunta a perfilarlo y a repartir más trabajo entre los tres cores de la Vita.
