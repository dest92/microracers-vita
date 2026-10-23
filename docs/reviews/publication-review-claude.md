# Review acotado de publicación — microracers-vita (Claude, 2026-10-22)
Alcance: export `hotwheels/out/microracers-vita-publication/`, sólo licencias/atribuciones, copyrighted/secretos, claims del README. Sin review de runtime ni perf.

## Veredicto: APRUEBA CON 1 BLOQUEO MENOR (privacidad), arreglable en minutos

### Bloqueo (corregir antes del push)
- **Datos privados de infra del usuario** (no son secretos/credenciales, pero no deben ser públicos): IPs LAN `192.0.2.10`/`192.0.2.20`, host `remote-worker`, usuario Windows `USER`, rutas `/home/user/...`. Archivos:
  `hotwheels/specs/050-rea-ghidra12.md`, `specs/051-worker-remoto.md`, `bugs/BUG-025…`, `bugs/BUG-035…`, `bugs/BUG-047…`,
  `tools/vita_watchdog.sh`, `tools/vita_deploy.py`, `tools/remote_run.sh`, `tools/diag_minvgl/main.c`, `tools/torture/test_x87_oracle.sh`.
  Fix: reemplazar por variables (`VITA_IP`, `REMOTE_HOST`, `$HOME`) o placeholders; volver a correr el grep `192\.168\.|USER|remote-worker|/home/user`.

### OK (verificado)
- No hay credenciales/tokens (grep de `ghp_`, `sk-`, claves privadas, `password=`/`api_key=`: sin hits reales).
- Sin datos retail versionables: `.gitignore` excluye exe/dll/wav/iso/vpk/elf y `**/build*/`, `**/out/`. Los `build/`, `build-vita/` (eboot, VPK, `d3d_golden.png`, logs `.fastbuild`) quedan ignorados — **verificar con `git status --ignored` en el repo nuevo antes del primer commit** que no entren.
- PNGs versionados: `sce_sys/*` es arte genérico propio ("HW" + auto genérico, sin assets del juego); `docs/img/bug032-pattern-host-point.png` es patrón de test sintético. OK. (Nota: "HW" evoca la marca; riesgo bajo, el disclaimer de no afiliación cubre).
- `tools/race-*.dat`: settings de 36 bytes generados por nosotros (dígitos), no datos del juego. OK.
- Docs RE: sólo nombres `FUN_00…`/direcciones y descripciones; no hay pseudocódigo ni volcados del binario. `docs/wine-ref/*.txt` son conteos/trazas de llamadas API. Aceptable.
- Licencias: GPL-3.0 raíz; winx86 MIT con LICENSE y THIRD-PARTY-NOTICES; Box86 MIT (ptitSeb) atribuido; x86primop, khash, DejaVu, shaders vitaGL LGPL-3.0 (+ texto GPL y LGPL) y shaders D3D6 GPL-3.0 con provenance. MIT/LGPL → GPL-3.0 compatible. Disclaimer Mattel/no afiliación presente. No se incluyen `libshacccg.suprx`, firmware ni plugins kernel.
- Symlinks `d2vita/D2Vita/third_party/{winx86,kubridge-stub,vpmu-stub}` relativos e internos al repo: OK (los clones en Windows sin symlinks romperán el build; mencionarlo en BUILDING si no está).
- README: claims prudentes ("experimental", sin claim de fps estable/compatibilidad completa, requiere copia legal propia). OK.

### Menores (no bloquean)
- El pedido menciona `NOTICE`; el archivo real es `THIRD-PARTY-NOTICES.md` (está bien, sólo consistencia).
- Confirmar que la licencia upstream de D2Vita permite relicenciar los shaders adaptados como GPL-3.0 (provenance dice GPL-3.0; si D2Vita es MIT, citar su copyright también).

## Re-review de cierre (README c502c78 + fix privacidad)
**APRUEBA el push** (fuente sólo, sin VPK).
- Grep `192\.168\.|USER|remote-worker|/home/user|nodetech|matias` fuera de build/out: 0 hits. Helpers exigen `VITA_IP`/`--ip`, `REMOTE_RUN_HOST`/`REMOTE_RUN_ROOT` (`:?`), diag usa 192.0.2.20 (TEST-NET) overrideable. OK.
- BUILDING documenta symlinks en Windows y variables. D2Vita GPL-3.0 confirmado → shaders GPL-3.0 coherentes.
- README: sin claims de fps/compatibilidad; 40 Hz descrito como cadencia original, afinidades como intención no medida, A/B como exploratorios. OK.
- Condición: el export hoy vive dentro del monorepo (no tiene `.git` propio). Al hacer `git init` en el export, correr `git status --ignored` y `git ls-files | grep -E '\.(vpk|bin|elf|wav|log)$|build'` antes del primer commit: debe salir vacío. Revisar también que el autor/email del commit sea el que el usuario quiere público.
