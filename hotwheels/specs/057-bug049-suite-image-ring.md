# SPEC-057 — BUG-049: la suite del replay de anillos debe ser ejecutable sin runner

- Feature: HW-070 · Milestone: M8 · Dueño: cc-img · Estado: borrador
- Insumos: `hotwheels/bugs/BUG-049-test-image-ring-sin-argv.md`,
  `hotwheels/specs/049-nitidez-carrera.md` (CA2), `hotwheels/bugs/BUG-048-run-all-insumos-fuera-de-git.md`
  (precedente de `skipUnless`), `coordination/brief-comun-m8.md`.

## Problema

`tests/run_all.sh` (etapa 3) corre **todas** las suites `tests/test_*.py` como
`python3 <archivo>`, sin argumentos: es un glob, no un registro con su comando. La suite
`tests/test_image_ring_replay.py` es además un driver de ctest (`image_ring_replay_input`),
y su bloque `if __name__ == "__main__"` leía `sys.argv[1]` **sin guarda** para resolver el
binario `image_ring_replay`. Con la invocación del glob no existe `argv[1]`, así que el módulo
muere **durante el import** con `IndexError: list index out of range` (rc=1) y `run_all` cuenta
un FAIL de código de HEAD. El gate de humo (`tools/smoke.sh` etapa "tests host") pierde señal:
un rojo de infraestructura se confunde con un rojo de la imagen.

La misma clase de defecto ya se corrigió para el audio en BUG-048 (`466b032`): *"una suite que
necesita un insumo externo se saltea, no falla"*, con el patrón `unittest.skipUnless` de
`test_dsound_oracle.py` (`HW_DS_REPLAY`) y el precedente de `test_qemu_insn_oracle.py` (docker).

## Contrato observable

1. **Un solo origen para el binario.** El path del binario `image_ring_replay` se resuelve desde
   la variable de entorno `HW_IMAGE_RING_REPLAY`. No hay segunda vía (ni `argv[1]`, ni default
   hardcodeado, ni búsqueda en el árbol): un insumo externo tiene una sola fuente de verdad, como
   `HW_DS_REPLAY` en el audio.
2. **Sin insumo, la suite se saltea.** Sin `HW_IMAGE_RING_REPLAY`, el módulo se importa sin
   excepción, los casos que necesitan el binario quedan `skipped` con un motivo explícito, y el
   proceso termina con rc=0. Nunca `IndexError`, nunca traceback al import.
3. **Con insumo, los casos corren de verdad.** Con `HW_IMAGE_RING_REPLAY` apuntando a un
   ejecutable válido, los 8 casos se ejecutan (0 skips) y el resultado es el mismo que antes del
   cambio: el binario es la entrada y el parser/replay no se tocan.
4. **El skip es visible en el resumen.** La suite anuncia el skip con una línea
   `RUN_MODE: skip (...)` en stdout, que `tests/run_all.sh` ya sabe leer (`record()`) para que el
   resumen diga `PASS test_image_ring_replay.py (skip: ...)` y no un PASS indistinguible de una
   corrida real. Un skip no se puede pasar por verde.
5. **El punto de cableado sigue siendo ctest.** `CMakeLists.txt` inyecta la env al test
   `image_ring_replay_input` con `ENVIRONMENT "HW_IMAGE_RING_REPLAY=$<TARGET_FILE:image_ring_replay>"`,
   de modo que la ruta del binario la conoce el build, no el test.

## Criterios de aceptación

- [ ] CA1: `python3 hotwheels/tests/test_image_ring_replay.py` sin env ni argumentos termina con
  rc=0, informa `skipped` y **no** contiene `IndexError` ni `Traceback`; además emite la línea
  `RUN_MODE: skip (...)`.
  → test: `hotwheels/tests/test_image_ring_replay_standalone.py`.
- [ ] CA2: el test de ctest `image_ring_replay_input` sigue ejecutando los 8 casos reales (0 skips)
  contra el binario inyectado por el build.
  → test: `ctest --test-dir <build> -R '^image_ring_replay_input$'`.
- [ ] CA3: `tests/run_all.sh` en worktree limpio de HEAD no reporta FAIL para
  `test_image_ring_replay.py`; el conteo PASS/FAIL mejora respecto de la línea base medida.
  → test: `hotwheels/tests/run_all.sh` (worktree limpio).
- [ ] CA4: el módulo es importable sin env y sin argumentos: `import test_image_ring_replay` no
  lanza. Es la propiedad que `run_all` necesita (no muere al importar).
  → test: `hotwheels/tests/test_image_ring_replay_standalone.py` (import en un subproceso).

## Diseño

**Por qué env y no `argv[1]`.** `argv[1]` colisiona con los flags propios de `unittest`:
`python3 tests/test_image_ring_replay.py -v` tomaría `-v` como path del binario y produciría un
`FileNotFoundError` (un FAIL distinto y más confuso que el que se quiere eliminar). Con env, el
flag `-v` sigue siendo de `unittest` y la ausencia del insumo se resuelve como SKIP. Además el env
es el mecanismo ya establecido en el repo para "insumo externo de una suite" (`HW_DS_REPLAY`) y
permite que `run_all.sh` distinga el caso "nadie proveyó el insumo" del caso "el insumo rompió".

**Por qué no un `tests/run_image_ring_replay.sh`.** La cobertura de la validación de anillos ya
existe en `run_all` por la etapa 1: `ctest` corre `image_ring_replay_input` con el binario
construido por el build. Un runner shell adicional duplicaría esa corrida y agregaría una segunda
fuente de verdad sobre dónde vive el binario; el bug reportado ("el test sólo es ejecutable a
mano") queda corregido al registrar que el runner real es ctest, no al sumar otro.

**Mecanismo del skip.** `unittest.skipUnless(REPLAY, "requiere HW_IMAGE_RING_REPLAY ...")` a nivel
de clase: los 8 casos comparten la misma dependencia (el binario), así que la guarda es de clase y
no se repite por método. `skipUnless` marca la clase con `__unittest_skip__` y `unittest.main()`
reporta `OK (skipped=N)`, de modo que el rc=0 no confunde "todo verde" con "nada corrido": el
motivo del skip queda en la salida.

**Costo.** Sólo tiempo de host de pruebas: una lectura de env en el import y una comparación de
cadenas. No toca el runtime Vita, el ring, el parser ni el replay; no cambia el tamaño del VPK ni
el costo por frame. La imagen de carrera (HW-070/CA3) no se ve afectada.

**Concurrencia / memoria.** No aplica: no hay código de producción ni estado compartido. El
subproceso que usa el test de regresión es un `subprocess.run` efímero, sin estado persistente.

## No-objetivos

- No tocar el parser de anillos, el replay offline ni el formato HWRING01 (SPEC-049 CA2 queda
  intacto: los mismos 8 casos, mismos veredictos).
- No agregar un runner shell que duplique el cableado de ctest.
- No cambiar la semántica de `run_all.sh` ni el formato de su resumen; se reutiliza el mecanismo
  `RUN_MODE:` que ya existe.
- No usar la consola real (Fase 1) ni tocar la build Vita.

## Riesgos / preguntas abiertas

- `ENVIRONMENT` con generador de expressions en `set_tests_properties` requiere CMake ≥ 3.22; el
  proyecto exige 3.24 y el host tiene 3.28, así que está soportado. Si la expresión no expandiera,
  el path sería literal y los casos fallarían con `FileNotFoundError` (ruido, no verde falso).
- Con el skip, `run_all` marca PASS una suite que no ejecutó casos. Por eso CA1 exige la línea
  `RUN_MODE:` y el resumen de `run_all` debe mostrarla; sin esa anotación el conteo miente.
- El skip no reemplaza la ejecución real: si alguien borra el build, la etapa 1 de `run_all`
  (ctest) también falla y no hay build ⇒ no hay binario que probar. Es el mismo supuesto que el
  resto de las suites de ctest.

## Evidencia

- Estático: el módulo se importa sin env ni args y no lanza; el test de ctest recibe la env.
- Host: el test de regresión (CA1/CA4) y `ctest -R image_ring_replay_input` (CA2).
- `run_all` en worktree limpio de HEAD (CA3), con el conteo PASS/FAIL antes y después.
- Sin impacto en Vita3K ni consola: el cambio es exclusivo de la suite host.
