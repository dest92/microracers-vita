# BUG-049 — HEAD rojo en `run_all`: `test_image_ring_replay.py` exige `argv[1]` y revienta con `IndexError`

Nivel: **medido** (oc-qa, 2026-10-21 ~21:17, HEAD `9580f82`, worktree aislado). Dueño: **cc-img** (HW-070, cadena de imagen). Severidad: mayor (`run_all` rojo). Estado: **cerrado** (`b68d778`; verificado por oc-qa en HEAD `6a25c55`).

## Síntoma (exacto, logs)
`hotwheels/out/qa-clean-m8/test_image_ring_replay.py.log`:
```
File ".../tests/test_image_ring_replay.py", line 75, in <module>
    ImageRingReplayInputTests.binary = Path(sys.argv[1]).resolve()
IndexError: list index out of range
```
`run_all` ejecuta `python3 tests/test_*.py` sin argumentos (`tests/run_all.sh:46`), así que la suite nunca llega a correr sus casos: muere al importar.

## Reproducción mínima
```bash
cd hotwheels && python3 tests/test_image_ring_replay.py      # IndexError
```
No hay `tests/run_image_ring_replay.sh` que arme `tools/image_ring_replay.cpp` y le pase el binario: el test sólo es ejecutable a mano.

## Esperado vs observado
- Esperado: sin `argv[1]` la suite se saltea (o compila y usa un binario por defecto) y `run_all` queda verde/skip.
- Observado: `IndexError` en tiempo de importación → FAIL contado como fallo de código de HEAD.

## Causa raíz (verificada)
El binario de replay se pasa por `sys.argv` en el bloque `if __name__ == "__main__"` (commits `75f90b1` "capturar y reproducir cadena de imagen" y `73872b6` "validar rings antes del replay offline"), sin guarda para la invocación sin argumentos que hace `run_all`.

## Fix + test de regresión
- Guarda al inicio: `if len(sys.argv) < 2: raise SystemExit(unittest.skip(...))`, o un `tests/run_image_ring_replay.sh` que compile `tools/image_ring_replay.cpp` y ejecute el test con el binario.
- Regresión: `tests/run_all.sh` en worktree limpio con la suite en PASS o SKIP (no FAIL).

## Re-verificación oc-qa (2026-10-21, HEAD `6ee09ac`, worktree limpio)
Sigue **rojo en HEAD**: `run_all` FAIL en `test_image_ring_replay.py` (`IndexError: list index out of range`,
línea 75) y en el flamante `test_image_ring_replay_standalone.py` (contrato de `f1ac5b9`: la suite debe
saltarse sin insumo). El árbol compartido da verde **sólo por WIP no commiteado** en
`tests/test_image_ring_replay.py`; el fix tiene que entrar commiteado para cerrar.

## Verificación y cierre (oc-qa, 2026-10-21; HEAD `6a25c55`, worktree limpio)
- Fix `b68d778`: `test_image_ring_replay.py` resuelve el binario por variable de entorno y
  `CMakeLists.txt` lo inyecta en el target `image_ring_replay_input`; sin binario la suite se **saltea**
  (regresión `test_image_ring_replay_standalone.py` de `f1ac5b9`).
- Worktree limpio de HEAD (`shared/*` symlinkado, sin artefactos de `out/` del árbol compartido):
  `run_all` → **PASS=80 FAIL=1 SKIP=5**, con `PASS test_image_ring_replay.py (skip …)` y
  `PASS test_image_ring_replay_standalone.py`. El único rojo restante es BUG-051.
- `build_vita` OK (VPK 1.944.584 B md5 `8b097273bfdf8b8af7bf2b6862258017`); qemu 20 s: 37 PNG,
  veredicto OK (`fin: shutdown requested`, `load=6.03`).
