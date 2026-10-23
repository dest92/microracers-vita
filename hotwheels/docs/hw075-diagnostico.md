# HW-075 — primer corte diagnóstico

El VPK observa main guest, render, productor DSound, feeder y decoder MCI. No cambia scheduling ni mezcla. HW-075 permanece in_progress: falta captura física, calibración y reducción serial medida. SPEC-064 y SPEC-063 separan esas fases.

## Artefactos
- `hotwheels/out/hw075-diagnostic/hotwheels-hw075.vpk`: 1.966.708 B; SHA256 `b9b5cd4bc7d85868bc52a67edc9a125eba90342c024083e59b41eeccd9d8366a`.
- `hotwheels/build-vita-hw075/eboot.bin`: 1.970.283 B; SHA256 `3ec3c7280eb6b0b72dee73aabc126abda056f7e0edac632ca0dcc3ed62db9263`.
- `out/hw075-diagnostic/manifest.json`: hashes de binarios, exe guest, librerías y fuentes capturados después del build. La procedencia de fuentes NO está completamente verificada; core-candidate-1ea906c-parent.cpp es una reconstrucción candidata, no prueba del contenido compilado. No hubo snapshot/hash de fuentes previo al build. `repo-wip.diff`/`winx86-wip.diff` registran el WIP al capturarlos: no prueban por sí solos el contenido compilado ni un árbol limpio. El VPK precede al fix de batching 1ea906c; no fue reemplazado.
- Build: `HW_VITA_BUILD_DIR="$PWD/hotwheels/build-vita-hw075" bash hotwheels/tools/build_vita.sh` (desde raíz). Log `hotwheels/out/hw075-build-final.log`. No se desplegó ni se lanzó el binario.

## Procedimiento mínimo para el orquestador
1. Coordinar consola con usuario; verificar hash del eboot instalado y asociar manifiesto a cada log. Mantener misma pista, settings, música, batching, clocks, input y modo original de 40 Hz; no comparar contra run3/run4 sin identidad de escena/build verificable.
2. Mismo eboot para A/B. En env.txt usar `HW_THREAD_CPU=0` para control y `HW_THREAD_CPU=1` para diagnóstico. Usar `HW_FPS_CONTEXT=hw075-3ec3c728-A` o `-B`; `HW_FPS_SCENE=unverified`. No hace falta PMU. Los registros de entrada/salida por rol existen en ambas condiciones; sólo snapshots/formato/logs periódicos se habilitan con =1.
3. Capturar al menos tres ventanas estables por condición, alternando tres pares A/B. Cada ventana dura nominalmente 10 s, pero usar `t_us`/`elapsed_us` individuales. Guardar captura gameplay y witness guest/PC verificado contra exe por separado, correlacionados por tiempo. Resolución/etiqueta no bastan para decir carrera; no clasificar menú sólo por 640×480.
4. Esperar en B cinco filas `thread-cpu` por ventana y una `thread-cpu-clocks`; revisar ambos extremos de rc, tid/generation, runclocks, mask, cpu/last_cpu, prioridad y estado. Decoder puede no existir si no hay música: `missing` es ausencia, no CPU=0. Primer intervalo puede cruzar arranque/cambio de afinidad: excluirlo. Cambios de afinidad/prioridad quedan marcados. `size=unknown` es deliberado.
5. Rechazar deltas con `delta_valid=0`; `delta_raw=unavailable` nunca cuenta como cero. No convertir runClocks a CPU_us ni a porcentajes de core antes de calibrar unidad/frecuencia con busy/sleep conocidos. Último CPU es observación puntual, no reparto de ejecución entre cores. No sumar main guest como si incluyera todos los runners.
6. Comparar guest Hz, replay/wait y underruns de ventanas con witness equivalente para cuantificar overhead A/B del diagnóstico. Después de medir y calibrar, elegir UNA reducción serial con SPEC-063; su mejora requiere otro A/B. No hay mejora de rendimiento demostrada en este corte.

## Verificación y reviews
`hotwheels/tests/run_thread_cpu.sh` corre host -O2/-Werror y TSan, ya descubierto por `tests/run_all.sh --list`; limpia sólo su corrida si pasa. `tests/run_audio.sh` PASS (ASan+TSan CA1..16, underrun/short-burst/self-paced y backend compartido). Snapshots: ASan/UBSan PASS; mutantes generación/scene/u32 rechazados. Review CA1 reproducida por par-luna (`08f62ec`), código por sonnet-rev (`coordination/review-HW075-code.md`), follow-up gate/size aprobado. CA2 consola y CA4 siguen pendientes. No se modificó winx86; no corresponde rebuild D2Vita por este cambio.
