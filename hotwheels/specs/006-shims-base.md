# SPEC-006 — Shims base M1: arranque de HotWheels.exe (HW-006)

- Feature: HW-006 · Milestone: M1/M2 · Dueño: `re` · Estado: en curso
- Depende de: HW-005 (esqueleto, `hw_boot`). Coordina con HW-009 (impl, registro de shims en el boot), HW-013 (sol, DDraw), HW-015 (deepseek, WINMM/MCI).
- Fuentes: `docs/gap-list.md` (luna), `docs/re-engine.md` §1–2 (RE), `FINDINGS.md` #12–16 (Wine), `docs/wine-reference.md` §CD.

## Problema
Con los shims genéricos de winx86, `HotWheels.exe` no pasa de `WinMain`:
1. **Instancia única** (`isAlreadyRunning` 0x450970): resuelve `CreateToolhelp32Snapshot/Process32First/Process32Next` con `GetModuleHandleA("kernel32.dll")` + `GetProcAddress`. Si falta alguna → MessageBox "Windows NT is not supported." y sale. Si cuenta >1 proceso `hotwheels.exe` → sale.
2. **Chequeo de CD**: recorre `GetLogicalDriveStringsA`, exige `GetDriveTypeA == DRIVE_CDROM (5)` y `FindFirstFileA("<X>:\hwicon.bmp")`. El genérico devuelve siempre `DRIVE_FIXED`.
3. **Archivos**: el CRT (MSVC estático) abre `data\...` con rutas `\` y mayúsculas mezcladas (en disco `DATA/MENU/...`) por `CreateFileA/ReadFile/SetFilePointer/CloseHandle`, y llama `GetFileType`: el genérico devuelve `FILE_TYPE_CHAR`, con lo que el CRT marca el fd como dispositivo y `fseek/ftell` fallan (rompe `fileLoadWhole`).
4. **Reloj**: `timerWaitFrame40Hz` (0x412df0) hace busy-wait sobre `QueryPerformanceCounter` hasta acumular 25 ms; `timeGetTime` se usa para autorepeat. Ambos deben salir de la misma fuente monotónica.
5. **Ventana/mensajes**: `RegisterClassA/CreateWindowExA/ShowWindow/PeekMessageA/DispatchMessageA/WaitMessage` no existen en el genérico (sólo en D2Vita). El loop sólo corre el juego si `appActive`, que lo pone `WM_ACTIVATE` en el WndProc.
6. Faltan además (gap-list M1/M3 que el CRT toca en el arranque): `GetModuleHandleA, GetProcAddress, LoadLibraryA, GetModuleFileNameA, GetCommandLineA, GetVersion, OutputDebugStringA, SetUnhandledExceptionFilter, ExitProcess, TerminateProcess, GetCurrentProcess, GetLocalTime, GetSystemTime, GetTimeZoneInformation, FindFirstFileA, FindClose, DeleteFileA, MoveFileA, FlushFileBuffers, WriteFile, MessageBoxA`.

## Decisión de diseño
- **Emulación del entorno, no parche del binario.** El exe corre intacto; los shims responden como un Windows con una lectora de CD que tiene el disco puesto.
- **Núcleo portable** (`hotwheels/src/shims/*.cpp`, C++17, sin winx86 salvo los tipos `d2rt::Cpu`/`d2rt::Shim`): `Vfs`, `CdDrive`, `GuestClock`, `MsgQueue`, tabla de handles. Testeable en host sin x86.
- **Adaptador** `hw::shims::build(Registrar, Context&)` que produce los `Shim` por (dll, nombre); `install(Bridge&, Context&)` los registra con `register_shim`. Los servicios del Bridge que se necesitan (`shim_trap` para `GetProcAddress`, `call_va` para `DispatchMessageA`) y del host (log, salida, espera, hora) se **inyectan** en `Context` → los tests capturan los shims y los invocan con una CPU falsa.
- Orden de instalación (para impl/HW-009): `win32_shims_*_install` genéricos → **`hw::shims::install`** (pisa `GetDriveTypeA`, `GetFileType`, `QueryPerformanceFrequency`, `PostQuitMessage`) → `hw::ddraw::install` (sol) → `br.link()`. El último registro gana.
- `DirectDrawEnumerateExA` **ya lo registra HW-013** (`src/ddraw/bridge.cpp`, un driver "HW DirectDraw software" con GUID NULL). HW-006 no lo duplica; el criterio A8 sólo verifica que exista tras instalar ambos.
- `CreateFile("\\.\D:")` + `DeviceIoControl(0x24000/0x24008)` y `GetVolumeInformationA` **no son imports del juego** (los emitió `mcicda` de Wine, FINDINGS #14). Se implementan igual en `CdDrive` (emulación de hardware) para que el MCI de HW-015 pueda pedir la TOC, y se registran en KERNEL32 por si algo los resuelve dinámicamente.

## Contrato observable

### Vfs (FS case-insensitive)
- Unidades: `C:` → raíz del juego (`base`, sólo lectura) + `save` opcional (escritura). `D:` → raíz del CD virtual (donde está `hwicon.bmp`). Directorio actual por defecto `C:\` (el juego abre rutas relativas `data\...`).
- `resolve_read(win)`: separadores `\` y `/`, `.`/`..`, letra opcional, relativo al cwd; cada componente se busca **sin distinguir mayúsculas**; busca primero en `save`, después en `base`. Devuelve ruta host o vacío.
- `resolve_write(win)`: siempre en `save` (crea directorios intermedios); si no hay `save`, en `base`.
- `find(pattern)`: comodines `*` y `?` en el último componente, case-insensitive, sin `.`/`..`; une `save` y `base` sin duplicados.

### CdDrive
- `letter='D'`, `label="HWMR"`, `fs="CDFS"`, serie fija `0x1F2E3D4C`, `max_component=110`, flags `FILE_CASE_PRESERVED_NAMES|FILE_UNICODE_ON_DISK|FILE_READ_ONLY_VOLUME (0x80006)`.
- Pistas: 1 de datos + N de audio (N = cantidad de `Track N.wav`, o el número configurado). `read_toc` → `CDROM_TOC` (Length BE = 2 + 8·(n+1), First=1, Last=n, `TRACK_DATA` 8 B con `Control` 4 (datos) / 0 (audio), `Adr`=1, dirección MSF con 2 s de pregap; última entrada lead-out 0xAA).
- `ioctl(0x24000 IOCTL_CDROM_READ_TOC)` → TOC, `ioctl(0x24008 IOCTL_CDROM_STOP_AUDIO)` → éxito sin datos; otro código → `ERROR_INVALID_FUNCTION (1)`.

### KERNEL32
| API | Contrato |
|---|---|
| `GetLogicalDriveStringsA(n, buf)` | `"C:\\0D:\\0\0"` (9 B, retorna 8); si `n` < 9 retorna 9 sin escribir |
| `GetDriveTypeA(root)` | `C:\`/`C:` → 3; letra del CD → **5**; NULL → la del cwd (3); otra → `DRIVE_NO_ROOT_DIR (1)` |
| `GetVolumeInformationA` | datos de `CdDrive` para `D:\`; `C:\` → label "HOTWHEELS", fs "NTFS"; otra → 0 + error 3 |
| `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS=2,0)` | handle; otros flags también devuelven handle con lista de 1 proceso |
| `Process32First(h, pe)` | exige `pe->dwSize >= 0x128`; escribe `th32ProcessID` (pid fijo), `cntThreads=1`, `szExeFile="HotWheels.exe"` (+36); retorna 1 |
| `Process32Next` | 0 + `ERROR_NO_MORE_FILES (18)` → el juego cuenta exactamente 1 |
| `GetModuleHandleA` | NULL → base del exe; `kernel32.dll`/`user32.dll`/`winmm.dll`/`ddraw.dll`/`dsound.dll`/`dinput.dll`/`gdi32.dll`/`advapi32.dll` (case-insensitive, con o sin `.dll`) → pseudo-handle distinto por DLL; otro → 0 + error 126 |
| `LoadLibraryA` | igual que `GetModuleHandleA` (no carga DLLs reales) |
| `GetProcAddress(h, name)` | pseudo-handle → `shim_trap(dll, name)`; 0 o nombre por ordinal → 0 + error 127 |
| `CreateFileA` | `\\.\X:` de la letra del CD → handle de dispositivo; ruta normal → `Vfs`; disposiciones 1–5 con semántica Win32; falla → `INVALID_HANDLE_VALUE` + error 2/3/80 |
| `ReadFile/WriteFile/SetFilePointer/CloseHandle/FlushFileBuffers` | sobre la tabla propia; `SetFilePointer` con `FILE_BEGIN/CURRENT/END`, alto ignorado si es 0 |
| `GetStdHandle(-10/-11/-12)` | pseudo-handles fijos `0x00F00001..3` (pisa el genérico para que `GetFileType` los reconozca) |
| `GetFileType` | handle de archivo/dispositivo propio → `FILE_TYPE_DISK (1)`; `GetStdHandle(-10/-11/-12)` → `FILE_TYPE_CHAR (2)`; otro → 0 |
| `FindFirstFileA/FindNextFileA/FindClose` | `WIN32_FIND_DATAA` (attrs, tamaño, `cFileName` @44); sin match → `INVALID_HANDLE_VALUE` + error 2 |
| `DeleteFileA`, `MoveFileA` | sobre `save` (o `base` si no hay `save`) |
| `DeviceIoControl(h, code, ...)` | handle de dispositivo CD → `CdDrive::ioctl`, escribe `*lpBytesReturned` |
| `QueryPerformanceFrequency` | 1.000.000 |
| `QueryPerformanceCounter` | µs de `GuestClock`, **siempre ≠ 0**, monotónico, retorna 1 |
| `GetModuleFileNameA` | `exe_path` (por defecto `C:\HotWheels.exe`), semántica de truncado Win32 |
| `GetCommandLineA` | `"C:\HotWheels.exe"` entre comillas, puntero estable |
| `GetVersion` | `0x0A280105` (XP, igual que D2Vita) |
| `OutputDebugStringA`, `MessageBoxA` | al log del host; `MessageBoxA` retorna `IDOK (1)` |
| `SetUnhandledExceptionFilter` | guarda el filtro, retorna el anterior |
| `ExitProcess/TerminateProcess` | hook del host `exit(code)` |
| `GetCurrentProcess` | `0xFFFFFFFF` |
| `GetLocalTime/GetSystemTime` | `SYSTEMTIME` desde el reloj de pared del host (UTC = local, sin zona) |
| `GetTimeZoneInformation` | estructura en cero (172 B), retorna `TIME_ZONE_ID_UNKNOWN (0)` |

### WINMM
- `timeGetTime` = `GuestClock` en ms (u32, misma fuente que QPC).

### Reloj
- `GuestClock` toma µs de una fuente inyectada (host: `wx86_now_us`). QPC = µs − origen + 1 (nunca 0). `timeGetTime` = µs/1000 (desde el mismo origen).
- Coherencia: para todo par de lecturas, `|Δqpc/1000 − Δtgt| ≤ 1 ms`.
- Opcional (`spin_idle_us > 0`): si dos QPC consecutivas están a menos de `spin_idle_us`, llama `idle()` del host antes de leer (alivia el busy-wait en Vita). Por defecto apagado.

### USER32
| API | Contrato |
|---|---|
| `RegisterClassA(wc)` | guarda `lpszClassName` (case-insensitive) → `lpfnWndProc`; retorna átomo ≠ 0; clase repetida → 0 + error 1410 |
| `CreateWindowExA` | clase registrada → HWND ≠ 0 (no envía `WM_CREATE`); si no → 0 + error 1407 |
| `ShowWindow(hwnd, cmd)` | primera vez con `cmd ≠ SW_HIDE` encola `WM_ACTIVATE(WA_ACTIVE=1, 0)`; retorna visibilidad previa (0/1) |
| `PeekMessageA(msg, hwnd, min, max, remove)` | FIFO; con `PM_REMOVE (1)` saca; escribe `MSG` (28 B); 0 si vacía |
| `DispatchMessageA(msg)` | llama al WndProc del HWND por `call_guest(wndproc, {hwnd, msg, wp, lp})`; retorna su resultado; HWND desconocido → 0 |
| `WaitMessage` | si la cola está vacía llama `wait()` del host; retorna 1 |
| `PostQuitMessage(code)` | encola `WM_QUIT (0x12)` con `wParam=code` |
El host puede encolar mensajes (`MsgQueue::post`) para foco/entrada.

## Criterios de aceptación (tests host, `tests/test_shims.cpp`)
- **A1 Toolhelp**: `GetModuleHandleA("KERNEL32.DLL")` ≠ 0; `GetProcAddress` de las 3 funciones ≠ 0; snapshot + First (dwSize 0x128) escribe `"HotWheels.exe"` y retorna 1; Next retorna 0 y error 18. El conteo `strcmpi(name,"hotwheels.exe")` del juego da 1.
- **A2 CD**: `GetLogicalDriveStringsA` da `C:\ D:\`; tamaño chico retorna 9; `GetDriveTypeA("D:\\")==5`, `"C:\\"==3`, `"Q:\\"==1`; `FindFirstFileA("D:\\hwicon.bmp")` encuentra el archivo aunque en disco sea `HWICON.BMP`; `GetVolumeInformationA("D:\\")` da "HWMR"/"CDFS".
- **A3 IOCTL**: `CreateFileA("\\\\.\\D:")` ≠ `INVALID_HANDLE_VALUE`; `DeviceIoControl(0x24000)` devuelve TOC con First=1, Last=1+N, primera pista de datos (Control 4) en 00:02:00, lead-out 0xAA; `0x24008` éxito; `0x2404C` → 0 + error 1.
- **A4 Vfs**: `data\menu\settings.dat` abre `DATA/MENU/SETTINGS.DAT`; `C:\data\..\DATA\Menu\x.TXT` resuelve; escritura de `data\menu\settings.dat` va a `save/` y la lectura posterior la prefiere; `FindFirstFileA("data\\menu\\*.TGA")` lista sin duplicados; `GetFileType` de un handle de archivo = 1 y de `GetStdHandle(-11)` = 2.
- **A5 Archivos**: `CreateFileA` OPEN_EXISTING + `SetFilePointer(0,FILE_END)` = tamaño; `SetFilePointer(0,FILE_BEGIN)` + `ReadFile` lee los bytes exactos; inexistente → `INVALID_HANDLE_VALUE` y error 2; CREATE_NEW sobre existente → error 80.
- **A6 Reloj**: con fuente falsa, QPF=1e6; QPC nunca 0 y monotónico; `timeGetTime` coherente (≤1 ms); simular `timerWaitFrame40Hz` (`dt = Δqpc·163840/QPF` hasta ≥ 0x1000) avanzando la fuente 100 µs por lectura termina con Δ ∈ [25, 25.1] ms; con `spin_idle_us` se llama `idle`.
- **A7 Ventana**: Register + Create + `ShowWindow(SW_SHOWNORMAL)` → `PeekMessageA(PM_REMOVE)` devuelve `WM_ACTIVATE` con `wParam=1`; `DispatchMessageA` invoca el WndProc registrado con (hwnd, 6, 1, 0); cola vacía → `PeekMessageA` 0 y `WaitMessage` llama `wait()`; `PostQuitMessage(0)` → `WM_QUIT`.
- **A8 Cobertura**: tras `build()` están registrados todos los imports KERNEL32/USER32 de la lista "faltan" de este documento (§Problema 6 + 1–5) y `WINMM!timeGetTime`; ningún nombre se registra dos veces.

## No-objetivos
- DirectDraw/D3D (HW-013/HW-010), DSound, DInput (HW futura), MCI/mmio (HW-015).
- Ejecutar x86 o el exe completo (eso es HW-007 en Vita3K/host con dynarec).
- Semántica completa de ventanas Win32 (mensajes sincrónicos de creación, múltiples ventanas, `GetMessage`).
- Zonas horarias reales.
