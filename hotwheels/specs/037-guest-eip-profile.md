# Spec 037 — perfil por muestreo del EIP guest (HW-051)

Problema: PMU de consola no está disponible mientras la Vita requiere desbloqueo físico. Para elegir optimizaciones hace falta una distribución de CPU por función que funcione en qemu-arm y Vita3K sin instrumentar cada instrucción.

Contrato observable:
- Opt-in por `HW_GUEST_PROFILE=1`; sin él no se crea sampler ni se emite salida.
- La API dyn86 del patch 0005 es opcional para el link base: si falta, el runtime sigue arrancando y el sampler no se crea aunque el env esté activo; con el patch aplicado vuelve a estar disponible.
- `HW_GUEST_PROFILE_SCENE=menu|race` etiqueta cada reporte. Se rechaza/normaliza cualquier otro valor a `unknown`.
- El backend guest-EIP usa un hilo host que despierta cada 1 ms y lee el EIP que una tienda ARM alineada publica en la entrada de cada bloque traducido. La instrumentación sólo se emite si `HW_GUEST_PROFILE=1` antes de traducir; el perfil no deshabilita el encadenamiento directo. Vita intenta fijar el sampler a `USER_1`, prioridad 128, e informa ambos rc; qemu conserva la afinidad del host. El hilo lector no accede al archivo de registros del emulador ni toma el GIL.
- Salida determinista `guest-profile: scene=... samples=N guest_pct=P shim_pct=Q` y hasta 30 filas ordenadas por cantidad: dirección inicial, nombre Ghidra o `sub_XXXXXXXX`, porcentaje del total y muestras. La tabla cubre funciones guest; las muestras fuera de módulos ejecutables cuentan como shim/host.
- Un mapa de funciones exportado del proyecto Ghidra relaciona rangos `[start,end)` con nombres; rangos solapados se resuelven a la función de rango más específico, huecos quedan como `sub_XXXXXXXX` y el binario del juego no se copia al repositorio.
- El cierre detiene y une el sampler antes de destruir runtime/log sink. Muestreo sin muestras no divide por cero.

Criterios de aceptación:
1. Test host del acumulador con EIP sintético verifica asignación por rango, huecos, guest/shim, top-N, porcentajes y escena inválida.
2. Prueba con juego real en qemu-arm produce top-30 independiente para menú y carrera; resultados de cada corrida incluyen build, escena, intervalo y cantidad de muestras. Una segunda corrida de la misma escena conserva la lista de funciones y el orden de las dominantes (variación de porcentaje se reporta, no se oculta).
3. Vita3K produce ambas escenas con el mismo mapa y contrato. Consola queda como validación futura, no bloquea estas dos.
4. `HW_GUEST_PROFILE` ausente conserva arranque normal y no agrega líneas del perfilador.
5. Build Vita sin el patch dyn86 enlaza; pedir perfil en ese build no inicia el hilo ni llama símbolos ausentes.

Diseño y límites: cada bloque perfilado emite `MOV32_(x1, init_addr)` + `STR_IMM9` al slot de su `x86emu_t` (una tienda por entrada; tres instrucciones ARM en la forma fija actual). El slot alineado se lee con carga atómica relaxed: una muestra vieja sólo puede retrasar la atribución un intervalo, no sincroniza memoria guest. El sampler representa proporción temporal aproximada, sujeta a latencia de wakeup y scheduler; el coste añadido puede sesgar bloques cortos y debe medirse con A/B intercalado. En un sistema con varios hilos guest, se necesita un slot por hilo; el reporte actual es válido sólo para el hilo guest principal. Hilos dormidos no aportan muestras. El lector no toma GIL.

No-objetivos: reemplazar PMU de Vita, publicar métricas de ciclo/instrucciones, capturar cada instrucción, cambiar semántica x86, instrumentar archivos del juego o marcar optimizaciones HW-034 como medidas.


## Backend PC host fiel (QEMU Linux)

Para evitar el sesgo de encadenamiento señalado en FINDINGS 25a305f, `HW_PC_PROFILE=1` arma un timer Linux `CLOCK_THREAD_CPUTIME_ID` periódico de 1 ms con `SIGEV_THREAD_ID`, dirigido al TID actual que ejecuta el guest. El handler lee `arm_pc` del `ucontext` de ese hilo. Al cerrar, `FindDynablockFromNativeAddress()` y `instsize[]` mapean PC host → dynablock → EIP guest; el mapa Ghidra resuelve la función. PC fuera del dynablock se simboliza con `addr2line` del ELF exacto y se agrupa por helper, dispatch, shim, render/audio o runtime. La ventana de escena usa tiempo monotónico, pero los ticks consumen CPU sólo del hilo guest. El backend Linux no está compilado para Vita; el muestreo Cortex-A9/PMU y el perfil Vita3K requieren validación aparte.

Resultados QEMU limpios de menú y carrera, top-30 y desglose host: `hotwheels/docs/guest-profile.md`. Evidencia de distribución muestreada; no informa ciclos ni IPC.
