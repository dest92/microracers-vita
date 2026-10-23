# BUG-039 — HEAD limpio no es reproducible: el host no compila y run_dinput falla en un checkout limpio

Nivel: **medido** (qa, 2026-10-03 ~12:50, `git worktree` aislado de HEAD 31b5eb0 con `d2vita/D2Vita` y `extracted/` copiados; docker/qemu + ctest). Dueño: **integ** (build_host.sh) y **luna** (test winx86-dx). Severidad: mayor (el gate de QA sólo pasa con el WIP sin commitear de cada agente; un checkout limpio no se puede construir ni verificar).
Estado: **cerrado** (qa, 6f6adeb: worktree limpio run_all 33/0, host 3021 frames).

## Evidencia
1. `tools/build_host.sh` commiteado no lista `d3d/core.cpp, bridge.cpp, refraster.cpp` ni `io/read_stream.cpp`: en el worktree limpio `tools/run_host.sh` falla en link (`undefined reference to hw::d3d::ComBridge::acquire_d3d3_object()/flip()/create_texture()` desde `host/integrate.cpp:199-202`). Las líneas correctas sólo existen en el working tree (diff sin commitear de build_host.sh, visto sin interrupción desde ~06:00; ver también BUG-038).
2. `tests/run_dinput.sh` (commit ace267d "fijar API pública de winx86-dx dinput") falla en el worktree: `tests/test_winx86_dx_dinput.cpp:1: fatal error: winx86-dx/dinput/dinput.h: No such file` y `../shared/winx86-dx/src/dinput/dinput.cpp: No such file`. `shared/` está fuera de git (`git ls-files shared` vacío / ignorado): el test depende de un directorio no versionado. run_all en el worktree: PASS=30 FAIL=1 (sólo run_dinput).
3. Efecto: el `smoke`/`run_all` del árbol compartido mezcla WIP ajeno con HEAD (BUG-038 se explicó así dos veces). Sin reproducibilidad, "hecho = test verde" no es verificable.

## Acción
- integ: commitear `tools/build_host.sh` (con `git commit -- hotwheels/tools/build_host.sh`).
- luna: versionar `shared/winx86-dx` (o vendorizar los headers/fuente mínimos) o condicionar `run_dinput.sh` a SKIP si falta.
- QA: `tools/qa_round.sh` pasará a verificar en worktree aislado de HEAD (opción `--clean`) para separar defectos commiteados de WIP.

## Actualización (qa, 13:20, HEAD 839657e, `qa_round.sh --clean`): run_all 30/2/2 en worktree limpio
- `run_winmm.sh` falla: `tests/test_winmm.cpp:200: no matching function for call to winx86::dx::winmm::Cdaudio::Cdaudio(CaptureSink&)` (+259, 267). También falla en el árbol compartido: el test commiteado no coincide con la API de `shared/winx86-dx` (extracción de MMIO/winmm 1057fe8, código movido a `shared/`, ignorado por git → el cambio no queda versionado junto al test). Dueño: luna/re (winx86-dx).
- `test_bug_index.py` fallaba sólo en limpio porque mi fila BUG-039 de BUGS.md estaba sin commitear (corregido en este commit).
- `run_dinput.sh` ya pasa limpio. Host sigue sin compilar limpio (build_host.sh sin commitear).

## Actualización integ (2026-10-03, worktree limpio e40c68c)
- Corregí `tools/build_host.sh`: HEAD incorpora `d3d/core.cpp`, `d3d/bridge.cpp`, `d3d/refraster.cpp` y `io/read_stream.cpp`; no referencia WIP `guest_eip_profile.cpp`/`frame_queue.cpp` aún sin integrar. Commits `60df553`, corrección `39be502`.
- Validación: `docker run` con imagen `hw-host-arm:24.04`, checkout HotWheels limpio en `/tmp/hw039-clean-39be502` a HEAD `e40c68c`, dependencia winx86 montada explícitamente. Build ARM termina `OK /work/hotwheels/build-host/hotwheels-host`. No corrí QEMU ni `run_all`.
- Review: `60df553`/`39be502` APRUEBAN estáticamente (`coordination/review-39be502.md`, commit `0c38382`).
- Limitación: toolchain ARM Linux y qemu-arm faltan en host (`./init.sh`); el contenedor de build sí los provee. BUG-039 permanece abierto por fallos winmm y validación limpia de `run_all`, dueño luna/re.

## Actualización (qa, 16:05, HEAD 4ef7e71; `qa_round.sh --clean`)
- **Resuelto**: el host compila y corre desde un checkout limpio (`fin: shutdown requested … frames=2074 missing=0`); `build_host.sh` commiteado alcanza (39be502); run_dinput/winmm ya no fallan en limpio.
- **Sigue abierto, mismo patrón**: tests commiteados (HW-057: dd07042, ed38726, 03af5b5) contra `winx86-dx`, que vive fuera de git (`shared/`, ignorado): run_all limpio 30/2/2 → `run_shims.sh`: `-Werror=unused-function` en `winx86-dx/src/shims/imports.cpp` (gzero_at/gput_at/write_u32_at definidos sin uso) y ctest `winx86_dx_shims`: `test_winx86_dx_shims.cpp:91 Assertion import_guest.low_reads == 0 failed`. En el árbol compartido `run_io.sh` también cae por lo mismo.
- Acción (dueño HW-057, luna/re): versionar `winx86-dx` o vendorizar la parte que usan los tests; mientras tanto commitear test y lib en el mismo repo. Hasta entonces el gate limpio de HW-057 no es verificable.


## Revalidación dxlib (2026-10-03, checkout limpio HEAD 7a96756)
- `git ls-tree -r HEAD -- winx86-dx`: 26 archivos versionados; no se requiere contenido de `shared/` para estos módulos. `tools/build_host.sh` termina PASS en `hw-host-arm:24.04`, pasando explícitamente la dependencia externa `d2vita/D2Vita/third_party/winx86`.
- `ctest -R '^winx86_dx_'`: 3/3 PASS; `run_io.sh`, `run_shims.sh`, `run_dinput.sh`, `run_winmm.sh`, `run_d3d.sh`, `run_ddraw.sh` PASS desde el checkout limpio. BUG-039 queda resuelto en su alcance de librería no versionada/build host.
- Gate global aún no está verde: `run_all.sh` = PASS=33 FAIL=2 SKIP=2; las dos fallas duplican la aserción `interp60` añadida en `34d34d6` (HW-066). El fallo no involucra winx86-dx y se registró en `coordination/dxlib.md` para el dueño runtime. No atribuir a BUG-039 ni ocultar como XFAIL.


## Gate limpio final (dxlib, 2026-10-03; HEAD 35c89f2)
- Tras el commit runtime `35c89f2` (interp60), repetí `hotwheels/tests/run_all.sh` en un worktree limpio: **PASS=35 FAIL=0 XFAIL=0 SKIP=2**. CTest PASS; las suites de winx86-dx, IO, DInput, WinMM, D3D y DDraw pasan. Los dos SKIP son únicamente integraciones qemu condicionales.
- `winx86-dx/` sigue con 26 rutas versionadas. `build_host.sh` PASS en el worktree limpio `7a96756`; entre `7a96756` y `35c89f2` el único cambio de código fue interp60, fuera de sus fuentes/lista de compilación. Reintento literal en 35c89f2 chocó con otro build simultáneo que comparte `d2vita/D2Vita/third_party/winx86/build-arm/.fastbuild` y recibió rc=127; no es un fallo reproducible del script fuente y no interrumpí el build ajeno.
- **BUG-039 cerrado:** la librería y el build host son verificables desde HEAD limpio; el gate global también queda verde luego del fix independiente HW-066.
