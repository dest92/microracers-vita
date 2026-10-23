# Boot log del HotWheels.exe real (HW-007, integrador)
Cómo se corre: `HW_ENV="HW_SCHED=coop TRAPTAG=1" hotwheels/tools/run_host.sh 8` (salida en hotwheels/out/latest).
| # | trampa / hito | estado | resolución |
|---|---|---|---|
| 1–43 | CRT: GetVersion, HeapCreate, GetStdHandle, env, locale, GetModuleFileName, GetProcAddress(IsProcessorFeaturePresent) | OK | winx86 genérico + HW-006 |
| 44 | CreateFileA `data\menu\highscore.dat` -> ERROR_FILE_NOT_FOUND | OK (el archivo no viene en la instalación; el juego lo crea al salir) | — |
| — | scheduler NATIVO se colgaba acá | RESUELTO | BUG-001: liberar GIL alrededor del callback guest anidado |
| 46–60 | DirectDrawEnumerateExA, lang.txt, Toolhelp32 (Process32*), GetDriveTypeA D:=5 (chequeo de CD pasa) | OK | HW-006/HW-013 |
| 61–69 | LoadIcon/Cursor, RegisterClassA, CreateWindowExA, ShowWindow | OK | winx86/HW-006 |
| 70 | CreateFileA `data\menu\settings.dat` -> no existe | OK (lo crea el juego al salir) | — |
| 72 | DSOUND #1 DirectSoundCreate | STUB -> DSERR_NODRIVER | integrate.cpp (falta el shim real de audio: ds_emul de winx86 / HW-018) |
| ~95 | sinus.sin abierto y leído (3 veces) | OK | HW-006 |
| ~100 | **DINPUT DirectInputCreateA** | RESUELTO | dinput_bridge: objeto y vtables guest válidos |

## HW-007, 2026-10-03 — corrida qemu-arm

- `DirectInputCreateA` y vtables guest pasan; `SetDataFormat`, `Acquire` y `GetDeviceState` se alcanzaron con el ejecutable real (`TRAPTAG=1`, `out/run-20261003-000522/stdout.txt`).
- `pushfw` de 16 bits hacía acceso a ESP guest sin fastmmu en el JIT ARM; parche reproducible `patches/winx86-0002-pushfw-fastmmu.patch`. Tras aplicarlo, el guest pasó de la sonda DDraw al render 8 bpp.
- DDraw4 leía `ddsCaps` a offset 116; el ABI i386 define 104. La sonda 16 bpp dejaba la primaria ocupada al recrear DirectDraw para el menú. Ambas correcciones están en `ddraw/bridge.cpp`.
- `HW_SCHED=coop` produjo 85 PNG en 12 s de límite (`out/run-20261003-000745`, medido por run_host.sh); `frame_00084.png` muestra la intro. Fixer reporta menú con scheduler nativo en `out/latest/frame_00027.png`, ver `coordination/fixer.md`.
- Los faltantes originales `WINMM!mciSendCommandA` y `DSOUND!#1` se cablearon con fixer; falta validar audio audible. BUG-001 resuelto y aprobado reviewer (coordination/review-HW-007-vita.md y review de 450d4d8).

## Vita3K, 2026-10-03 — primer VPK integrado

- Build `b1ba07a`: VPK 7.833.314 bytes (`stat`), tests present/DDraw verdes.
- `STAGE=hotwheels/build-vita/stage tools/vita3k_run.sh build-vita/hotwheels-vita.vpk 35`: instalado y ejecutado; alcanza carga `DATA/LANG.TXT` y búsqueda de settings.
- Evidencia `build-vita3k/hotwheels-vita-20261003-001932/run.stdout`: SIGSEGV del emulador, acceso 0x0, antes de present. `shot_020.png` negra inspeccionada. boot.log conserva contenido viejo, no sirve como evidencia de este run; se agrega sceClibPrintf para diagnosticar. M3 Vita sigue abierto.
- Segundo intento `002429` con sceClibPrintf: 94 imports, 0 sin shim; ReadFile(LANG.TXT) devuelve éxito. Crash NULL después de settings ausente, todavía antes de present. BUG-008 conserva evidencia. M3 no aprobado.

## Consola autorizada: deploy, ejecución pendiente

`vita_deploy.sh`: primera copia 113 archivos (todos SIZE igual al local),
updates 7 subidos/106 omitidos. Companion dice Launched, pero no apareció
hotwheels.log: ejecución **no confirmada**. Después de los intentos quedó
`ux0:data/hotwheels/hotwheels-vita.vpk` (7.836.964 bytes verificados) para instalar
y abrir HWMR00002 en VitaShell a la mañana. No se tocaron plugins/apps ajenos.
No hay todavía comparación gráfica consola/Vita3K.

## M3 gráfico Vita3K observado (sin audio, diagnóstico)

`HW_AUDIO=0` en env.txt, backend HW-021 con pool circular2MiB y legacy0:
`build-vita3k/hotwheels-vita-20261003-003318/shot_020.png` muestra menú real de
selección de jugadores (inspección visual). Log persiste por open/write/close y
registra >1400 frames640x480. Harness45s. Audio default ON sigue BUG-008;
no se afirma audio Vita funcional. Evidencia visual ignorada por Git (arte juego).
