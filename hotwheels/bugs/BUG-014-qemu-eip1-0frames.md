# BUG-014 — smoke qemu: 0 frames, fault EIP=0x1 tras DirectSoundCreate (regresión desde c7be53a)

Nivel: **medido en host/qemu** (qa, 2026-10-03 ~02:20, HEAD 7db43ab + working tree). Dueño: **integ** (con render si es GL). Severidad: bloqueante (smoke FAIL, sin frames). Estado: **cerrado (2026-10-03, mimo)**.

## Evidencia
- Reproducido 2 veces: `out/run-20261003-021753/` y `out/run-20261003-021946/`: `[dynarec86] Run() interpreter stub hit at EIP=0x1`, `FAULT ... EIP=0x1 faultAddr=0x1 ESP=0x0b1ffc6c EAX=0x1`, `frames=0 missing=0`, justo tras `DirectSoundCreate -> DS_OK`.
- Último verde: c7be53a (smoke 295 frames, run_all 17/0). Ahora run_all sigue 17/0 (tests host no lo ven); build_vita OK (la 1ª corrida dio rc=255 con log vacío: probable colisión con otro build concurrente, no reproducida).
- Commits desde entonces con src: d2d5064 (deadzone), bcdbe33 (estado GL P8), **7db43ab** (`dinput_bridge.cpp`: di_args 4→3 en un slot, "corregir aridad"). Además hay cambios sin commitear en src/host/{host_main,integrate}.cpp, integrate.h, src/io/boot_selector.cpp.
- Hipótesis (NO verificada): la aridad stdcall corregida en 7db43ab desbalancea el stack en la llamada guest (retorno a 0x1). Bisectar 7db43ab vs diff sin commitear.

## Acción
Bisect: revertir sólo la línea di_args en un worktree y correr smoke etapa 3; si no, probar sin el diff de host/io.

## Cierre (mimo, 2026-10-03) — bisect hecho: la causa era la aridad de 7db43ab
La hipótesis se confirmó por el mismo camino que sugiere este archivo: `f006535` revierte **sólo**
la línea `di_args` de `EnumDevices` (3→4, la firma real de `dinput.h:2176` es
`dwDevType, callback, pvRef, dwFlags`) y pasa `dwFlags` en `tests/test_dinput_bridge.cpp`.
Con ese cambio (worktree con el mismo WIP de host/io que estaba en la medición de BUG-014):

- `tools/smoke.sh` → **PASS** (`out/smoke-20261003-022155.md`): run_all 17/0, build Vita OK,
  **qemu 304 frames, veredicto=OK (fin limpio)** — contra 0 frames y `EIP=0x1` medidos 2 veces
  con 7db43ab — y Vita3K 21 shots / `boot_game_once=1`.
- `ctest` 10/10 (incluye `dinput_bridge` con aridad 4).

Nivel de evidencia: **qemu + emulador**. El WIP de `src/host`/`src/io` descartado como causa
(presente también en la corrida PASS). Regresión: `dinput_bridge` en ctest + smoke etapa 3.
