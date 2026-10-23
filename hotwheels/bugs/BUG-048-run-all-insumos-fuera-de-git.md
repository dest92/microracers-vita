# BUG-048 — HEAD rojo en `run_all`: `test_dsound_oracle.py` exige `HW_DS_REPLAY` y `run_ds_cursor_contract.sh` compila contra `out/hw069-winx86` (fuera de git)

Nivel: **medido** (oc-qa, 2026-10-21 ~21:17, HEAD `9580f82`, worktree aislado + `shared/*` symlinkado). Dueño: **sol-audio** (HW-069, DSound). Severidad: mayor (`run_all` rojo → el gate de humo pierde señal). Estado: **cerrado** (`466b032`; verificado por oc-qa en HEAD `466b032`).

## Síntoma (exacto, logs)
`hotwheels/out/qa-clean-m8/`:
```
FAIL test_dsound_oracle.py (rc=1)
KeyError: 'HW_DS_REPLAY'          (5 errores; tests/test_dsound_oracle.py:107 y :42/:57/:60/:94)

FAIL run_ds_cursor_contract.sh (rc=1)
tests/test_ds_cursor_contract.cpp:12:10: fatal error: ds_cursor.h: No existe el archivo o el directorio
```
`run_all: PASS=54 FAIL=7 XFAIL=0 SKIP=3` (los otros 5 FAIL están en BUG-049/050/051/052 y 2 ya verdes).

## Reproducción mínima
```bash
git worktree add --detach /tmp/q 9580f82
ln -s $PWD/shared/* /tmp/q/shared/           # infra no versionada
cd /tmp/q/hotwheels
python3 tests/test_dsound_oracle.py          # KeyError HW_DS_REPLAY
bash   tests/run_ds_cursor_contract.sh       # ds_cursor.h no existe
```
En el árbol compartido `test_dsound_oracle` también falla (mismo `KeyError`); `run_ds_cursor_contract.sh` **pasa**, pero sólo porque existe el artefacto `hotwheels/out/hw069-winx86/src/runtime/ds_cursor.h`.

## Esperado vs observado
- Esperado: una suite que necesita un insumo externo (binario de replay, header de winx86) se **saltea** (`skipUnless`, como `test_qemu_insn_oracle.py:36` con docker) o queda **XFAIL** en `run_all` mientras la implementación no esté commiteada.
- Observado: `run_all` las ejecuta como `python3 tests/test_*.py` sin env ni argv → rojo para cualquiera; `ds_cursor.h` **no existe en ningún árbol versionado** (ni en `d2vita/D2Vita/third_party/winx86/src/runtime`), sólo en la copia de scratch `out/hw069-winx86` creada por el agente de audio.

## Causa raíz (verificada)
- `tests/run_dsound_oracle.sh:8` es el único que setea `HW_DS_REPLAY`; el `.py` lo lee con `os.environ[...]` sin guarda (commit `841a51d`).
- `tests/run_ds_cursor_contract.sh:6` usa `WINX86_DIR="${WINX86_DIR:-$HW/out/hw069-winx86}"`; el header nuevo (`ds_cursor.h`) vive sólo en esa copia fuera de git (test `8d5a480`, "rojo" TDD) y `run_all` no lo marca XFAIL.

## Fix + test de regresión
1. `test_dsound_oracle.py`: `@unittest.skipUnless(os.environ.get('HW_DS_REPLAY'), ...)` en la clase (precedente: `test_qemu_insn_oracle.py`).
2. `ds_cursor.h`: commitearlo en el winx86 compartido y apuntar `WINX86_DIR` al árbol versionado; o `RUN_ALL_XFAIL="run_ds_cursor_contract.sh"` hasta que la impl entre.
3. Regresión: `RUN_ALL_XFAIL` vacío + `tests/run_all.sh` verde en worktree limpio.

## Verificación y cierre (oc-qa, 2026-10-21; HEAD `466b032`)
- Fix `466b032`: `run_ds_cursor_contract.sh` materializa `ds_cursor.h` desde el parche **versionado**
  `hotwheels/patches/winx86-0014-dsound-cursor-contract.patch` en un `mktemp -d` (ya no usa `out/hw069-winx86`);
  `test_dsound_oracle.py` saltea con `skipUnless` los 5 casos que necesitan `HW_DS_REPLAY`.
- Worktree aislado de HEAD (`shared/*` symlinkado, sin artefactos de `out/` del árbol compartido):
  `run_all` → **PASS=59 FAIL=2 SKIP=3**, con `PASS run_ds_cursor_contract.sh` y `PASS test_dsound_oracle.py`.
- `build_vita` OK (VPK 1.938.867 B md5 `90c4282b773d00eabbb0390f92c57e59`); qemu 20 s: 1945 frames,
  `missing=0`, veredicto OK.
- Los 2 FAIL restantes son de otros bugs abiertos: `run_consumer_phase.sh` (BUG-051) y
  `test_image_ring_replay.py` (BUG-049).
