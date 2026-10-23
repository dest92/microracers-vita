# Spec 001 — Esqueleto `hotwheels-vita` (HW-005)

Owner: impl. Estado: en curso. Depende de: HW-003 (inventario de shims; el esqueleto no lo necesita para cargar, sí para HW-006).

## Problema
Necesitamos el mínimo ejecutable que, igual que D2Vita (`tools/rt_boot.cpp`), use el motor
genérico `winx86` para cargar el PE de Hot Wheels y dejar la CPU invitada parada en su entry point,
con todas las importaciones cableadas a trampas del `Bridge`. Todo lo siguiente (shims, render,
boot a menú) se apoya en esto.

## Cómo lo hace D2Vita (estudiado, nivel estático)
- `winx86` es submódulo (`D2Vita/third_party/winx86`). Dos builds:
  - host: `CMakeLists.txt` de winx86 → `winx86_core` (PeImage + Bridge + shims genéricos, **sin CPU**),
    `pe_analyze`. Compila en x86-64 Linux (verificado: `cmake --build` OK).
  - Vita: `TARGET=vita third_party/winx86/build.sh` → `build-vita/libwinx86_vita.a` (dynarec Box86 + runtime);
    el consumidor compila sus TUs con `arm-vita-eabi-g++ -D__vita__ -DD2RT_CPU_BOX86` y enlaza el `.a`
    + stubs SCE, luego `vita-elf-create` / `vita-mksfoex` / `vita-pack-vpk`.
- Secuencia de boot en `rt_boot.cpp`: `make_cpu_box86()` → `Bridge br(cpu)` → `br.add_module(exe)`
  → `br.commit()` (mapea imágenes + pila, abre ventana de trampas) → `register_shim(...)` /
  `set_default_shim(...)` → `br.link()` (parchea IAT) → `make_scheduler(...)`,
  `sched.set_main(entry_va, stack, tib)` → correr.
- `Cpu` es una interfaz abstracta (`runtime/cpu.h`): en host se puede sustituir por una CPU falsa
  que sólo guarda memoria/registros. Así testeamos todo menos la ejecución x86.

## Hecho relevante del binario (estático, `pe_analyze`)
`Hot Wheels® - Micro Racers.exe`: image_base 0x400000, image_size 0x204000, entry_rva 0x12315a,
50010 relocs, 165 imports **sólo de KERNEL32.dll** (incluye CreateProcessW, CreateNamedPipeW,
FindResource…), ~222 MB de overlay. Sospecha: es un wrapper/lanzador y el juego real va en el
overlay (lo confirma HW-001 → `docs/triage.md`). Por eso el esqueleto **recibe la ruta del PE por
parámetro** y no asume nada del exe concreto.

**Actualización (orquestador + test A4, 2026-10-02):** ese exe es el instalador. El juego es
`HotWheels.exe` (430080 bytes, sin overlay, base 0x400000, entry 0x452f05, 94 imports; medido por
`test_skeleton` A4 sobre `extracted/cab-files/HotWheels.exe`).

## Contrato observable
Biblioteca `hw_boot` (`hotwheels/src/boot/`), portable host/Vita, namespace `hw`:

1. `bool hw::read_pe_file(path, std::vector<uint8_t>& out, PeFileInfo& info, std::string& err)`
   - Lee **sólo** cabeceras + datos crudos de secciones (hasta `max(PointerToRawData+SizeOfRawData)`),
     nunca el overlay. `info.file_size`, `info.bytes_read`, `info.overlay_size` informan qué se hizo.
   - Rechaza con `err` no vacío: archivo inexistente, sin `MZ`, sin `PE\0\0`, máquina ≠ i386 (0x14c),
     PE32+ (magic ≠ 0x10b).
2. `bool hw::boot_to_entry(d2rt::Bridge& br, d2rt::Cpu& cpu, const std::vector<uint8_t>& pe, const BootOptions&, BootResult& r, std::string& err)`
   (el `Bridge` lo construye el llamador sobre `cpu`, porque debe vivir mientras corra el juego)
   - `Bridge::add_module` + `commit` + `set_default_shim` (registra cada import no resuelto llamado
     en tiempo de ejecución) + `link`.
   - Prepara la CPU para arrancar: EIP = entry VA del módulo, ESP dentro de la pila mapeada, con una
     dirección de retorno centinela en `[ESP]`.
   - `r.entry_va`, `r.load_base`, `r.import_count`, `r.unresolved_count` (imports sin shim registrado).
   - **No** ejecuta código x86 (eso es responsabilidad del backend; ver nivel Vita).
3. Ejecutable `hotwheels-vita` (`src/main_vita.cpp`, sólo Vita): `make_cpu_box86()`,
   `read_pe_file("ux0:data/hotwheels/<exe>")`, `boot_to_entry`, y arranca el hilo principal en
   `entry_va` con el scheduler de winx86. Log a `ux0:data/hotwheels/boot.log`.

`winx86` se **referencia**, no se copia: `WINX86_DIR` (default
`../d2vita/D2Vita/third_party/winx86`) en CMake (`add_subdirectory`) y en `tools/build_vita.sh`.

## Criterios de aceptación
Host — enlaza `winx86_core` + `cpu.cpp`/`gil.cpp` de winx86 (grupo RESCAN) y `tests/host_stubs.cpp`
(2 globals de perfilado que en Vita aporta `cpu_box86.cpp`) (`hotwheels/tests/test_skeleton.cpp`, CTest, `cmake -S hotwheels -B hotwheels/build && ctest`):
- A1 PE sintético generado por el test (secciones `.text` + `.idata` con import
  `KERNEL32.dll!ExitProcess`, sin relocs, overlay de 1 MiB): `bytes_read` (bytes **físicamente**
  leídos) == 0x600 == fin de la última sección, `overlay_size == 1 MiB`. Ningún byte de overlay se lee.
- A2 Rechazos: inexistente / no-MZ / MZ sin `PE\0\0` / máquina AMD64 / PE32+ (magic 0x20b) /
  Optional Header truncado (< 64 bytes) → `false` + `err` no vacío, sin lectura fuera de rango.
- A3 `boot_to_entry` sobre el PE sintético con `FakeCpu`: EIP == `load_base + entry_rva`;
  ESP mapeado; el slot IAT de `ExitProcess` en memoria invitada apunta a la ventana de trampas
  (≠ valor original del archivo, ≠ 0); `import_count == 1`; ESP == `Bridge::stack_top() - 0x10`.
- Si A4 se saltea y no hay fallas el test sale con 77 → CTest lo reporta **Skipped**, no verde.
- A4 (si existe `hotwheels/extracted/**/HotWheels.exe` — el juego real, extraído de `ISO/HWMR.iso` por HW-001; si no, SKIP explícito): `read_pe_file` OK, `bytes_read+overlay==file_size`, `import_count>0`, EIP == base + entry_rva del header. El exe de 224 MB es el **instalador**, no es objetivo.
Vita (build):
- A5 `hotwheels/tools/build_vita.sh` produce `hotwheels-vita.vpk` sin errores enlazando
  `libwinx86_vita.a` (siempre re-invoca el `build.sh` incremental de winx86). A5 es **sólo
  compilar/enlazar**: no valida boot en Vita.
Emulador (posterior, no bloquea `done` de HW-005; se valida en HW-007 / Vita3K):
- A6 en Vita3K el log muestra la primera trampa de import desde el CRT (prueba de que se ejecutó
  el entry point). Requiere el setup de arena/layout/kubridge/scheduler: feature HW-009, spec 002.

Alcance del shim por defecto: devuelve 0 sin limpieza de pila stdcall (argc desconocido). Sirve
para cablear la IAT y contar imports sin shim; **no** es un fallback ABI-correcto ni valida el CRT.

## No-objetivos
- Shims específicos de Hot Wheels (HW-006), render, audio, input.
- Desempaquetar el overlay (lo define HW-001).
- Modificar D2Vita. Cambios a winx86: sólo aditivos y autorizados por el orquestador, guardados en
  `hotwheels/patches/` (hoy `winx86-0001-stack_top.patch`, ver `docs/winx86-patches.md`).
- Usar consola real.
