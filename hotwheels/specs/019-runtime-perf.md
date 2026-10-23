# SPEC-019 — Perfilado y optimización medida de winx86
- Features: HW-033, HW-034 · Milestone: M6 · Dueño: runtime · Estado: en progreso

## Problema
Hot Wheels ya llega al menú y a una carrera en Vita, pero todavía no hay perfil de hardware de su runtime. El dynarec derivado de Box86 puede gastar tiempo en dispatch, generación/ejecución de bloques, GIL, I/O y esperas del scheduler. Las prioridades de optimización propuestas deben ordenarse por costo observado en consola, no por intuición ni por resultados de Vita3K.

## Contrato observable
- Con `HW_PMU=1` en `ux0:data/hotwheels/env.txt`, el runtime Vita intenta habilitar `vpmu.skprx`, configura contadores Cortex-A9 y acumula contadores por runner. Antes del primer tramo `Cpu::run`, `progress.txt` registra `pmu: init begin tid=...` y luego `pmu: init end tid=... enabled=...`; el intervalo permite distinguir init incompleto de guest activo aunque nunca cierre el hilo. El resumen de métricas por hilo se publica al salir, y el resumen de shims se agrega a `hotwheels.log`. Si el plugin no está disponible, registra el error y el juego sigue sin PMU.
- Cada intervalo es un tramo de `Cpu::run` de hasta 500 ms. Los totales se acumulan por runner y la línea final incluye `HW_PMU_SCENE` (etiqueta fija por ejecución, no detección automática de transición), hilo guest, ciclos, evento de instrucciones, IPC proxy, branch mispredicts y data-cache misses. Se descartan tramos mayores a 500 ms para evitar wrap. Mapeo A9 de D2Vita: `inst_rename=0x68`, `dc_access=0x04`, `dc_miss=0x03`, `br_mispred=0x10`, `dc_stall=0x61`, `ic_stall=0x60`; confirmar en sonda conocida antes de llamar "IPC" a `inst_rename/cycles`, o publicar como proxy.
- El informe agrega `cpu_run_us`, ciclos estimados del lookup `DBGetBlock`, contención y espera GIL de `thread_emu_filstat`, y tiempo/cantidad por shim de `natprof_window` (I/O se identifica por el shim). No estima todavía el wall time fuera de CPU ni el tiempo total reteniendo el GIL; los temporizadores se reportan aparte de la PMU y no se infiere causalidad sólo por correlación.
- HW-034 habilita cada optimización con su propio knob `WX86_*`, apagado por default. La configuración base y candidata corren A/B intercalado sobre la misma escena/guion y carga, registran build/hash/configuración, verifican salida y trampas, y compilan tanto HotWheels Vita como D2Vita.

## Criterios de aceptación
- [ ] CA1: `HW_PMU` ausente o `0` no accede a CP15 ni cambia el log/comportamiento normal; `HW_PMU=1` registra habilitación, identidad de hilos y fallo explícito si falta `vpmu.skprx`. → revisión estática: todas las instrucciones CP15 están bajo `__vita__` y sólo se activan si el knob vale 1; test host cubre deltas e IPC proxy. El piloto de consola no produjo log nuevo.
- [ ] CA2: prueba instrumental en consola con carga conocida confirma eventos esperados y aislamiento por hilo antes de perfilar el juego. → `hotwheels/tests/` y ventana coordinada con integ; evidencia consola. El A9 ofrece seis event counters más CCNT; se configuran los seis eventos en la misma ventana.
- [ ] CA3: capturar al menos cuatro pasadas menú y cuatro pasadas carrera con build/config iguales; publicar por hilo y por escena ciclos, instrucciones, IPC proxy, mispredicts, dc_miss, ciclos estimados en lookup/dispatch (muestreo 1/64), GIL y tiempo por shim/I/O, además de slices válidos/descartados. → reporte A/B en `hotwheels/out/` resumido en FINDINGS/PROGRESS; corrida aprobada se elimina según §Disco.
- [ ] CA4: cada optimización de HW-034 tiene prueba diferencial/correctness en `winx86/tools/torture`, knobs opt-in, A/B intercalado de al menos cuatro pares y guardia de corrección (salida/trampas/errores), y evidencia de impacto medido. → torture correspondiente + resultados host/qemu y, si es candidata de consola, ventana coordinada.
- [ ] CA5: las rutas aditivas/condicionales de winx86 conservan build Vita de HotWheels y D2Vita desde checkout limpio con parches aplicados; cambios de ABI o memoria ejecutable incluyen comprobación de caché D/I (flush D, invalidate I, barreras requeridas). → builds documentados.

Matriz de equivalencia para CA4 (cada fila corre con knob apagado y prendido contra el mismo EXE golden):

| Optimización | Caso de `tools/torture` requerido | Knob aislado |
|---|---|---|
| Encadenamiento de bloques + caché indirecta direct-mapped de 8.192 entradas EIP→dynablock | ciclos directos, destinos indirectos alternantes y SMC que reescribe un destino traducido; salida/memoria/retorno idénticos con caché apagada/encendida. El candidato experimental no se integra: revisión encontró que el puntero puede liberarse después de soltar `mutex_dyndump` y antes de consumirlo; requiere hazard/refcount o integrar con todas las rutas de reclamación | `WX86_CHAIN=0/1`, `WX86_IBRCACHE=0/1` (pendiente) |
| Lazy flags | secuencias reproducibles de ADD/ADC/SUB/SBB/INC/DEC/shift/rotate seguidas por cada condición Jcc/SETcc/CMOVcc y captura EFLAGS; compara con intérprete de referencia | `WX86_LAZY_FLAGS=1` |
| Asignación de registros | presión de registros con spills, llamadas anidadas y preservación EBX/ESI/EDI/EBP, además de flags vivas a través de call/trap | `WX86_REGALLOC=1` |
| Direccionamiento guest con base fija | loads/stores alineados y no alineados en `0x00400000`, borde de página y rango inválido; misma prueba en base relocalizada debe usar fallback y producir idéntico estado/fault | `WX86_FIXED_BASE=1` |
| Menor granularidad del GIL | tres threads con interlocked, eventos y callback que reentra; resultado exacto, progreso de todos y timeout que detecta deadlock/re-adquisición circular | `WX86_GIL_GRANULARITY=...` |
| Afinidad | worker bloqueado/despierto y carga competidora: confirmar máscara efectiva, ausencia de starvation y salida idéntica; comparar pinned/unpinned en legs separados | `WX86_AFFINITY=...` |

Los knobs se habilitan individualmente; no combinar cambios hasta identificar el efecto de cada uno. Si una prueba no puede expresar la semántica observable de un fast path, ese cambio no pasa a A/B.

## Diseño y razonamiento de bajo nivel
- PS Vita da tres cores de usuario (0–2); los hilos de usuario deben documentar prioridad/afinidad. No se fija afinidad hasta medir migraciones y contención. PMU es contexto por hilo que SceThreadmgr restaura en cambios de contexto; `vpmuEnableUser(1)` se invoca después de crear los hilos existentes. Hilos creados después requieren habilitación/revalidación, no se supone que hereden el permiso.
- El dynarec ejecuta código ARM generado para estado IA-32. Cada ruta rápida debe preservar flags diferidas, segmentos (incluido FS/TIB), SEH, convenciones cdecl/stdcall y el estado observable al entrar en un shim. La prueba torture compara estado y memoria guest con la ruta de referencia. El hardware PMU sólo cuenta cuando el hilo está corriendo; sumar deltas por hilo evita mezclar el tiempo de otros procesos o hilos del runtime.
- El scheduler nativo serializa shims/dispatch protegidos por GIL; código guest traducido corre sin GIL y puede tocar memoria compartida. Reducir granularidad amplía regiones concurrentes: mapear Coffman, evitar adquisición recursiva/espera circular (BUG-001), y usar mutex/condición con orden claro. Para publicar datos entre cores ARMv7 usar atomics apropiados y barreras DMB/DSB/ISB donde exige la API; no asumir x86 TSO en host.
- `jump_to_next()` usa la tabla global de bloques de dos niveles y los enlaces directos saltean `LinkNext`; los destinos que llegan al dispatcher entran por `DBGetBlock`. Se prototipó una caché direct-mapped global opt-in de 8.192 EIP→`dynablock_t*`, pero no se integra: aunque se compare época y metadata bajo `mutex_dyndump`, al soltar el mutex el `dynablock_t*` puede liberarse antes de ser consumido por DBGetBlock/dispatcher. La época invalida lecturas futuras, pero no pinnea el bloque actual; ver BUG-045. Hará falta hazard/refcount o que todas las rutas de free participen de una reclamación aplazada. `WX86_CHAIN=0` fuerza el funnel; `=1` fuerza direct-link y ausente mantiene el default histórico respetando `WX86_NOLINK`/`D2_NOLINK`.
- La arena guest y la memoria host son espacios distintos; cualquier cache de traducción respeta vida/invalidez de páginas y TLB guest. Generar código requiere coherencia D/I en ARMv7. Atribuir `dc_miss` requiere considerar líneas de 32 B y working set; misses no equivalen por sí solos a tiempo de memoria.
- Contadores PMU son por hilo, no por escena ni categoría. En la implementación actual `HW_PMU_SCENE` es una etiqueta fija por ejecución; todavía falta separar menú/carrera dentro de un proceso. Dispatch, GIL e I/O usan acumuladores de software y clocks monotónicos de bajo costo. Reportar overhead de la instrumentación y no sumar tiempos concurrentes como si fueran elapsed wall time.
- Logs se acumulan por runner y se publican una vez al salir cada hilo; el resumen de shims/I/O se publica al cerrar la ejecución. No hacer fopen/fclose por muestra ni polling activo. La E/S en memory card puede bloquear cientos de ms, por lo que lectura de fuentes/configuración y escritura de perfil se miden por separado.
- El init Vita fija la condición de medida del A9 en ARM 444 MHz, bus 222 MHz,
  GPU 222 MHz y crossbar GPU 166 MHz. Cada setter se registra junto con su
  retorno y una lectura posterior de `scePowerGet*`; un valor efectivo menor
  invalida la comparación temporal con otra pasada. El log es de arranque, no
  una lectura periódica: una herramienta externa puede cambiar clocks durante
  una ventana y esa limitación debe quedar visible en el reporte PMU.
- `hotwheels/tools/torture/run_minimal.sh` es el oracle mínimo del runtime:
  compila `libwinx86.a` directamente, enlaza un runner ARM que usa
  `Cpu::map/write/invalidate_code/set_trap/run`, ejecuta un loop con dos
  destinos indirectos y reescribe un destino ya traducido. Compara
  `WX86_CHAIN=0/1`: EAX/ECX/EBX, memoria guest (`0x11/0x22/0x33`),
  trap/retorno limpio y ausencia de faults. La prueba de `WX86_IBRCACHE` queda
  pendiente hasta resolver la vida del puntero del caché (BUG-045). No depende de
  `d2vita/tools/rt_boot_arm_check.sh`, del PE real ni de los shims D2; por eso
  aísla la corrección de esos casos del dynarec cuando el checkout D2 está en WIP. Si faltan
  compiladores ARM Linux o qemu, sale `77` explícito y no presenta un falso
  verde.

## No-objetivos
- Cambiar el scheduler global, afinidades, reloj Vita o defaults de rendimiento antes de tener línea base.
- Usar Vita3K como evidencia temporal de PMU/caché/planificación de hardware.
- Integrar optimizaciones que no tengan knob de apagado, prueba de corrección y comparación con la ruta de referencia.
- Tocar `ur0:tai/config.txt`, plugins, saves o apps ajenas; usar consola en paralelo con integ.

## Riesgos / preguntas abiertas
- La habilitación del PMU puede no cubrir hilos creados luego; se comprobará con ensayo multi-hilo instrumentado y no se reportarán como válidos hilos no habilitados.
- **[consola, `d2vita/vita-pmu/NOTAS.md` 2026-09-24]** El PMCR retail reportó `N=6`: hay seis event counters más CCNT. Los seis eventos de la tabla pueden medirse en una misma pasada; no multiplexar ese conjunto. Sólo multiplexar si una futura sonda agrega un séptimo evento programable, y reportar cada pasada por separado por la variación de carga.
- El estimador de `DBGetBlock` coloca dos lecturas CCNT serializadas (`ISB` + `MRC`) alrededor del lookup y multiplica cada muestra 1/64. Por eso `dispatch_lookup_cycles_est` incluye costo de instrumentación y es un estimador superior hasta medir/restar el bracket vacío en consola; no atribuir ese total exacto al lookup. `HW-033` debe publicar ese overhead y muestras observadas antes de priorizar HW-034.
- HW-032 y otros owners modifican runtime/build; coordinar solapamientos antes de cambios en `runtime_vita.cpp`, scripts y winx86.
- La consola está reservada a integ: esta feature necesita ventanas declaradas en `coordination/integ.md`; no se ejecutan comandos de consola hasta el handoff explícito.

## Evidencia y reporte
Estático para ABI/dispatch y ensamblador; host/qemu para corrección diferencial y sobrecosto del código portable; Vita3K sólo para integración funcional; consola para contadores y comparación de rendimiento. Cada número incluye frecuencia/configuración, build hash, escena, duración, cantidad de pasadas, media/mediana y dispersión. Hallazgos refutables y limitaciones quedan en `hotwheels/FINDINGS.md`.
