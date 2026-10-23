# Spec 002 — Runtime Vita de hotwheels-vita (HW-009, M1)

Owner: impl. Depende de: HW-005 (esqueleto, en review). Validación emulador: HW-004 (Vita3K), HW-014 (harness).

## Problema
`main_vita.cpp` (spec 001) crea `make_cpu_box86()` + `Bridge` con los defaults dispersos de winx86
(módulos 0x30000000, pila 0x60000000, trampas 0x7F000000). En Vita el backend Box86 trabaja sobre
**un único bloque** (`WX86_ARENA`) con `H(va) = va + membase`: todo VA invitado debe caer en
`[0, arena − 16 MiB)`. Además falta lo que `rt_boot` de D2Vita hace antes de correr: mapear heap /
VirtualAlloc / MISC, armar el scratch, dimensionar heap/pila del host, kubridge, scheduler nativo con
TIB principal, NOCANCEL. Sin eso el entry point no puede ejecutarse.

## Cómo lo hace D2Vita (estático, `tools/rt_boot.cpp`, `src/platform/*`, `src/runtime/scheduler_backend.cpp`)
- `d2_boot_config.cpp`: `D2LAYOUT=compact`, `D2ARENA=0x10300000` (259 MiB), `WX86_B5=1`, `WX86_YIELD=2`,
  `QUANTUM`, lee `env.txt` (KEY=VAL) que pisa defaults.
- `vita_present.cpp`: `sceUserMainThreadStackSize = 4 MiB`, `_newlib_heap_size_user = 46 MiB`,
  `wx86_vita_progress_path`. VPK con `ATTRIBUTE2=12` (modo de memoria extendida).
- `apply_compact_layout()`: pack contiguo bajo (heap 0x20000.., módulos 0x03900000, MISC, VA 160 MiB,
  pila 0x0E400000, pilas/TIBs de hilos, trampas 0x0F200000); empuja `D2_MODBASE/STACKBASE/TRAPBASE`
  al Bridge por env **antes** de construirlo; `wx86_set_main_tib(0)`; fuerza `D2_RELOC_EXE=1` porque
  el heap pisa 0x400000.
- mapeo: `cpu->map(HEAP,RW)`, `map(VA,RWX)`, `map(MISC,RWX)` + `wx86_scratch_init(MISC+0x100, …)`;
  `br.set_module_limit(MISC_BASE)`.
- kubridge: `dyn86_vita_kubridge()`; si falta, **sigue** con piscina JIT de 16 MiB y sin handler de faltas.
- scheduler (`make_scheduler`): nativo por defecto — `cpu->thread_emu_create()` como sonda,
  `d2rt_tls_probe()`, `dyn86_set_native(1)`, `new NativeScheduler(cpu, br, stacks, 1 MiB, tibs)`,
  `g_native_sched`, `wx86_set_scheduler`. Luego `set_main(entry, MAIN_STACK_TOP, MAIN_TIB)` y `run()`.
- NOCANCEL: `WX86_NOCANCEL=1` desactiva cancelación pthread en runners (default **apagado** en D2Vita).

## Diferencia clave de Hot Wheels (estático, `pe_analyze`)
`HotWheels.exe` **no tiene relocs** (relocs=0): debe cargar en 0x00400000, imagen 0x79000
(→ 0x00479000). El pack de D2 no sirve (su heap empieza en 0x20000). Layout propio:

| región | base | tamaño | prot |
|---|---|---|---|
| página 0 / TIB principal (FS lineal) | 0x00000000 | 0x1000 (dentro del guard) | — |
| exe (ImageBase fija) | 0x00400000 | imagen del PE | RWX (Bridge) |
| heap invitado | 0x00500000 | 64 MiB | RW |
| módulos (DLLs futuras) | 0x04500000 | 8 MiB (límite = MISC) | RWX |
| MISC (scratch, stubs) | 0x04D00000 | 2 MiB | RWX |
| VirtualAlloc | 0x05000000 | 96 MiB | RWX |
| pila principal (Bridge) | 0x0B000000 | 2 MiB | RW |
| pilas de hilos | 0x0B200000 | 16 × 1 MiB | (scheduler) |
| TIBs de hilos | 0x0C200000 | 1 MiB | (scheduler) |
| ventana de trampas | 0x0C300000 | 1 MiB | sin mapear |
Span = 0x0C400000 (196 MiB); arena = span + 16 MiB = 0x0D400000 (212 MiB).
Presupuesto host: arena 212 + newlib 32 + pila 4 + JIT 2×16 = 280 MiB < ~330 MiB (ATTRIBUTE2=12).
Tamaños provisorios (sin medición del juego todavía); se recalibran con datos de Vita3K/Wine.

## Contrato observable
1. `hw::Layout hw::make_layout(uint32_t exe_base, uint32_t exe_size)` (puro, host): regiones de la
   tabla; falla (`ok=false`, `why`) si el exe se solapa con otra región o el span excede la arena.
2. `hw::layout_env(const Layout&)` → pares `WX86_MODBASE / WX86_STACKBASE / WX86_TRAPBASE /
   WX86_ARENA` (hex) que se aplican con `setenv` **antes** de construir CPU y Bridge.
3. `bool hw::map_runtime(Cpu&, const Layout&, std::string& err)`: mapea heap/VA/MISC y arma
   `wx86_scratch_init(MISC+0x100, MISC_SIZE−0x100)`.
4. `src/runtime_vita.cpp` (sólo Vita) orquesta: `env.txt` → layout/env → `make_cpu_box86()` →
   kubridge (log `kubridge: presente|AUSENTE`) → `map_runtime` → `Bridge` (+`set_module_limit`) →
   `boot_to_entry` → scheduler nativo → `set_main(entry, layout.main_stack_top, 0)` → `run()`.
   El shim por defecto loguea `[hw] trap #n DLL!func` en `ux0:data/hotwheels/boot.log`; con
   `HW_STOP_AT_FIRST_TRAP=1` (default en M1) detiene el scheduler en la primera trampa.
5. NOCANCEL: `WX86_NOCANCEL` se respeta desde `env.txt`; default igual que D2Vita (apagado).

## Criterios de aceptación
Host (`tests/test_runtime.cpp`, CTest):
- B1 `make_layout(0x400000, 0x79000)`: ok; regiones sin solapes entre sí ni con el exe; todas
  dentro de `[0x1000, arena − 16 MiB)`; `main_stack_top == stack_base + 2 MiB`.
- B2 `make_layout(0x400000, 0x200000)` (exe que pisaría el heap) → `ok=false` con `why`.
- B3 tras aplicar `layout_env`, un `Bridge` nuevo reporta `stack_top() == layout.main_stack_top` y
  carga el exe en 0x400000; las trampas de la IAT caen en la ventana de trampas del layout.
- B4 `map_runtime` con `FakeCpu`: heap RW, VA y MISC RWX mapeados con los tamaños del layout.
Vita (build): B5 `tools/build_vita.sh` produce el VPK con `ATTRIBUTE2=12` y `runtime_vita.cpp`.
Emulador (Vita3K, cuando HW-004/HW-014 lo permitan):
- B6 `boot.log` muestra `kubridge: …`, el layout, y `[hw] trap #1 <DLL>!<func>` = primera
  importación llamada por el CRT ⇒ el entry point corrió traducido por el dynarec.
- Si Vita3K no soporta kubridge: documentarlo (FINDINGS, nivel `emulador`) y verificar que el
  fallback sin kubridge (piscina JIT 16 MiB, `sceKernelAllocMemBlockForVM`) funciona; si tampoco,
  evaluar modo intérprete de winx86 o memoria ejecutable que Vita3K acepte (registrar en `coordination/`).

## No-objetivos
Shims de juego (HW-006), DirectDraw/presentación (HW-013, se integra después), audio, input,
tamaños calibrados por medición, consola real.
