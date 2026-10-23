# Spec 015 — Paquete "listo para consola" (HW-028)

- Feature: HW-028 · Milestone: M6 · Dueño: ports · Estado: implementada (tests host verdes), pendiente review
- Depende de: HW-017 (VPK/LiveArea, review), HW-021 (present Vita, in_progress). **No ejecuta consola.**

## Problema
Falta un artefacto único y una guía para cuando el usuario autorice probar en la Vita real: el VPK del
runtime, la ubicación de los datos del usuario, el checklist de validación y una forma de verificar que
el paquete está completo sin tocar la consola. Hoy el VPK existe (`tools/build_vita.sh` → `package.sh`)
pero no hay manifiesto, ni checklist, ni un armado reproducible del paquete.

## Contrato observable
- `tools/package_console.sh [--build] [--vpk PATH] [--out DIR]`:
  - `--build`: corre `tools/build_vita.sh` (compila el runtime completo y genera el VPK).
  - sin `--build`: usa un VPK existente (`build-vita/hotwheels-vita.vpk`, o `--vpk PATH`).
  - arma `DIR` (default `hotwheels/dist/console-<stamp>/`, gitignoreado) con:
    `hotwheels-vita.vpk`, `INSTALL.md`, `CONSOLE-CHECKLIST.md`, `stage_data.sh`, `MANIFEST.txt`.
  - `MANIFEST.txt`: `TITLE_ID`, `APP_VER`, ruta/size/sha256 del VPK, lista de archivos del paquete,
    layout de datos requerido (`ux0:data/hotwheels/`).
  - **No** copia datos del juego (el usuario aporta su copia legal) ni ejecuta nada en consola.
- `docs/CONSOLE-CHECKLIST.md`: pasos de instalación, validación por hito (con cómo observarlo),
  recolección de logs y triage. Sólo se usa con autorización explícita del usuario.
- `tests/test_console_package.sh`: arma un paquete a partir de un VPK de prueba y verifica contenido.

## Criterios de aceptación (cada uno ⇒ un test)
Test host `tests/test_console_package.sh` (sin consola; SKIP 77 si faltan `unzip`/`vitasdk`):
- [ ] **CA1** `package_console.sh --vpk <test.vpk> --out <dir>` arma los 5 archivos del paquete.
- [ ] **CA2** `MANIFEST.txt` contiene el `TITLE_ID` de `tools/app.conf`, el sha256 y el size del VPK.
- [ ] **CA3** `unzip -l` del VPK copiado lista `eboot.bin`, `sce_sys/param.sfo` y los assets de LiveArea.
- [ ] **CA4** el paquete **no** contiene material del juego (`.exe`, `DATA/`, `EREG/`, `HWICON`).
- [ ] **CA5** `CONSOLE-CHECKLIST.md` existe y tiene las secciones requeridas (requisitos, instalar,
  datos, validación, logs, triage) — grep de encabezados.
- [ ] **CA6** (manual, `[host]`) `package_console.sh --build` en esta máquina genera VPK + paquete sin
  errores (no valida en consola).
- [ ] **CA7** el MANIFEST, INSTALL y checklist dan el comando con `SRC` para ejecutar el stage script
  desde el paquete extraído; la prueba lo extrae y lo ejecuta con fixture sin datos del juego.

## Diseño
- Reusa HW-017 (`tools/app.conf`, `tools/package.sh`, `sce_sys/`) y HW-020 (`stage_data.sh`).
- El paquete no incluye `sce_sys/` por separado: ya van dentro del VPK; el manifiesto lista lo que hay.
- `dist/` se gitignorea; el paquete se regenera determinísticamente salvo el VPK (build).
- El checklist declara explícitamente los niveles de validación (Vita3K → consola) y que la consola
  requiere autorización (AGENTS.md).

## No-objetivos
- Ejecutar en consola real (prohibido salvo orden del usuario).
- Empaquetar o distribuir datos del juego.
- Validar performance en consola.

## Riesgos / preguntas abiertas
- HW-021 (present) in_progress: el VPK compila con el present actual; el checklist incluye el hito de
  presentación como "esperado si HW-021 está integrado".
- ¿`kubridge` obligatorio? El runtime soporta VM sin kubridge (HW-009); el checklist lo marca opcional.

## Evidencia
- `[host]` `tests/test_console_package.sh` (CA1–CA5).
- `[host]` `package_console.sh --build` sobre `build-vita/` (CA6), sin consola.
