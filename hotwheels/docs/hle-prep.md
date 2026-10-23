# HW-052 — preparación HLE (opus, 2026-10-03; estático)

Esperando `docs/guest-profile.md` (HW-051) para elegir objetivos >3 %. Esto es la base lista.

## Mecanismo (winx86, sin cambios)
`dyn86_intrin_add(va, fn, retn, inmask, regmask, name)` (`src/dynarec86/dyn86_intrin.h`): en el inicio de bloque en `va` el traductor emite `bl helper(emu, esp)`; si devuelve 0 cae al cuerpo traducido original (fallback fiel). `dyn86_intrin_on`: 0 apagado, 1 nativo, 2 sólo plomería (mide costo del round-trip). Referencia de uso: `D2Vita/src/runtime/d2_intrin_114.cpp` (proyección 0x50dd60). Reglas: `inmask` obligatorio para args en registros; `regmask` = todo registro que el cuerpo guest deja y un llamador puede leer (decide el desensamblado, no la convención); flags quedan intactos.

## Integración prevista en Hot Wheels
- `src/runtime/hw_intrin.cpp`: registro de objetivos tras `boot_to_entry` y antes de `ns->run()`; `HW_INTRIN=0|1|2|3` (3 = **sombra**: ejecuta el nativo sobre una copia de la memoria de salida, deja correr el guest y compara; loguea diferencias → test diferencial con entradas reales en consola/Vita3K/qemu).
- Test diferencial host obligatorio por objetivo: entradas aleatorias + volcadas reales, nativo vs guest traducido (harness qemu-arm de winx86) — bit a bit en memoria de salida y registros de `regmask`.

## Prueba de captura (`tools/verif_hle_capture.py`, alineado a instrucciones, ignora saltos internos)
HotWheels.exe md5 `4fbf44e9571a22801cbd6a926280eb19`. Límites = siguiente inicio de función (destinos de call ∪ punteros `$0x…` a .text).
| función (re-engine.md) | rango | call directos | entradas externas al cuerpo | ret | veredicto |
|---|---|---|---|---|---|
| gpuDrawPacket | 0x41c1e0..0x41cecf (3.3 KB) | 1 | 0 | 18 | COMPLETA |
| swRasterFT3 (opaco) | 0x41d5a0..0x420d2f (14 KB) | 0 (tabla) | 0 | 5 | COMPLETA |
| swRasterFT3Semi | 0x420d30..0x421ebf (4.5 KB) | 0 (tabla) | 0 | 1 | COMPLETA |
| timerWaitFrame40Hz (HW-056) | 0x412df0..0x412f3f | 3 | 0 | 1 | COMPLETA |
`swRasterFT3` con 5 `ret` en 14 KB probablemente agrupa variantes (por modo de textura); el HLE puede empezar por la variante dominante y devolver 0 (fallback) en el resto. Pendiente: decompilar (Ghidra MCP no estaba levantado; usar analyzeHeadless) y medir con el perfil.

## Primer objetivo: spanFT3 0x450af3 (opus, 2026-10-03)
- Rasterizador SW: `swRasterFT3` (float/x87, globales) delega cada span a 0x450af3, bucle x86 escrito a mano (textura 256×256 indexada por bl/bh, `add ecx,ebp`+`adc bl,dl`, 4 px/vuelta derecha→izquierda; estado en [0x4679c8]/[0x4679cc] armado por 0x450a40). Objetivo entero puro, cdecl, 5 args.
- Nativo: `src/hle/span_ft3.c` (transcripción exacta, flags SF/OF de jl/jge incluidos). `inmask=AX` (la cola <4 px conserva los bytes altos de EAX), `regmask=AX|CX|DX`, `retn=0`. Fallback si destino/página de textura salen de la arena o n>4096.
- **Test diferencial**: `tests/test_hle_span_ft3.py` ejecuta los bytes x86 ORIGINALES en Unicorn y compara memoria + EAX/ECX/EDX con el C: 300 casos aleatorios PASS; mutación (quitar acarreo de adc) detectada.
- Vita3K, carrera SW 800×600, `HW_INTRIN=1`: 68.4 M llamadas, 68.3 M servidas (0.13 % fallback), render correcto (captura revisada), sin fault.
- Pendiente: A/B de rendimiento (Vita3K bloqueado por WIP ajeno en `extract_log_delta.py`; consola bloqueada) y `HW_INTRIN=2` (costo de plomería). Default sigue en 0 hasta medir en consola.

## Segundo objetivo: spanFT3Semi 0x450c19 (opus)
- Mismo esquema + 6.º arg `blend` (tabla 64 KiB: al=texel, ah=destino), 1 px/vuelta con escritura en pipeline; `mov dh,[eax]` toca el contador edx antes de cada `sub` (transcrito exacto). `inmask=0`, `regmask=AX|CX|DX`. Captura COMPLETA (0x450c19..0x450c89).
- Diferencial Unicorn: 300 casos (mitad opaco, mitad semi) PASS; mutación real (leer [edi] en vez de [edi-1]) detectada (una mutación de orden escritura/lectura resultó equivalente: direcciones distintas).
- Vita3K (HEAD 8ec91a5, carrera SW 800×600, `HW_INTRIN=1`, 60 s): spanFT3 76.0 M llamadas (99.89 % servidas), spanFT3Semi 3.86 M (99.94 %); render correcto incl. marcas de neumático semitransparentes (captura revisada).
- A/B Vita3K (HEAD 8ec91a5, carrera SW 800×600, 60 s, 2 legs intercalados por modo): HW_INTRIN=0 → 2200/2100 frames; =1 → 2200/2200. Neutro: el juego está topado por su busy-wait de 40 Hz y Vita3K no es CPU-bound aquí; la ganancia debe medirse en consola (CPU A9 444 MHz) con fps-window/perfil.

## Medición HW-052 en qemu-arm (opus, 2026-10-03; worktree aislado .wt-opus = HEAD 31e3927 + camino rápido)
Carrera SW 320×240 (settings de fábrica) con `race_enter.script`, 40 s/pata, patas intercaladas.
- Frames y CPU total NO sirven: el guest va ~29 fps y el spin del timer (HW-056) absorbe la holgura (qemu siempre 100 % de un core).
- Métrica válida: `HW_QPC_IDLE_US=300` (el spin duerme) + **ms de CPU del proceso qemu por frame**. Pares estables: HLE=1 31.57 / 31.18 ms; HLE=0 29.95 / 30.77 ms → **HLE de spans ≈3 % MÁS LENTO** (un 3.er par bajo load≈7 descartado). Coherente con menos siestas del timer con HLE (1358–2113 vs 2151–2949 por ventana) y con el modo 2 ≈ baseline.
- Primera versión (transcripción literal registro a registro): claramente peor (frames 674/760 vs 1105/1126 sin idle). La reformulación cerrada (40 bits `X+=S`, offset `(X&0xff00)|((X>>32)&0xff)`, ~7 instr/px, fallback exacto si dst se solapa con textura/tabla; diferencial 400 casos PASS incl. solapes, mutación detectada) no alcanza.
- Causa (teoría + números): ~4.2 M spans/≈15 s ≈ 7 k spans/frame a 320×240 → ~11 px/span. El costo fijo por llamada (plomería intrin + 6 args + rangos + 4 llamadas cross-TU a regs + contadores, ~80–100 instr) se come el ahorro por píxel. La granularidad span es incorrecta para HLE.
- Decisión: default OFF (sin cambios de comportamiento). Siguiente paso con impacto: HLE a nivel **triángulo** (`swRasterFT3`/`Semi`: setup x87 + todos sus spans en una llamada; MSVC fija precisión 53 bits → reproducible con double ARM redondeando a float en cada store), medido en consola con PMU (qemu no representa el A9: TCG reescribe ambos lados).

## HLE a nivel triángulo: descartado por Amdahl (opus, 2026-10-03; qemu, métrica limpia)
- Cota superior con `HW_DIAG_SKIP_FT3=1` (swRasterFT3/Semi = no-op, render roto a propósito), `HW_QPC_IDLE_US=300`, carrera SW 40 s, patas intercaladas: skip 26.66 / 25.19 ms CPU/frame vs base 29.71 / 28.72 → **el raster de triángulos completo pesa ≈11–12 %** del CPU por frame en qemu.
- Un HLE nativo sigue dibujando los mismos píxeles: recupera sólo una fracción (estimado 5–7 %), por debajo del umbral ≥10 % fijado por el orquestador. No se implementa. El 48 % del perfil HW-051 era mayormente otra cosa (sesgo de atribución: todo lo que corre entre el shim Lock y el siguiente shim).
- Preparado para quien retome: `__ftol` declarado `longlong __ftol(double ST0)` en Ghidra (script en scratch de opus) muestra los 50 operandos x87; semántica de referencia = double IEEE (box86) ≡ x87 con CW=0x27F.
- Siguiente con mayor techo medible: el ~88 % restante (cómputo de juego, traducción/dispatch, shims) — requiere un perfil fiel (EIP por bloque o PC host → dynablock; ver FINDINGS 25a305f).

## __ftol / FISTP (perfil fiel HW-051 carrera: __ftol 6 % + arm_fist64_3 6 %) — opus, 2026-10-03
- HLE de `__ftol` (0x4521c4, 401 call, captura completa) en `src/hle/emu_glue.c`: exacto — modo sombra `HW_FTOL_CHECK=1` comparó 16.55 M resultados reales contra el `fistp` del guest: 0 discrepancias (scratch pisado excluido por EBP/CW intactos). Pero A/B limpio (load<1): **+2.0 % más lento** (pares +1.2/+2.0/+3.5): la plomería del intrin cuesta más que el cuerpo; ambos llaman a arm_fist64_3. Queda sólo para validar (no se registra salvo `HW_HLE_TARGETS=ftol`).
- La ganancia real: **winx86-0008** — FISTP m64 inline en el JIT para RC=truncar (VCVT.S32.F64 + detección de NaN/saturación → helper). Validado con la sombra: 18.73 M comparaciones, 0 discrepancias. A/B código (3 pares): −18.7 (carga) / −4.6 / −4.4 % → **mediana −4.6 % CPU/frame** en carrera qemu. En el A9 debería ser mayor (helper con trunc() y saturación vs VCVT en hardware).

## Raster a nivel triángulo por AOT x86→C (opus, 2026-10-03) — 8951639
- `tools/aot_x86.py` traduce swRasterFT3/Semi instrucción por instrucción (x87 en double ≡ box86, flags perezosos exactos, pila guest real). Diferencial Unicorn (imagen completa, CW=0x27F): 300 triángulos PASS bit a bit; mutación detectada. El C generado NO se commitea (traducción del código del juego): `tools/gen_aot_raster.sh` lo crea localmente; sin él, fallback al guest.
- A/B qemu (ab_cpu_frame, 4 pares; los 2 con load≈1: +2.3 / +2.6 %): **neutro/levemente peor** pese a la cota ≈19 %. La traducción de box86 de este código lineal ya es de calidad cercana a C, y qemu emula VFP por software en ambos lados (domina el costo). Conclusión de método: **qemu no sirve para medir ganancias en código float-intensivo**; ganancia real a medir en consola (A9 con VFP hardware). Mejoras posibles del generador si la consola muestra potencial: pila x87 con profundidad estática → locales escalares (hoy array indexado), evitar `&eax` en span_setup.
- Estado HW-052: todo opt-in y exacto (spans, ftol-sombra, raster AOT); ganancias medidas en qemu sólo en el JIT (winx86-0008 −4.6 %).
