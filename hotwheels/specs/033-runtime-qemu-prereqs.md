# SPEC-033 — Prerequisitos explícitos del oracle ARM runtime

## Problema y contrato

`init.sh` declara listo el checkout sin verificar `arm-linux-gnueabihf-gcc`, `arm-linux-gnueabihf-g++` ni `qemu-arm`. El oracle de `winx86/tools/torture` depende de los tres para compilar el runtime ARMv7 y ejecutar el mismo PE de referencia. Un OK prematuro hace que la verificación parezca disponible aunque el primer build falle por `command not found`.

`init.sh` debe reportar cada herramienta con una línea `OK` o `FALTA`; estas dependencias son opcionales al entorno general del port y no deben convertir el init global en fallo crítico. El harness de torture conserva su propio chequeo antes de ejecutar.

## Aceptación

- Un test exige que init reporte los tres prerequisitos, sea `OK` o `FALTA` según el PATH.
- Si falta uno, init muestra `FALTA <nombre>` en vez de omitirlo o afirmar que el tier qemu está disponible.
- No se modifica dynarec ni se condiciona el build de Vita.

## Razonamiento técnico

La compilación cruzada produce instrucciones ARMv7-A/NEON; el runner `qemu-arm` ejecuta el proceso ARM en espacio de usuario y permite comprobar la semántica funcional del runtime. Ninguno de los dos reproduce PMU, coherencia I/D física ni planificación de los tres cores Vita. El cambio sólo hace visible la frontera entre el tier host y el tier qemu; no mueve guest threads, no altera el GIL ni modifica el modelo de memoria x86→ARM.

## No objetivos

- Instalar toolchains o depender de red.
- Cambiar requisitos críticos de Vita SDK, Wine, Vita3K o del build de consola.
- Presentar qemu como evidencia temporal de consola.
