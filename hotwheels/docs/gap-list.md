# HW-003 — Gap list de imports contra winx86

- Fecha: 2026-10-02.
- Base: imports ordinarios del `HotWheels.exe` original (94 símbolos, 6 DLL), tabla exacta en [`re-engine.md`](re-engine.md) § Anexo A; comparación estática contra [`winx86-inventory.md`](winx86-inventory.md), el inventario generado del submódulo y registros en `d2vita/D2Vita/src/runtime`.
- Evidencia de cobertura: **[estático]**. Prioridades también consideran la traza **[wine]** registrada en `FINDINGS.md` #12–16: el menú corre por DirectDraw software a 8 bpp; D3D es una sonda inicial y no el renderer del menú. “Presente” significa que existe un registro usable como punto de partida; no afirma ABI/comportamiento validado en Vita.

## Criterio de prioridad

- **M1 — arranque/primer frame:** sin esto no pasa el chequeo inicial, no crea ventana/presenta o no abre datos.
- **M2 — experiencia jugable:** audio, input y camino de render que puede postergarse mientras se prueba una escena mínima.
- **M3 — robustez/compatibilidad:** operaciones periféricas, errores y salidas que no bloquean el camino inicial observado.

La prioridad estima el camino descrito en `re-engine.md`; HW-002/Wine debe confirmar orden y reachability. “Stub insuficiente” separa una clave existente de una implementación que no cumple el contrato que el juego necesita.

## Imports ordinarios (94)

### WINMM.dll

| Función | Estado en la referencia | Prioridad | Nota de port |
|---|---|---:|---|
| `mmioAscend` | Falta | M2 | No está en winx86 ni en las extensiones D2. Necesario para parsear/cerrar chunks RIFF. |
| `timeGetTime` | Presente (D2) | M1 | D2 lo liga a su tick virtual/intrínseco; portar el contrato de reloj y hacerlo coherente con QPC. |
| `mciSendCommandA` | Falta | M1 | Se invoca para inicializar/reproducir música `cdaudio`; emular con pistas del port o responder en silencio para no bloquear el menú. |
| `mmioClose` | Falta | M2 | Parte de la lectura WAV/MMIO. |
| `mmioRead` | Falta | M2 | Lectura de bytes/chunks RIFF del WAV. |
| `mmioDescend` | Falta | M2 | Navegación RIFF/WAVE. |
| `mmioOpenA` | Falta | M2 | Apertura del WAV por rutas Win32; integrar con FS case-insensitive. |
| `mmioSeek` | Falta | M2 | Posicionamiento dentro del WAV. |
| `mmioSetInfo` | Falta | M2 | Contrato MMIO de buffer/estado; verificar uso real con traza. |
| `mmioGetInfo` | Falta | M2 | Contrato MMIO de buffer/estado; verificar uso real con traza. |
| `mmioAdvance` | Falta | M2 | Avance/refill MMIO; verificar si el juego lo usa en su ruta WAV estática. |

### DSOUND.dll

| Función/ordinal | Estado en la referencia | Prioridad | Nota de port |
|---|---|---:|---|
| `#1` (`DirectSoundCreate`) | Presente, condicionado | M2 | `ds_emul.cpp` registra ordinal 1 y emula la interfaz COM. El boot debe instalarlo con `HostOps` propios (VA, reloj, sink/frecuencia); la pieza estática sola no alcanza. |

### DDRAW.dll

| Función | Estado en la referencia | Prioridad | Nota de port |
|---|---|---:|---|
| `DirectDrawEnumerateExA` | Falta | M1 | No hay registro con este nombre. Hot Wheels lo llama durante `ddInit`; enumerar un backend software y tolerar la sonda D3D, sin exigir el renderer D3D6 para mostrar el menú. |
| `DirectDrawCreate` | Stub insuficiente | M1 | El genérico devuelve error de DirectDraw no disponible. M1 necesita el objeto/superficies DirectDraw4 para la ruta software 8 bpp (`Lock/Unlock`, `BltFast`, `Flip`, paleta, vblank) y una sonda D3D mínima; el renderer D3D6 completo queda después del menú. |

### DINPUT.dll

| Función | Estado en la referencia | Prioridad | Nota de port |
|---|---|---:|---|
| `DirectInputCreateA` | Falta | M2 | Falta el punto de entrada y toda la emulación COM de teclado/mouse/joystick. Puede subir a M1 si `inpInit` no tolera ausencia de input. |

### KERNEL32.dll

| Función | Estado en la referencia | Prioridad | Nota de port |
|---|---|---:|---|
| `MoveFileA` | Presente (D2) | M3 | D2 implementa rename sobre su root/handles; adaptar paths y semántica del save root. |
| `GetTimeZoneInformation` | Presente (D2) | M3 | Implementación en reloj D2; usar una zona consistente con hora local/sistema del port. |
| `GetSystemTime` | Presente (D2) | M3 | Implementación en reloj D2; no es genérica/autoinstalada. |
| `CompareStringW` | Presente (genérico) | M1 | Implementado en locale winx86. |
| `CompareStringA` | Presente (genérico) | M1 | Implementado en locale winx86. |
| `SetEnvironmentVariableA` | Stub insuficiente | M3 | Registro genérico retorna éxito sin mantener el entorno. |
| `SetEndOfFile` | Stub insuficiente | M3 | Registro genérico retorna éxito sin truncar el archivo. |
| `LoadLibraryA` | Presente (D2) | M1 | Resolución de módulos pseudo/sistema y del mapa D2; portar resolver/carga dinámica sin nombres/literales de Diablo. |
| `GetACP` | Presente (genérico) | M1 | Code page de locale del engine; confirmar que el valor elegido corresponde a paths/textos del juego. |
| `GetCPInfo` | Presente (genérico) | M1 | Implementación mínima (`MaxCharSize=1`); suficiente sólo para el code page ANSI elegido. |
| `GetOEMCP` | Presente (genérico) | M1 | Code page OEM del engine. |
| `OutputDebugStringA` | Falta | M3 | No hay registro en inventario genérico ni runtime D2 localizado. Puede ser logger/no-op para el MVP. |
| `QueryPerformanceFrequency` | Presente (genérico) | M1 | Frecuencia del engine. Debe ser coherente con `QueryPerformanceCounter`. |
| `QueryPerformanceCounter` | Presente (D2) | M1 | D2 registra el contador con reloj propio; portar reloj monotónico compatible con el cálculo de 40 Hz. |
| `FindClose` | Presente (D2) | M1 | Comparte handles de búsqueda D2; adaptar al VFS/FS del port. |
| `FindFirstFileA` | Presente (D2) | M1 | Adaptar búsqueda case-insensitive y raíz de datos; el juego la usa para localizar `hwicon.bmp`. |
| `DeleteFileA` | Presente (D2) | M3 | Implementación ligada al filesystem D2; mapear al save/data root. |
| `GetDriveTypeA` | Stub insuficiente | M1 | El genérico devuelve siempre `DRIVE_FIXED (3)`; el juego exige `DRIVE_CDROM (5)` para la unidad que contiene `hwicon.bmp`. |
| `GetLogicalDriveStringsA` | Stub insuficiente (D2) | M1 | D2 enumera sólo `C:\\`; aun combinado con su `GetDriveTypeA` fijo no satisface el chequeo CD. Exponer unidad sintética consistente con `FindFirstFileA` y MCI. |
| `GetProcAddress` | Presente (D2) | M1 | Resolver imports/exports dinámicos. El juego lo usa para Toolhelp; requiere que el mapa de exports incluya las funciones indicadas más abajo. |
| `GetModuleHandleA` | Presente (D2) | M1 | Adaptar al registro de módulos del port. |
| `ReadFile` | Presente (D2) | M1 | Reusar sólo tras separar VFS/handles D2; Hot Wheels usa CRT para assets y también importa la API. |
| `GetVersion` | Presente (D2) | M3 | Valor fijo Windows XP de D2; elegir versión compatible, aunque no parece gate principal. |
| `CloseHandle` | Presente (D2) | M1 | En D2 cierra sus handles de archivos; integrar con handle table del port. |
| `IsBadReadPtr` | Presente (genérico) | M3 | Stub permisivo que informa “válido”; no valida memoria. |
| `SetUnhandledExceptionFilter` | Presente (D2) | M3 | Registra el filtro guest en estado compartido con el dispatcher; portar o usar salida controlada. |
| `FlushFileBuffers` | Presente (D2) | M3 | Código D2 conoce sus handles; adaptar o exponer flush coherente para saves. |
| `SetStdHandle` | Stub insuficiente | M3 | Genérico retorna éxito sin actualizar el handle estándar. |
| `CreateFileA` | Presente (D2) | M1 | Requiere VFS con traducción `\\`/case-insensitive y roots de datos/escritura del port. |
| `GetEnvironmentStringsW` | Presente (genérico) | M3 | Devuelve bloque vacío, correcto sólo si el juego no depende de variables del host. |
| `GetEnvironmentStrings` | Presente (genérico) | M3 | Devuelve bloque ANSI vacío. |
| `FreeEnvironmentStringsW` | Presente (genérico) | M3 | No-op; compatible con el bloque vacío estático actual, revisar si se asigna dinámicamente. |
| `FreeEnvironmentStringsA` | Presente (genérico) | M3 | No-op; compatible con el bloque vacío estático actual, revisar si se asigna dinámicamente. |
| `GetModuleFileNameA` | Presente (D2) | M3 | Resolver rutas del módulo con el mapa de módulos del port, no la ruta `C:\\Diablo II`. |
| `UnhandledExceptionFilter` | Stub insuficiente | M3 | Genérico siempre continúa búsqueda; requiere ruta de fallo/SEH coherente si se alcanza. |
| `LCMapStringW` | Presente (genérico) | M1 | Locale winx86. |
| `LCMapStringA` | Presente (genérico) | M1 | Locale winx86. |
| `ExitProcess` | Presente (D2) | M3 | La salida D2 desencadena teardown de port; sustituir por shutdown de Hot Wheels. |
| `TerminateProcess` | Presente (D2) | M3 | Igual: acción de shutdown específica del host. |
| `GetCurrentProcess` | Presente (genérico) | M3 | Pseudo-handle genérico. |
| `GetLastError` | Presente (genérico) | M3 | Usa el estado de error/TIB guest del engine. |
| `GetStringTypeW` | Presente (genérico) | M1 | Locale winx86. |
| `HeapAlloc` | Presente (genérico) | M1 | Requiere configurar arena/heap del guest para el port. |
| `HeapFree` | Presente (genérico) | M1 | Requiere el mismo allocator guest de `HeapAlloc`. |
| `RtlUnwind` | Presente (genérico) | M3 | Stub neutral; comprobar si el CRT realmente necesita desenrollado estructurado. |
| `GetStringTypeA` | Presente (genérico) | M1 | Locale winx86. |
| `IsBadCodePtr` | Presente (genérico) | M3 | Stub permisivo, no valida punteros ejecutables. |
| `GetLocalTime` | Presente (D2) | M3 | Implementación con reloj/calendario D2; portar zona/fecha coherentes. |
| `GetStartupInfoA` | Presente (genérico) | M3 | Llena estructura mínima con tamaño y campos restantes a cero. |
| `GetCommandLineA` | Presente (D2) | M3 | D2 genera `game.exe`/argumentos desde su entorno; crear cadena propia del launcher. |
| `WideCharToMultiByte` | Presente (genérico) | M1 | Conversión implementada por locale winx86. |
| `MultiByteToWideChar` | Presente (genérico) | M1 | Conversión implementada por locale winx86. |
| `HeapReAlloc` | Presente (genérico) | M1 | El allocator genérico asigna bloque nuevo; validar copia/semántica de tamaño para CRT del juego. |
| `WriteFile` | Presente (D2) | M3 | Adaptar handles y save root; no hay evidencia de que sea necesario para cargar datos. |
| `SetHandleCount` | Presente (genérico) | M3 | Retorna el valor solicitado; compatibilidad mínima. |
| `GetStdHandle` | Presente (genérico) | M3 | Devuelve pseudo-handles de consola; consola Vita no equivale a Win32 console. |
| `GetFileType` | Stub insuficiente | M3 | Genérico devuelve `FILE_TYPE_CHAR` incluso para handles de archivo. |
| `SetFilePointer` | Presente (D2) | M1 | Adaptar a handles del VFS; CRT `fseek` usa sus propios streams, pero el import debe resolver. |
| `HeapSize` | Presente (genérico) | M1 | Tamaño consultado sobre el allocator configurado. |
| `HeapDestroy` | Stub insuficiente | M3 | Genérico retorna éxito sin destruir/liberar el heap. |
| `HeapCreate` | Presente (genérico) | M1 | Crea handle guest y usa el heap de arena configurado. |
| `VirtualFree` | Presente (genérico) | M1 | Requiere mapear reserva/liberación a la arena guest del port. |
| `VirtualAlloc` | Presente (genérico) | M1 | Requiere plan de arena guest con límites de Vita; el engine implementa reservar/commit. |
| `IsBadWritePtr` | Presente (genérico) | M3 | Stub permisivo, no valida memoria. |

### USER32.dll

| Función | Estado en la referencia | Prioridad | Nota de port |
|---|---|---:|---|
| `TranslateMessage` | Presente, no-op (genérico) | M2 | No sintetiza `WM_CHAR`; puede afectar teclado/cheats, verificar si el juego depende de texto de ventana. |
| `DefWindowProcA` | Presente, no-op (genérico) | M3 | Retorna cero; no implementa comportamiento default Win32. |
| `PostQuitMessage` | Stub insuficiente (genérico) | M3 | No encola `WM_QUIT`; afecta cierre normal, no el primer frame. |
| `MessageBoxA` | Presente (D2) | M1 | D2 lo vuelca a stdout/log; adaptar a UI/input del port si los errores de CD deben mostrarse al usuario. |
| `LoadIconA` | Presente (genérico) | M1 | Handle falso para recursos stock; la ventana del port debe tolerarlo. |
| `LoadCursorA` | Presente (genérico) | M1 | Handle falso para recursos stock. |
| `RegisterClassA` | Presente (D2) | M1 | Implementación guarda WndProc en tabla de ventanas del boot D2; extraer sin dependencias del ejecutable Diablo. |
| `GetSystemMetrics` | Presente (genérico) | M1 | Consulta dimensiones configuradas por el backend; revisar ajuste Vita/aspect ratio. |
| `CreateWindowExA` | Presente (D2) | M1 | Crea HWND guest sintético; extraer lifecycle y conectar eventos Vita. |
| `ShowWindow` | Presente (D2) | M1 | Activa ventana/pump en el host D2; reimplementar como estado visible del port. |
| `UpdateWindow` | Presente, stub exitoso (genérico) | M1 | No genera pintura; suficiente sólo si la presentación la gobierna el renderer. |
| `DispatchMessageA` | Presente (D2) | M1 | Llama WndProc almacenado; desacoplar callbacks/hooks que consultan Diablo. |
| `WaitMessage` | Falta | M2 | Se usa si la app queda inactiva; implementar espera/evento no bloqueante para Vita. |
| `PeekMessageA` | Presente (D2) | M1 | Pump y generación de input D2; portar cola/eventos y semántica `PM_REMOVE`. |
| `ShowCursor` | Presente (genérico) | M2 | Contador lógico; la política de cursor visible debe conectarse a input/overlay de Vita. |

## APIs dinámicas que RE encontró fuera de los 94 imports

`re-engine.md` §1 muestra que `isAlreadyRunning` resuelve Toolhelp con `GetProcAddress`; el juego sale con “Windows NT is not supported.” si esas funciones no están disponibles. La traza [wine] #16 confirma el loop de mensajes, input y archivos, y #14 confirma el chequeo CD de tipo de unidad; estas APIs Toolhelp no aparecen en la tabla PE de imports ordinarios.

| Función dinámica | Estado en D2 | Prioridad | Nota de port |
|---|---|---:|---|
| `CreateToolhelp32Snapshot` | Presente (D2) | M1 | `toolhelp.cpp`; adaptar snapshot de procesos a proceso único del port. |
| `Process32First` | Presente (D2) | M1 | `toolhelp.cpp`; devolver estructura guest-compatible con nombre Hot Wheels. |
| `Process32Next` | Presente (D2) | M1 | `toolhelp.cpp`; semántica de fin de enumeración debe coincidir con el chequeo `>1`. |

## Resumen por prioridad

- **M1:** resolver el chequeo CD como conjunto coherente (`GetLogicalDriveStringsA`, `GetDriveTypeA`, `FindFirstFileA`/`FindClose`, VFS y `hwicon.bmp`); Toolhelp dinámico; ventana/mensajes; reloj; DDRAW software 8 bpp y presentación del menú; heap/archivos. `FINDINGS.md` #13 observó una sonda D3D inicial, así que también hace falta responder a esa creación de dispositivo aunque no se requiere un pipeline 3D para M1.
- **M2:** DInput; DirectSound con `HostOps`; WINMM MMIO y MCI. MCI se marca M1 porque se ejecuta al inicializar `cdaudio`, aunque puede degradarse a stub de silencio para alcanzar una primera escena.
- **M3:** APIs de logging, limpieza, locale raro y errores. Mantenerlas registradas para evitar imports sin resolver, aunque sus stubs neutrales no deben confundirse con semántica completa.

## Límites y siguiente validación

Esta tabla no cubre los métodos COM obtenidos por `QueryInterface`/vtable (DDraw4, Surface4, D3D3/Device3/Viewport3, DInput y DirectSound). Son una parte sustancial del trabajo de HW-006 y requieren su propia matriz de métodos. Tampoco demuestra qué imports alcanza cada build del juego. Confirmar el orden y los argumentos con Wine antes de cerrar prioridad M1/M2; validación Vita queda después.
