# Checklist de prueba en PS Vita real (HW-028)

> **Sólo ejecutar con autorización explícita del usuario** (AGENTS.md: la consola real no se usa salvo
> orden). Este documento es para esa sesión. Nada de acá se corre en el CI.

## 0. Requisitos
- [ ] PS Vita / PSTV con HENkaku o Enso (homebrew habilitado).
- [ ] `hotwheels-vita.vpk` del paquete (`dist/console-*/`), `TITLE_ID = HWMR00002`, versión 01.00.
- [ ] ~20 MB libres en `ux0:` para los datos del usuario (más el VPK).
- [ ] Copia legal de *Hot Wheels Micro Racers* (PC) disponible en la PC. **El paquete no trae el juego.**
- [ ] (Opcional) `libshacccg.suprx` en `ur0:/data/` si el backend usa vitaGL/GXM.
- [ ] (Opcional) `kubridge.skprx` v0.3+ en `ur0:tai/` — el runtime funciona sin él (VM de 16 MiB).

## 1. Instalar el VPK
1. [ ] Copiá `hotwheels-vita.vpk` a la Vita (USB/FTP/VitaShell).
2. [ ] En VitaShell: seleccionar el VPK → **Instalar**. Debe aparecer "HotWheels Vita" con icono/fondo.
3. [ ] Verificá que **no** haya errores `0x8...`. Si falla la instalación, parar y reportar el código.

## 2. Copiar los datos del usuario
Con la Vita montada en la PC (o por FTP), dejá el contenido de `Program_Files/` en `ux0:data/hotwheels/`:
```
ux0:data/hotwheels/
├── HotWheels.exe      (requerido)
├── HWICON.BMP
├── DATA/              (requerido)
├── DIAG/
└── EREG/              (opcional)
```
- [ ] Desde el repo: `tools/stage_data.sh <montaje>/ux0/data/hotwheels [--no-ereg]`.
- [ ] Desde el paquete extraído: `SRC=/path/to/Program_Files ./stage_data.sh /path/to/mount/ux0/data/hotwheels` (reemplazá ambas rutas; opcional `--no-ereg`; o copia manual, ver `INSTALL.md`).
- [ ] Confirmá en VitaShell que `ux0:data/hotwheels/HotWheels.exe` y `DATA/` existen.

## 3. Primer arranque — hitos observables
Arrancá desde el LiveArea. El runtime escribe su progreso en `ux0:data/hotwheels/`:
`boot.log`, `progress.txt` (y `hwvita.ini` si lo creaste). Sin pantalla, el log es la evidencia.

| # | hito | cómo se observa | OK |
|---|---|---|---|
| 1 | Proceso arranca, lee el PE | `boot.log`: base/entry/imports; `progress.txt` con el primer tick | [ ] |
| 2 | CRT + WinMain, ventana | log: shims KERNEL32/USER32; sin crash | [ ] |
| 3 | DirectDraw 8bpp + intro | la pantalla deja de ser negra (intro/menú) | [ ] |
| 4 | Menú navegable | input responde (ver §4) | [ ] |
| 5 | Audio | música/efectos audibles (ver §5) | [ ] |
| 6 | Carrera | se puede iniciar una carrera y jugarla | [ ] |

- [ ] Si el selector de arranque aparece (mantener **L** al bootear), probá cambiar `fps_mode`,
  `control_scheme`, `scaling` y `filter`; al confirmar debe reescribir `hwvita.ini`.

## 4. Input
- [ ] D-pad / sticks mueven el menú; Cross confirma; Circle vuelve.
- [ ] En carrera: acelerar/frenar/doblar según el esquema (`control_scheme` del ini).
- [ ] Si no responde: revisar en `boot.log` las trampas `DirectInputCreateA` / `GetDeviceState`.

## 5. Audio
- [ ] Efectos (choques, motor) y música (pista CD→WAV) audibles.
- [ ] Sin audio: revisar log de `hw_register_dsound`/`sceAudioOut` y que `MUSIC/Track*.wav` estén.

## 6. Recolectar evidencia (para el reporte)
- [ ] `ux0:data/hotwheels/boot.log` y `progress.txt` (FTP/VitaShell) → adjuntar.
- [ ] `ux0:data/hotwheels/hwvita.ini`.
- [ ] Screenshot del menú/ carrera (botón PS+Start) si hay imagen.
- [ ] Si crashea: `ux0:data/` dumps (psp2core) + última línea del log.
- [ ] Anotar `TITLE_ID`, versión, modelo de Vita y si fue VPK recién instalado.

## 7. Triage rápido
- **No instala**: código de error de VitaShell; TITLE_ID duplicado (desinstalar versión previa).
- **Arranca y cierra**: falta `HotWheels.exe` o `DATA/`; mirar `boot.log` (último import sin shim).
- **Pantalla negra**: present (HW-021) no integrado o el juego se quedó antes de `Flip`; el log dice
  la última trampa.
- **Sin audio**: `hw_register_dsound` no cableado o sin `libshacccg`; el log tiene el motivo.
- **Congelado**: ver `progress.txt`; si hay heartbeat, es lentitud, no cuelgue.

## 8. Qué NO concluir
- Un resultado en consola no valida performance de otros títulos ni de PC.
- Un fallo de audio/present en este VPK puede deberse a features aún en review (HW-021/HW-018).
- "Anda" = hito observado + log adjunto, no "debería andar".

## 9. Cerrar
- [ ] Anotar el resultado por hito en `coordination/` (dueño de la prueba) y abrir `bugs/BUG-NNN.md`
  con la evidencia si algo falla.
