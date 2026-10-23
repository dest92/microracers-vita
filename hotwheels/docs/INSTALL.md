# Instalación — Hot Wheels Micro Racers en PS Vita

Este proyecto **no incluye el juego**: el VPK sólo trae el runtime (`eboot.bin`), el icono y el
LiveArea propios. Vos aportás tu copia legal de *Hot Wheels Micro Racers* (PC) y sus datos se copian
a la consola como se explica abajo.

## Requisitos

- PS Vita / PSTV con HENkaku/Enso (para instalar homebrew).
- `ux0:` con al menos ~20 MB libres para los datos (más el VPK, ~10 MB).
- El VPK `hotwheels-vita.vpk` (se genera con `tools/build_vita.sh`) y los datos extraídos del juego
  en la PC (salida de HW-001: `hotwheels/extracted/original/Program_Files/`).

## 1. Instalar el VPK (runtime + LiveArea)

1. Conectá la Vita por USB/FTP o usá la tarjeta en un lector.
2. Copiá `hotwheels-vita.vpk` a la consola.
3. En VitaShell, navegá al VPK y elegí **Instalar**. Aparecerá el burbuja "HotWheels Vita"
   (`TITLE_ID = HWMR00002`, versión 01.00) con su icono y fondo propios.

> El VPK sólo contiene `eboot.bin`, `sce_sys/param.sfo`, `sce_sys/icon0.png` y
> `sce_sys/livearea/contents/{bg.png,startup.png,template.xml}`. **No** contiene archivos del juego.

## 2. Copiar los datos del juego (copia legal del usuario)

Los datos van en **`ux0:data/hotwheels/`**. El runtime los busca ahí (`VITA_DATA_PATH` en
`tools/app.conf`); en particular `HotWheels.exe` y la carpeta `DATA/`.

### Opción A — script (recomendado)

Con la Vita montada en la PC en un punto de montaje (p. ej. `/run/media/$USER/PSVITA`):

```sh
tools/stage_data.sh /run/media/$USER/PSVITA/ux0/data/hotwheels
```

Copia el contenido de `extracted/original/Program_Files/` (HotWheels.exe, DATA/, DIAG/, EREG/,
HWICON.BMP). `EREG/` no lo usa el juego: agregá `--no-ereg` para omitirlo.

Si corrés **el `stage_data.sh` del paquete extraído** (donde no está el repo), indicá de dónde
salen tus datos con `SRC`:

```sh
SRC=/path/to/Program_Files ./stage_data.sh /path/to/mount/ux0/data/hotwheels
```

Reemplazá ambos ejemplos por las rutas reales. Agregá `--no-ereg` al final si querés omitir EREG.

En una instalación nueva, el script inicializa la resolución de carrera en 800×600
(`settings.dat`, índice 2) y conserva el modo 3D inicial del juego. Si la fuente trae
preferencias, sólo ajusta ese índice y conserva las demás líneas; si la fuente no trae
el archivo y el destino ya tiene preferencias, no las reemplaza.

También acepta los datos ya copiados al lado del script (`./Program_Files/` o
`./extracted/original/Program_Files/`). Sin ninguna de esas rutas el script sale con el aviso
y no copia nada.

### Opción B — manual

Copiá **el contenido** de `hotwheels/extracted/original/Program_Files/` dentro de
`ux0:data/hotwheels/`, de modo que quede:

```
ux0:data/hotwheels/
├── HotWheels.exe      (requerido)
├── HWICON.BMP
├── DATA/              (requerido: MENU, INTRO, LANG.TXT, SINUS.SIN, ...)
├── DIAG/
└── EREG/              (opcional)
```

> Antes de copiar podés extraer los archivos con `tools/extract_advanced_installer.py`
> (ver `docs/triage.md`). El `.exe` de 224 MB es el instalador, **no** el juego; el juego es
> `HotWheels.exe` (430 KB).

## 3. Regenerar / personalizar el VPK

```sh
# Build completo (compila el runtime y empaqueta):
tools/build_vita.sh
# Salida: build-vita/hotwheels-vita.vpk

# Sólo (re)generar los assets propios de LiveArea/icono:
python3 tools/make_sce_sys.py            # escribe hotwheels/sce_sys/

# Reempaquetar sin recompilar (requiere build-vita/eboot.bin):
tools/package.sh build-vita
```

`tools/app.conf` es la fuente única de verdad de `TITLEID`, nombre y versión. Si cambiás el
`TITLE_ID`, desinstalá la versión anterior antes de reinstalar.

## Controles (100% con botones físicos, sin touch ni teclado)

El juego original usa teclado (y mouse opcional). El port traduce los botones de la Vita a las
teclas del juego, así que se juega completo con el pad. Fuente del relevamiento:
`extracted/iso/MANUAL.TXT` §Keyboard Controls y strings de `HotWheels.exe`.

| Botón Vita | Acción |
|---|---|
| D-pad / stick izquierdo ↑↓←→ | Menús: navegar · Carrera: acelerar / frenar / doblar |
| Cross (X) / Triangle (△) | Menú: confirmar (Enter) |
| Circle (O) / Square (□) | Menú: volver atrás (Backspace) |
| Start | Pausa / volver al menú principal (Esc) |
| L1 / L2 | Turbo |
| R1 / R2 | Espejo retrovisor |
| Select | Reservado (sin uso) |

- **Menús**: flechas + Cross (confirmar) + Circle (volver). No se necesita mouse.
- **Carrera**: stick/d-pad para conducir, L para turbo, R para espejo.
- **Pausa**: Start.
- El touch frontal (→ mouse) es **opcional** y sólo una comodidad; el juego es plenamente jugable
  sin usarlo.

El mapa es configurable en `ux0:data/hotwheels/hwvita.ini` (sección `[buttons.pad0]`, p. ej.
`cross=RETURN`, `l1=RSHIFT`; ver `docs/` y la spec 007).

## Verificación

```sh
tests/test_vpk.sh
```

Comprueba que el VPK tenga las entradas esperadas (`unzip -l`), que el `param.sfo` lleve el
`TITLEID` correcto, que no haya archivos del juego y que los assets sean reproducibles.

## Notas legales

No se distribuye ni se commitea ningún archivo del juego (`.exe`, `DATA/`, texturas, audio). Este
repositorio sólo contiene el runtime y assets originales generados por script. Usá una copia
legítima de *Hot Wheels Micro Racers*.
