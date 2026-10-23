# Inventario de winx86 y runtime de D2Vita

Alcance: lectura estática del submódulo `d2vita/D2Vita/third_party/winx86` y del consumidor `d2vita/D2Vita` (en particular `tools/rt_boot.cpp`, `tools/rt_boot_srcs.sh` y `src/runtime`). El cruce con los imports ordinarios y APIs dinámicas observadas está en [`gap-list.md`](gap-list.md); la lista PE fuente está en [`triage.md`](triage.md), con análisis adicional en [`re-engine.md`](re-engine.md).

## Cómo leer el inventario

El inventario exacto de los shims genéricos vive en [`winx86/docs/shims.md`](../../d2vita/D2Vita/third_party/winx86/docs/shims.md), generado por `third_party/winx86/tools/gen_shim_list.py` desde las llamadas de registro en `src/runtime/win32_shims_*.cpp`. La página fuente registra 366 claves únicas en 14 DLL y 15 unidades de instalación. El estado por fila de abajo describe qué clase de implementación hay; **que exista la DLL o el nombre no significa que toda la API de Windows esté implementada**.

`Bridge::register_shim(dll, name, shim)` guarda cada entrada individual. Registrar de nuevo la misma clave la reemplaza silenciosamente: gana el último registro. `rt_boot` combina unidades del engine y registros propios, por lo que la cobertura efectiva depende también del orden de instalación.

## Shims genéricos de winx86

En “Funciones” se incluyen familias representativas para ubicar el alcance. La lista completa, incluidos todos los nombres/ordinales, está en el inventario generado enlazado arriba y en el archivo fuente indicado.

| DLL / clave | Funciones y alcance comprobado en fuente | Estado efectivo en el engine |
|---|---|---|
| `KERNEL32.dll` | Heap/local/global, memoria virtual, sincronización/espera, locale y cadenas, consola, tiempo, errores, proceso/hilos, IOCP; aliases `K32EnumProcessModules`, `K32GetModuleInformation`. | Parcial; 137 claves genéricas en varias unidades. No es un `KERNEL32` completo. D2 agrega además 115 claves en su boot/runtime (ver abajo). |
| `USER32.dll` | Geometría `RECT`; ventanas, pintura y DC genéricos; cursor, métricas, strings y traducción básica de mensajes. | Parcial; stubs/semántica neutral sin sistema de ventanas real ni estado de teclado. D2 instala su ventana, pump y entrada propios. |
| `GDI32.dll` | Objetos fake, paletas, fuentes, DC, gamma, métricas, operaciones de dibujo básicas (`StretchDIBits`, `PatBlt`, `LineTo`). | Parcial y mayormente stub/fake handles. No es un renderer GDI; `BitBlt`/`StretchBlt` y DIB de D2 tienen ruta específica de presentación. |
| `ADVAPI32.dll` | ACL/SID, tokens, seguridad, SCM, servicios; `Reg*` y CryptoAPI no forman parte de esta cobertura genérica. | Parcial; varios retornan éxito neutral sin modelo de seguridad. D2 agrega registry persistente, CD-key store y CryptoAPI/servicios. |
| `DDRAW.dll` | `DirectDrawCreate`, `DirectDrawEnumerateA`. | Stub de DirectDraw no disponible; no dibuja. |
| `binkw32.dll` | Familia `_Bink*` usada para abrir/cerrar/reproducir/esperar/copiar video. | Stub “video no disponible”; no decodifica video. |
| `smackw32.dll` | Familia `_Smack*` de apertura, cierre, frame y blit. | Stub “video no disponible”; no decodifica video. |
| `ijl11.dll` | Ordinales `#2`, `#3`, `#5`. | Claves presentes para Intel JPEG; la página generada no promete un decodificador general. |
| `IMM32.dll` | 11 entradas `Imm*` de contexto, conversión y composición. | IME informado como ausente. |
| `PSAPI.DLL` | `GetModuleInformation`. | Implementado sobre la tabla de módulos de `Bridge`; D2 agrega rutas/nombres de módulo propios. |
| `SHELL32.dll` | `SHAppBarMessage`, `ShellExecuteA`. | Stubs sin efecto observable genérico. D2 agrega `SHGetFolderPathA` con su ruta de instalación. |
| `VERSION.dll` | `GetFileVersionInfoSizeW`, `GetFileVersionInfoW`, `VerQueryValueA/W`. | Lee recursos de versión PE (`VS_FIXEDFILEINFO`). |
| `WSOCK32.dll`, `WS2_32.dll` | Socket API, nombres y ordinales, resolución, byte order, connect/send/recv, etc. | Capa genérica traducida a POSIX; observer y redirect son puntos neutrales del port. En D2 el observer/ruta/guardia añaden política específica. |
| `WINTRUST.dll` / `CRYPT32.dll` | No aparecen como cobertura base genérica de `shims.md`. | D2 cablea verificación Authenticode real y funciones CryptoAPI desde `checkrevision_crypto.cpp`; no asumir cubiertas en un port nuevo. |
| `DSOUND.dll` / `DSOUNDVT.dll` | `DirectSoundCreate`, `DirectSoundCaptureCreate`, `DirectSoundEnumerateA` y alias por ordinal; métodos de dispositivo/buffer vía vtables COM/traps (`DSOUNDVT.dll`). | El engine trae emulación DirectSound/mixer (`src/runtime/ds_emul.cpp`), pero requiere que el host provea allocator VA, reloj, write root y `mix_rate`; D2 la instala con datos/política de D2. No es un shim auto-instalable. |
| `WINMM.dll` | D2 registra `timeGetTime`, `timeBeginPeriod`, `timeEndPeriod` en `src/runtime/kernel32_time.cpp`. | **No está en el inventario genérico generado**. El reloj está ligado al tick virtual de D2; no portar sin revisar el contrato temporal. |

La tabla de nombres exactos de las 14 DLL base se mantiene deliberadamente en la página generada: copiarla acá crearía una segunda lista que quedaría obsoleta. Para auditar cambios, usar `gen_shim_list.py --check` en el submódulo o comparar el registro fuente correspondiente.

## Extensiones y dependencias específicas de D2Vita

El runtime de D2 no es solo “winx86 más unas funciones”. `tools/rt_boot.cpp` construye `Cpu`/`Bridge`, mapea arenas y estado Win32, instala unidades genéricas, y enlaza política/estado del juego con funciones de `src/runtime`.

| DLL / área | Funciones extra o reemplazos visibles | Estado / por qué es específica |
|---|---|---|
| `KERNEL32.dll` | `GetVersion`, `GetCommandLineA`; archivos/directorios (`CreateFile*`, `ReadFile`, `FindFirst/NextFile*`, etc.); rutas y enumeración de módulos; clock/tick; TIB/TLS/hilos; critical sections; `.ini`; excepciones/salida; file mapping; fsinfo; Toolhelp; intrínsecos. | `rt_boot.cpp` comenta 252 claves totales: 137 genéricas y 115 aún servidas por D2. Las 115 duplican lógica del otro port, pero dependen de tablas de módulos/rutas, handles y roots de archivos, scheduler/TIB, tick virtual o intrínsecos. No clasificarlas todas como “Diablo-only”: buena parte es deuda de extracción, no diferencia de comportamiento del juego. |
| `USER32.dll` | `RegisterClassA/ExA`, `CreateWindowExA`, `ShowWindow`, `PeekMessageA`, `Get/DispatchMessageA`, `Send/PostMessageA`, `CallWindowProcA`, `Get/SetWindowLongA`, `GetKeyState`, `GetAsyncKeyState`, `GetKeyboardState`, `GetForegroundWindow`, `GetActiveWindow`, `MessageBoxA`; clipboard/monitor/draw-text/formatters desde `win32_import_remainder.cpp`. | Ventana fake con wndproc y cola de mensajes de D2; pump conectado al reloj e input scripted/real. `DispatchMessageA` y hooks de D2 consultan RVA/estado del binario. Los imports restantes son mix de compatibilidad general y policy D2. |
| `GDI32.dll` | `CreateDIBSection`, `SetDIBColorTable`, `BitBlt`, `StretchBlt`, `TextOutA`/otros puntos usados por el camino DIB. | Integra el DIB/frames de D2 con captura o presentación en Vita. Los exports base de GDI no implementan una API gráfica completa. |
| `ADVAPI32.dll` | `Reg*` de instalación, configuración y gateway; `CryptAcquireContextW`, `CryptCreateHash`, `CryptHashData`, `CryptGetHashParam`, etc.; servicios auxiliares. | Registry persistente con tratamiento separado para el material secreto de CD-key; CryptoAPI conectada a CheckRevision/huellas del cliente. Tiene semántica y datos de D2. |
| `SHELL32.dll` | `SHGetFolderPathA`. | Devuelve literal `C:\Diablo II`; inequívocamente específico y no reutilizable tal cual. |
| `WINMM.dll` | `timeGetTime`, `timeBeginPeriod`, `timeEndPeriod`. | `timeGetTime` avanza el tick virtual de D2 y se usa en loops de juego/red. |
| `DSOUND.dll` | `DirectSoundCreate`; vtables de dispositivo/buffer (métodos COM generados por `ds_emul.cpp`). | El mecanismo de audio está en winx86, pero `rt_boot` lo arma usando allocator/clock/write-root y frecuencia muestreo propios de D2. D2 reporta capacidades a su UI y añade decodificación Storm/Huffman/ADPCM por separado. |
| `d2vhost.dll` | `d2vGlideFlush`, `d2vGlideNop`, `d2vGlideTexUpload` y exports auxiliares de host. | Convención interna del port; no es DLL Win32 de sistema ni parte de winx86. Dispara el renderer GXM/ring y hooks de frame de Diablo. |
| `native.hook` | Hooks registrados con `Cpu::set_alternate`/traps para resoluciones, celdas, DCC/SCOMP, render, timing, etc. | Namespace convencional de hooks del port, no import de DLL. Muchos son offsets/semánticas de Game.exe 1.14d y deben descartarse para Hot Wheels. |
| `CRYPT32.dll`, `WINTRUST.dll`, `PSAPI.DLL`, `ole32.dll` | Crypto/Authenticode; `CoTaskMemFree`; rutas `GetModuleFileNameEx*`. | Implementación agregada en el port para dependencias observadas en la carga/CheckRevision. Algunas API tienen cuerpo utilitario, otras dependen del identity/path map de D2. |

### Qué es genérico y qué no

- Genérico reusable: loader PE32, `Cpu`/dynarec x86→ARMv7, `Bridge` de imports/traps, scheduler cooperativo/nativo, memoria guest, muchas shims neutrales y piezas de la capa DirectSound. Fuentes: `third_party/winx86/src/{runtime,dynarec86,platform}`.
- D2-only: launcher/arranque de `Game.exe` y DLL Blizzard, distribución de archivos MPQ, layout de guest calibrado para Diablo, `glide3x.dll`/GXM y `d2vhost.dll`, hooks por RVA de 1.14d, registro/ruta `C:\Diablo II`, protocolo/guardia BNCS y decodificadores Storm/DCC/Huffman/ADPCM.
- `tools/rt_boot.cpp` es el entry point consumidor (pese al nombre, está en `tools/`, no existe `src/rt_boot`). `src/runtime` mezcla módulos auxiliares de port, no todo ese directorio es automáticamente Diablo-specific. Los comentarios de extracción en cada unidad son la autoridad para la frontera.

## Cómo agregar una shim

1. Identificar el import por pareja exacta DLL + nombre/ordinal desde la triage; confirmar convención (`stdcall`/`cdecl`), número de argumentos y quién limpia la pila. Un nombre sin ABI correcto puede desalinear ESP.
2. Si la semántica no depende de juego, agregar implementación en la unidad adecuada `third_party/winx86/src/runtime/win32_shims_<grupo>.cpp` y declarar su `win32_shims_<grupo>_install(Bridge&)` en el header asociado. Registrar explícitamente `Shim` con `argc`, `stdcall_cleanup` y función `fn`; incluir esa unidad/header en `third_party/winx86/build.sh` y llamar al install desde el boot consumidor. Las instalaciones no se ejecutan automáticamente.
3. Si necesita estado, layout, política, renderer o datos de Hot Wheels, dejarla en el runtime del port. Las APIs genéricas con política variable deben recibir callbacks/ops neutrales; no meter literals del juego al engine.
4. Revisar duplicados con `tools/shim_seq.py` / `shim_seq.sh`: el último `register_shim` gana silenciosamente. Ordenar install calls de forma intencional. Añadir/actualizar `docs/shims.md` corriendo `tools/gen_shim_list.py`; `--check` detecta deriva.
5. Hacer el build host-side/target-side que corresponda y una prueba que ejercite ABI y semántica. A la fecha, este documento es inventario estático; no afirma ejecución en Vita3K para Hot Wheels.

## Cómo se arma el build Vita de referencia

El empaquetado que usa D2 está dividido en dos etapas:

```sh
TARGET=vita bash d2vita/D2Vita/third_party/winx86/build.sh
bash d2vita/D2Vita/tools/build_rt_boot_vpk.sh
```

La primera produce `third_party/winx86/build-vita/libwinx86_vita.a` con `arm-vita-eabi-gcc/g++`, ARMv7-A/NEON, `-D__vita__` y las adaptaciones Vita del backend. La segunda invoca esa compilación internamente, compila `tools/rt_boot.cpp`, módulos de `src/runtime`, renderer GXM/Glide, dependencias StormLib y crashreport, enlaza el archive más stubs VitaSDK y empaqueta `eboot.bin`/VPK. `tools/rt_boot_srcs.sh` enumera las unidades del consumidor; `tools/build_rt_boot_vpk.sh` es la receta efectiva del VPK. Para un build incremental se usa `tools/build_rt_boot_vpk_fast.sh` si está disponible en el checkout. `CMakeLists.txt` **no** arma el VPK: solo utilidades host-side y herramientas de inspección.

Para Hot Wheels, el segundo paso debe ser un boot consumidor nuevo; no alcanza con compilar la biblioteca genérica ni copiar `rt_boot` porque ese launcher enlaza estado y renderer de Diablo.

## Evidencia y límites

- `[estático]` Los nombres base se leen de `winx86/docs/shims.md`, generado desde registros fuente; los extras se corroboran en llamadas a `register_shim`/helpers y los `install` de `tools/rt_boot.cpp`, `tools/rt_boot_srcs.sh` y `src/runtime`.
- `[estático]` La página generada discrimina registros, no prueba que una función particular sea alcanzada por el binario ni que el retorno sea suficiente para otro juego.
- Límite: la gap list separa claves registradas de suficiencia semántica y de dependencias D2; no valida ejecución. Verificar prioridades contra traza Wine y la matriz COM de HW-006.
