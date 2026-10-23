# Spec 008 — Empaquetado VPK instalable (HW-017)

- Feature: HW-017 · Milestone: M6 · Dueño: ports · Estado: implementada (test host verde), pendiente review
- Depende de: HW-005 (esqueleto/build `tools/build_vita.sh`). No depende del renderer.

## Problema
El build actual (`tools/build_vita.sh`) genera un VPK que sólo contiene `eboot.bin` y `param.sfo`,
sin icono ni LiveArea, con `TITLE_ID=HWMR00001` **que colisiona** con el homebrew "hello" de HW-004
(`hotwheels/tools/vita3k/hello/CMakeLists.txt`). Además:
- no hay assets propios (icono/background/startup): hay que generarlos, nunca tomarlos del juego;
- no hay forma de que el usuario prepare `ux0:data/hotwheels/` con **su** copia legal del juego;
- no hay documento de instalación.

Sin esto, M6 ("VPK instalable + guía de instalación") no se puede cerrar aunque el port funcione.

## Contrato observable

1. **`hotwheels/tools/app.conf`** — fuente única de verdad: `VITA_TITLEID`, `VITA_APP_NAME`,
   `VITA_VERSION`, `VITA_DATA_PATH` (`ux0:data/hotwheels`) y `VITA_EXE_NAME` (`HotWheels.exe`).
   `TITLEID=HWMR00002` (distinto del hello `HWMR00001`).
2. **`hotwheels/tools/make_sce_sys.py`** — genera, sólo con stdlib (`zlib`+`struct`), los assets de
   LiveArea/icono en `hotwheels/sce_sys/`: `icon0.png` (128×128), `livearea/contents/bg.png`
   (840×500), `livearea/contents/startup.png` (280×158) y `livearea/contents/template.xml`.
   Salida **determinista** (sin timestamps, PNG filtro 0, zlib nivel 9). No usa material del juego.
3. **`hotwheels/tools/package.sh [OUT]`** — con un `eboot.bin` ya construido produce
   `OUT/param.sfo` (`vita-mksfoex`, `TITLE_ID`/`APP_VER`/`ATTRIBUTE2=12`) y `OUT/hotwheels-vita.vpk`
   (`vita-pack-vpk`) con `eboot.bin`, `sce_sys/param.sfo` y los 4 assets.
4. **`hotwheels/tools/build_vita.sh`** — al final del build delega en `package.sh` (no duplica el
   TITLEID). La parte de compilación/enlazado no cambia.
5. **`hotwheels/tools/stage_data.sh <DESTINO> [--no-ereg]`** — copia el contenido de
   `extracted/original/Program_Files/` (salida de HW-001) a la carpeta del usuario
   (`ux0:data/hotwheels/`). **No** copia datos del juego al repo ni al VPK.
6. **`hotwheels/docs/INSTALL.md`** — pasos de instalación (VPK + datos del usuario).

Invariantes:
- El VPK **no** contiene ningún archivo del juego (`HotWheels.exe`, `DATA/`, `EREG/`, `HWICON.BMP`).
- `param.sfo` lleva exactamente `VITA_TITLEID` y `APP_VER=VITA_VERSION`.
- Los PNG/XML commiteados son byte a byte iguales a lo que produce `make_sce_sys.py`.

## Criterios de aceptación (cada uno ⇒ un test)

Test: `hotwheels/tests/test_vpk.sh` (rápido, no compila el juego; empaqueta un `eboot.bin` dummy).
- [x] CA1 `package.sh` con un eboot de prueba genera un VPK válido (zip legible por `unzip -l`).
- [x] CA2 `unzip -l` lista: `eboot.bin`, `sce_sys/param.sfo`, `sce_sys/icon0.png`,
  `sce_sys/livearea/contents/{bg.png,startup.png,template.xml}`.
- [x] CA3 `param.sfo` contiene `TITLE_ID=HWMR00002` (el de `app.conf`, no el del hello).
- [x] CA4 el VPK no contiene material del juego (grep negativo de `HotWheels.exe|/DATA/|EREG|HWICON`).
- [x] CA5 los assets de `sce_sys/` commiteados == salida de `make_sce_sys.py` (reproducibilidad).
- [x] CA6 (manual, `[host]`) `package.sh` sobre `hotwheels/build-vita/` (eboot real del esqueleto)
  produce un VPK con las 6 entradas y el TITLEID correcto.
- [ ] CA7 (posterior, Vita3K) el VPK instala y aparece con icono/LiveArea. No bloquea `done` de
  HW-017 (requiere el runtime HW-009).

SKIP (exit 77) si faltan `python3`, `unzip`, `vita-mksfoex` o `vita-pack-vpk`.

## Diseño

- **TITLEID**: `HWMR00002`. `HWMR00001` está tomado por el hello de HW-004; no se toca ese archivo
  (es de otro dueño). Si más adelante el hello se retira, el juego puede migrar a `HWMR00001`, pero
  eso es una decisión del orquestador.
- **Para qué no se usa Pillow**: los assets se dibujan con un escritor PNG propio (stdlib). Así el
  generador corre en cualquier host con Python 3 y el resultado es determinista y auditable.
- **Rutas**: el runtime (`src/runtime_vita.cpp:44`) ya usa `ux0:data/hotwheels/` y
  `HotWheels.exe`; `app.conf` lo documenta, no lo cambia.
- **LiveArea**: `template.xml` sin ítem de configuración (el juego no tiene companion). Si más
  adelante hace falta un `-config`, se agrega el `<frame>` correspondiente.
- **Tamaño VPK**: ~10.3 MB (dominado por `eboot.bin`); assets ~5.5 KB.
- **Datos del usuario**: `stage_data.sh` copia los ~20 MB de `Program_Files/` (DATA 17 MB, DIAG 304 KB,
  EREG 2.4 MB). `--no-ereg` omite `EREG/` (no lo usa el juego, FINDINGS #11).

## No-objetivos

- Compilar el juego dentro del test (el test empaqueta un eboot dummy).
- Validar instalación en Vita3K/consola (CA7, posterior).
- Empaquetar o distribuir assets/binarios del juego (regla de `.gitignore`).
- Cambiar el proceso de compilación/enlazado de `build_vita.sh` (sólo se delega el empaquetado).
- Firma/parcheo del `eboot` (el esqueleto ya usa `UNSAFE` para RWX).

## Riesgos / preguntas abiertas

- `build_vita.sh` es de `impl`. El cambio es mínimo (dos líneas → `bash tools/package.sh`); queda
  registrado en `coordination/impl.md`/`coordination/ports.md`. Si impl lo reescribe, `package.sh`
  sigue siendo reusable (contrato: recibe `OUT` con `eboot.bin`).
- ¿Vita3K requiere `pic0.png`? No es obligatorio en el formato VPK; se puede agregar si CA7 lo pide.
- ¿Conviene commitear los PNG generados o sólo el script? Se commitean para que el VPK se pueda
  empaquetar sin correr Python; CA5 garantiza que sean idénticos al script.

## Evidencia (cómo se valida)

- `[host]` `bash hotwheels/tests/test_vpk.sh` → PASS (CA1–CA5). Medido 2026-10-02.
- `[host]` `bash hotwheels/tools/package.sh hotwheels/build-vita` sobre el eboot real del esqueleto →
  VPK de 6 entradas, `TITLE_ID=HWMR00002` (CA6). Medido 2026-10-02.
- CA7 pendiente (Vita3K/consola).
