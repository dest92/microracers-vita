# Spec 010 — IO Vita (HW-020)

- Feature: HW-020 · Milestone: M4 · Dueño: ports · Estado: implementada (tests host CA1–CA12 verdes), pendiente review
- Depende de: HW-006 (`hw::shims::Vfs` + `CdDrive`, spec 006). Coordina con `re` (dueño de HW-006:
  **extender, no duplicar**), HW-016 (bits 007, mapa de botones) y HW-009 (boot/threads).

## Problema
En Vita los datos del juego viven en `ux0:data/hotwheels/` (el usuario copia su copia legal, ver
`docs/INSTALL.md`). El runtime debe:
1. **Mapear** las rutas del guest (cwd, `C:\`, `\\.\D:`) a esa carpeta, reusando el FS case-insensitive
   y la unidad de CD que ya hizo HW-006 (`hotwheels/src/shims/core.cpp`). No se reimplementa nada de eso.
2. **Escribir** los pocos archivos que el juego reescribe (`settings.dat`, `highscore.dat`, y
   `physics.dat` por las dudas) en un overlay persistente, sin tocar los datos base (FINDINGS #16).
3. **Ahorrar I/O**: D2Vita documenta que cada `ReadFile` cuesta **decenas de ms** en la memory card
   (`d2vita/.../io_stat.cpp`, cabecera) y hubo tirones de hasta ~438 ms (dato del orquestador,
   experiencia D2Vita). Los archivos chicos que el juego lee al arrancar (`data\menu\*.dat`,
   `lang.txt`, `sinus.sin`, `*.tga`/`*.sfp`/`shade.tab` de menú) deben quedar **precargados en RAM**.
4. Leer **`ux0:data/hotwheels/hwvita.ini`**: mapa de botones (para HW-016), escalado (`fit`/`stretch`)
   y filtro (`point`/`linear`).

## Contrato observable
Módulo `hw::io` en `hotwheels/src/io/`, portable C++17, **reusa** `hw::shims::Vfs` (`src/shims`).

### `paths.h` — política de rutas
```cpp
struct Roots { std::string game, cd, save; };
Roots vita_roots(const std::string& data_root = "ux0:data/hotwheels");
Roots host_roots(const std::string& extracted_root);   // tests/qemu
bool  ensure_save(const Roots&);                        // crea .../save
void  mount(hw::shims::Vfs&, const Roots&);             // C:=game+save, D:=cd, cwd "C:\\"
bool  is_writable_game_file(const std::string& leaf);   // settings/highscore/physics.dat (case-insens.)
```
- `C:\` y el cwd → `ux0:data/hotwheels` (lectura) + `ux0:data/hotwheels/save` (escritura).
- `\\.\D:` **no** pasa por `Vfs`: es un device handle de `CreateFileA` + `CdDrive` (HW-006), con
  `hwicon.bmp` bajo la misma carpeta (`cd = game`). HW-020 no duplica `CdDrive`.
- `mount()` sólo llama `Vfs::mount`/`set_cwd` (comportamiento case-insensitive y `..` ya probado en HW-006).

### `read_cache.h` — caché RAM de lecturas completas
```cpp
struct CacheStats { uint64_t files, bytes, hits, misses, bypasses, preload_us; };
class ReadCache {
  void configure(size_t max_file_bytes = 2u<<20, size_t max_total_bytes = 32u<<20);
  bool load(const std::string& host_path);           // entero, si <= max_file y entra en max_total
  const std::vector<uint8_t>* get(const std::string& host_path) const;
  size_t preload_dir(const std::string& host_dir, bool recursive = true);  // devuelve archivos
  void clear();
  const CacheStats& stats() const;
};
```
- Sólo cachea archivos que entren en el umbral; un archivo grande (p. ej. una pista de música)
  se marca `bypass` y se lee normal.
- `preload_dir` mide bytes/tiempo (publicados en `stats()`); no aborta el arranque si una lectura falla.
- **Medido** sobre `extracted/original/Program_Files/DATA` (99 archivos, **16.41 MB**):

  | umbral | archivos | tamaño |
  |---|---|---|
  | ≤ 64 KB | 53 | 1.24 MB |
  | ≤ 256 KB | 74 | 3.03 MB |
  | ≤ 1 MB | 96 | 11.02 MB |
  | ≤ 2 MB | 99 | 16.41 MB |

  Default `max_file_kb=2048`, `max_total_mb=32` ⇒ precarga **DATA/ entero** (16.41 MB); `MUSIC/`
  (≈50 MB/pista) queda fuera por tamaño y se lee bajo demanda. Decisión a validar en consola.

### `config.h` — `hwvita.ini`
```cpp
struct HwVitaConfig {
  std::string data_root = "ux0:data/hotwheels";
  enum class FpsMode { Original, Interp60, Native60 } fps_mode = FpsMode::Original; // HW-023
  enum class Scaling { Fit, Stretch }   scaling = Scaling::Fit;
  enum class Filter { Point, Linear }   filter  = Filter::Point;
  std::string control_scheme = "original";  // preset de mapa (HW-024)
  bool preload = true;
  size_t max_file_kb = 2048, max_total_mb = 32;
  bool mouse_from_right_stick = true; int stick_deadzone = 8000;
  bool  btn_set[2][hw::dinput::kButtonCount] = {};
  uint8_t btn_dik[2][hw::dinput::kButtonCount] = {};
  std::vector<std::string> warnings;
};
const char* fps_mode_name(FpsMode);  bool fps_mode_parse(const std::string&, FpsMode&);
std::vector<std::string> scheme_names();                 // presets registrados
void apply_control_scheme(hw::dinput::MapperConfig&, const std::string& scheme);
bool parse(const std::string& text, HwVitaConfig& out);  // no lanza; llena warnings
bool load(const std::string& path, HwVitaConfig& out);   // ausente => defaults, true
std::string defaults_ini();                              // texto comentado
std::string to_ini(const HwVitaConfig&);                 // estado ACTUAL
bool save(const std::string& path, const HwVitaConfig&); // lo usa el selector
void apply_to(hw::dinput::MapperConfig&, const HwVitaConfig&);  // preset + overrides
```
Formato INI: secciones `[general]`, `[display]`, `[io]`, `[buttons.pad0]`, `[buttons.pad1]`;
`;`/`#` comentan; claves desconocidas se ignoran (forward-compat); valor inválido ⇒ default +
`warnings`. Claves: `fps_mode=original|interp60|native60` (HW-023), `control_scheme=<preset>`
(HW-024), `scaling=fit|stretch`, `filter=point|linear`, `preload`, `max_file_kb`, `max_total_mb`,
`mouse_from_right_stick`, `stick_deadzone`, y overrides de botones (`cross=RSHIFT`, …).
Presets de mapa: `original` (= `ButtonMap::for_pad(0/1)` de HW-016) y `swapped` (intercambia los dos
pads); el set final lo define **HW-024** extendiendo `scheme_names()`.

### `boot_menu.h` + `boot_selector.{h,cpp}` — selector de arranque
```cpp
enum class MenuAction { Up, Down, Left, Right, Confirm, Cancel };
enum class MenuResult { Continue, SaveAndExit, CancelExit };
class BootMenu {
  explicit BootMenu(HwVitaConfig&);
  int count() const; int selected() const;
  std::string label(int) const; std::string value(int) const;
  MenuResult handle(MenuAction);                 // navega y cicla valores
  std::vector<std::string> lines() const;         // para el backend de dibujo
  bool save(const std::string& path);
  HwVitaConfig& config();
};
bool boot_should_open();                          // mantiene L al bootear (o HW_CFG=1)
int  run_boot_selector(const std::string& ini_path);  // 1 = guardó
```
- **Modelo portable** (items: FPS mode, Control scheme, Scaling, Filter) navegable y testeable en
  host; en host `run_boot_selector`/`boot_should_open` son no-ops (el env `HW_CFG=1` fuerza abrir).
- **Vita**: dibuja una **pantalla propia** antes de lanzar el juego con `sceDisplay` (framebuffer
  A8B8G8R8 960×544, sin vitaGL/GXM) y la fuente 8×16 generada de winx86
  (`platform/vita_kb_font.h`); navega con d-pad/Cross/Circle, y al confirmar guarda `hwvita.ini`.
  Se abre **manteniendo L** (`SCE_CTRL_LTRIGGER`) al bootear.

## Criterios de aceptación (cada uno ⇒ un test)
Test host `hotwheels/tests/test_io.cpp` (runner `tests/run_io.sh`; sin vitasdk):
- [ ] **CA1** `vita_roots("ux0:data/hotwheels")` → `{game=cd="ux0:data/hotwheels", save=".../save"}`;
  `mount()` deja `C:` y `D:` montadas, cwd `C:\`.
- [ ] **CA2** `Vfs` (reusada): `data\menu\settings.dat` resuelve case-insensitive a la base; tras
  `resolve_write` de `data\menu\settings.dat`, la ruta cae en `save/` y `resolve_read` la prefiere;
  `highscore.dat` también es escribible; `is_writable_game_file` distingue.
- [ ] **CA3** `ReadCache`: `load`/`get` de un archivo chico devuelve los bytes; un archivo >
  `max_file_bytes` se salta (`bypass`); `preload_dir` carga sólo los chicos y `stats().bytes` es la suma.
- [ ] **CA4** `parse()` de un INI de prueba: `scaling=stretch`, `filter=linear`, `cross=SPACE`,
  `max_file_kb=512` se leen; `apply_to(MapperConfig)` cambia el `ButtonMap` del pad; `#`/`;` ignorados.
- [ ] **CA5** `defaults_ini()` parsea de vuelta a los defaults (round-trip); `parse` de valores
  inválidos deja el default y agrega `warnings`; clave desconocida se ignora.
- [ ] **CA6** Integración-lite: montar un `Roots` host con un `DATA/` sintético (2 chicos + 1 grande),
  preload con umbral, verificar que los chicos están en caché y el grande no, y que el total medido
  coincide con `stats().bytes`.
- [ ] **CA7** `parse()` lee `fps_mode=interp60` y `control_scheme=swapped`; `fps_mode_name` round-trip
  con los 3 valores.
- [ ] **CA8** `fps_mode` inválido ⇒ queda `original` + `warnings`.
- [ ] **CA9** `BootMenu`: 4 items, `Up/Down` mueven la selección, `Left/Right` ciclan cada valor
  (fps original→interp60→native60→original; scheme; scaling; filter), `lines()` marca el seleccionado,
  `Confirm` ⇒ `SaveAndExit`, `Cancel` ⇒ `CancelExit`.
- [ ] **CA10** `save()` + `load()` round-trip: fps_mode/control_scheme/scaling/filter sobreviven.
- [ ] **CA11** `apply_control_scheme`: `original` == `ButtonMap::for_pad(0/1)`; `swapped` intercambia.
- [ ] **CA12** `boot_should_open()`: false sin env; true con `HW_CFG=1` (host).

## Diseño (bajo nivel)
- **Reuso total de HW-006**: `Vfs` ya implementa case-insensitive, cwd, `..`, overlay de escritura
  (`resolve_write` crea directorios y `CreateFileA` hace copy-on-write base→save). HW-020 sólo aporta
  la **política** (`Roots`, `is_writable_game_file`) y los módulos nuevos.
- **Caché**: `std::map<std::string, std::vector<uint8_t>>` (host path → bytes), single-thread (el juego
  es monohilo, FINDINGS #6). `preload_dir` usa `std::filesystem` (host) / `sceIo` (Vita, fuera de esta
  unidad); mide con `std::chrono`. La integración real con `ReadFile` es **una línea** en
  `src/shims/shims.cpp` (dueño `re`): registrado el `ReadCache` en `Context`, `ReadFile` sirve del
  cache si `get(path)` existe. Handoff en `coordination/re.md`; no se edita su archivo acá.
- **Escrituras**: `settings.dat`/`highscore.dat` van a `save/`; el resto de la base queda read-only.
  En Vita el `save` es `ux0:data/hotwheels/save` (persistente entre sesiones).
- **hwvita.ini**: parser propio (no hay INI en el repo). `apply_to` toca `MapperConfig` de HW-016 sin
  duplicar su tabla de DIK (reusa `hw::dinput::Button` y `dik::*`; los nombres extra se resuelven con
  la tabla estándar DIK de MinGW `dinput.h`).
- **Escalado/filtro**: la config expone `scaling`/`filter`; el renderer (HW-010/HW-013) los consume
  (`fit` → `fit_rect` de `docs/ports-patterns.md` §4; `point`/`linear` → filtro de textura).

## No-objetivos
- Reimplementar `Vfs`/`CdDrive` (HW-006) o el recorrido de TOC.
- Editar `src/shims/*` (de `re`): la integración del cache se pide por handoff.
- `sceIo` real de Vita y `MUSIC/` streaming (la caché es para archivos completos chicos).
- Renderer/DDraw (HW-010/HW-013) y DInput (HW-016): HW-020 sólo entrega la config.
- Consola real.

## Riesgos / preguntas abiertas
- El umbral de precarga (2 MB/DATA entero = 16.41 MB) es una apuesta de memoria; medir en consola y
  ajustar por `hwvita.ini` (`max_file_kb`/`max_total_mb`).
- Integración del cache en `ReadFile` depende de `re`/impl; sin ella la caché no reduce stalls aún.
- ¿`settings.dat` se lee de `save/` pero el juego espera la versión base en el primer arranque? El
  overlay de HW-006 ya hace copy-on-write, pero conviene trazarlo en el boot real.
- Formato exacto del INI: es nuestro (no del juego); se documenta en `docs/INSTALL.md`.

## Evidencia (cómo se valida)
- `[host]` `tests/run_io.sh` (CA1–CA12).
- `[estático]` tamaños reales de `DATA/` (tabla de arriba) medidos con `find DATA -type f -printf '%s'`.
- `[estático]` D2Vita `io_stat.cpp` (coste por `ReadFile` y read-ahead) como justificación.
- `[consola]` posterior: medir tiempo de boot con/sin precarga (no bloquea `done`).
