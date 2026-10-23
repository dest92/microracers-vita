# HW-072 — RE de las funciones guest más calientes (REA + Ghidra 12.1.4)
- Autor: `rea-re` · Fecha: 2026-10-21 · Evidencia: **[estático]** vía REA/Ghidra 12.1.4 sobre
  `extracted/original/Program_Files/HotWheels.exe` (base `0x400000`, SHA-256 `5418a6aa…1122`), cruzado con el perfil EIP de
  `docs/guest-profile.md` (HW-051, niveles `[qemu]`/`[host]`).
- Alcance: las 5 funciones guest más calientes de la corrida **race** + las vecinas que importan para el ranking de HLE.

## 0. Advertencia de alcance (leer antes de optimizar)
El perfil de `guest-profile.md` corre el backend **Linux/QEMU con DirectDraw software y presentador PNG**: la categoría
"render" de ese perfil **no es VitaGL**. Y la carrera del perfil ejecuta el **rasterizador software** (aparecen
`swRasterFT3` 3,70 % y `swRasterFT3Semi` 0,54 % en race) → es la carrera con 3D=NO, que es **la configuración que trae el
repack** (`FINDINGS.md` #17: `settings.dat` línea 1 = 0, 800×600×8 software). Con 3D=ON el motor no llama al rasterizador:
va `gpuDrawPacket` → `d3dDrawPrim` (`docs/re-tracks.md` §4). **Conclusión operativa**: los HLE del rasterizador rinden en la
config SW (y en el menú, que siempre es SW); si la medición de fps de consola se hace con 3D=ON, el peso se mueve a
`gpuDrawPacket` + `__ftol`. Confirmar con qué `settings.dat` se mide antes de elegir el HLE.

## 1. Tabla resumen de las 5 más calientes (race)
| # | EIP | Nombre | race % | menu % | Tamaño | x87 | Llama a shims | Callers |
|---:|---|---|---:|---:|---:|---|---|---:|
| 1 | `0x004521c4` | `__ftol` | **6,06** | 2,13 | 39 B, 1 bloque | **sí (FSTCW/FLDCW/FISTP)** | no | **35** |
| 2 | `0x00450af3` | `FUN_00450af3` | **4,19** | — | 144 B, 5 bloques | no | no | **1** (`swRasterFT3`) |
| 3 | `0x0041d5a0` | `swRasterFT3` | **3,70** | 2,39 | 4.462 B, 55 bloques | indirecta (`__ftol`) | no | — |
| 4 | `0x00412df0` | `timerWaitFrame40Hz` | **3,03** | — | 331 B, 17 bloques | **sí (escala double + `__ftol`)** | **sí (3)** | — |
| 5 | `0x00422320` | `FUN_00422320` | **1,94** | 0,73 | 645 B, 38 bloques | no (entero 0.12) | no | **14** |

Vecinas relevantes: `gpuDrawPacket` `0x41c1e0` 1,09 % (race) / 0,85 % (menu), `swRasterFT3Semi` `0x420d30` 0,54 %,
`inpUpdate` `0x41bb80` **7,05 % en menú** (la más caliente del menú), `FUN_00450c19` 0,61 %, `FUN_004073c0` 0,65 %.
Helper del dynarec dominante: **`arm_fist64_3` 5,92 %** — es el helper x87 de `FISTP`, es decir **el costo de las conversiones
x87 que `__ftol` provoca** (ver §2.1).

## 2. Detalle por función

### 2.1 `__ftol` — `0x004521c4` (6,06 % race, la más caliente)
- **Propósito**: `_ftol` del CRT de MSVC (Ghidra lo etiqueta "Library Function - Single Match", librería Visual Studio).
  Convierte `ST(0)` a entero con **truncado hacia cero** y devuelve `int64` en `EDX:EAX`.
- **Entradas/salidas**: entrada = `ST(0)` (registro x87; sin argumentos en la pila). Salida = `EDX:EAX` = `trunc(ST0)`
  como `int64`; **`ST(0)` se descarta** (el `FISTP` hace pop → el tope del stack x87 sube 1 y cambia el tag).
- **x87 — secuencia exacta** (leída instrucción por instrucción, 39 B):
  ```
  0x4521c4 PUSH EBP / MOV EBP,ESP / ADD ESP,-0xc
  0x4521ca FSTCW [EBP-2]        ; guarda el control word
  0x4521ce WAIT
  0x4521cf MOV AX,[EBP-2] / OR AH,0xC   ; bits 10-11 = RC = 11b = truncar hacia cero
  0x4521d6 MOV [EBP-4],AX / FLDCW [EBP-4]
  0x4521dd FISTP qword [EBP-0xc]        ; ST0 -> int64 (truncando) y pop
  0x4521e0 FLDCW [EBP-2]        ; restaura el control word (efecto neto nulo)
  0x4521e3 MOV EAX,[EBP-0xc] / MOV EDX,[EBP-8]
  0x4521e9 LEAVE / RET
  ```
  → **El par `FSTCW`/`FLDCW` existe sólo para que `FISTP` trunque**: el control word se restaura siempre, así que para el
  guest es **semánticamente equivalente a "truncar y hacer pop"**. Ese es el punto clave del HLE.
- **Llamadas a shims**: ninguna. Hoja.
- **Callers**: **35 sitios**, entre ellos `buildShadeTables` `0x404cf0`, `timerWaitFrame40Hz` `0x412df0`, `gpuDrawPacket`
  `0x41c1e0`, `swRasterFT3` `0x41d5a0`, `swRasterFT3Semi` `0x420d30`. Es el leaf más invocado del motor.
- **HLE: SÍ, candidato N.º 1 — reconciliado con lo ya hecho.** perf-cpu ya **inlineó `FISTP`** con `VCVT.S32.F64` para
  RC=truncar (winx86-0008, −4,6 % CPU/frame en qemu, sombra 18,73 M casos / 0 diferencias; `docs/perf-cpu-072.md`). Lo que
  aporta mi lectura es *por qué queda costo y dónde*: el par `FSTCW`/`OR AH,0xC`/`FLDCW`/`FLDCW` es **neto nulo** y existe
  sólo para forzar el truncado en `FISTP` → una regla de dynarec que reconozca esa **secuencia completa** (o la entrada de
  `__ftol` como función) puede emitir *truncar + pop* **sin emular los dos `FLDCW`** ni cruzar frontera de bloque.
  Contrato exacto a respetar:
  1. leer `ST(0)` del cache x87 (mismo acceso que el camino inline ya usa);
  2. `int64 r = (int64)trunc(st0)`, **truncado hacia cero** (no `round()` ni `floor()`);
  3. escribir `EDX:EAX = r` y **hacer pop del stack x87** (decrementar el tope, actualizar tag vacío);
  4. **borde**: `FISTP` levanta `#IA` y devuelve el "integer indefinite" **`0x8000000000000000`** si `ST0` es NaN o queda
     fuera de `int64`; reproducirlo, no devolver 0. Son justo los casos que hoy caen al helper (saturación) — si el camino
     inline no los cubre, ahí está el resto del 5,92 % de `arm_fist64_3`.
  **Verificación antes de codear**: confirmar que el guest **no lee** la FPU status word (grep de `FNSTSW`/`FSTSW` en el exe).
  Si la lee, el `#IA` deja de ser irrelevante y la regla no puede ignorar el status.

### 2.2 `FUN_00450af3` — `0x00450af3` (4,19 % race)
- **Propósito**: **escritor de span con textura afín del rasterizador software** (el bucle interno de `swRasterFT3`). Escribe
  bytes de **índice de paleta 8 bpp** en el framebuffer: recorre la textura con un acumulador **punto fijo 16.16** y escribe
  **4 píxeles por iteración** (los `CONCAT31(...)` de 4 bytes son el empaquetado de 4 lecturas contiguas).
- **Entradas/salidas**: firma observada `__cdecl FUN_00450af3(u32 *dst, u32 p2, u32 p3, int p4, u32 p5)`.
  `dst` = puntero al framebuffer (se **decrementa** en el bucle: se escribe hacia atrás); `p2`/`p3`/`p5` = componentes del
  paso U/V y de la dirección base de textura empaquetadas en 32 bits; `p4` = número de iteraciones de 4 píxeles
  (`p4 << 8` combinado con el byte `DAT_004679cc`; el bucle resta `0x40000` por iteración → `p4` cuenta grupos de 4 filas/pasos).
  Salida: **memoria del framebuffer** (no devuelve valor). Lee de la página de textura; **no** expande paleta (trabaja en
  espacio de índices) — coherente con que el framebuffer de este camino sea 8 bpp.
- **x87**: **no** (aritmética entera y `CARRY4` en los `CONCAT`).
- **Llamadas a shims**: ninguna. Hoja.
- **Callers**: **exactamente 1**: `swRasterFT3` `0x41d5a0`. Toda la muestra de esta función es el bucle interno del
  rasterizador opaco.
- **HLE: NO prioritario — medido neutro en qemu.** perf-cpu midió HLE/AOT de este span como **neutro en qemu** (el VFP
  emulado domina) y describe el lazo como textura indexada por `bl/bh` con `add ecx,ebp` + `adc bl,dl` (`docs/perf-cpu-072.md`,
  `docs/hle-prep.md`). Mi aporte es estructural: **tiene un único caller** (`swRasterFT3`), así que no hay otro consumidor que
  proteger, y como escribe **índices de paleta** el contrato es *byte a byte exacto* → si en A9 se decide atacar el rasterizador,
  este lazo nativo (vectorizable) es la parte medible y el test es comparar el back buffer 8 bpp completo.
  El layout exacto de los campos de `p2/p3/p5` sigue siendo lectura parcial mía (§4.1); la descripción de perf-cpu del lazo
  cubre más detalle que la mía — **partir de la suya**.

### 2.3 `swRasterFT3` — `0x0041d5a0` (3,70 % race)
- **Propósito**: rasterizador **software** de `POLY_FT3` (triángulo/quad con textura, opaco) — ya documentado en
  `docs/re-engine.md` §3.0/§3.1: escribe píxeles en una superficie 8 bpp en memoria de sistema. Variante semitransparente
  = `swRasterFT3Semi` `0x420d30` (0,54 %), que usa las tablas de mezcla construidas por `buildShadeTables` `0x404cf0`.
- **Entradas/salidas**: firma observada `(float *p1, float *p2, float *p3)` = **3 punteros a registros de vértice**.
  Quien los prepara es `gpuDrawPacket` (`0x41c1e0`): ordena los vértices por Y y llama al rasterizador pasando punteros a
  los globals de vértice (`DAT_0046bfe0`/`_DAT_0046c028`/`DAT_0046c008` y sus campos x,y,u,v ya convertidos). La **página de
  textura activa** llega por el global `DAT_0046c0d4` (fijado en `gpuDrawPacket` desde la tabla de páginas) y el rectángulo de
  recorte/framebuffer por `DAT_0046c000`/`DAT_0046c048` + base/stride `DAT_00473ca4`/`DAT_00473cb4` (los fija el paquete
  `0x0f`). Salida: píxeles en el framebuffer 8 bpp; devuelve void.
- **x87**: indirecta — llama a `__ftol` `0x4521c4` (parte del 6,06 %).
- **Llamadas a shims**: **ninguna**. Sus callees son internos: `FUN_00450a40` `0x450a40`, `FUN_00450af3` (§2.2) y `__ftol`.
  → El rasterizador no cruza ningún shim: todo su costo es dynarec.
- **HLE: NO en qemu, SÍ candidato en A9 — reconciliado.** perf-cpu midió la **cota superior** con `HW_DIAG_SKIP_FT3`
  (≈11–12 % del CPU/frame en qemu) y descartó el HLE a nivel triángulo **por Amdahl en qemu**, dejándolo como candidato real
  en A9 (`docs/perf-cpu-072.md`). Mi aporte es de contrato y estructura: es **hoja respecto de los shims** (callees internos +
  `__ftol`), recibe **3 punteros a vértices** y el estado por globals (`DAT_0046c0d4` página, `DAT_0046c000`/`DAT_0046c048` +
  `DAT_00473ca4`/`DAT_00473cb4` clip/framebuffer), y produce **índices 8 bpp** → el test de igualdad es **exacto** (comparar el
  back buffer completo), sin ambigüedad de coma flotante. Si en A9 se ataca, la forma menos riesgosa es nativizar el par
  `swRasterFT3` + span (§2.2) y comparar byte a byte contra el camino actual.

### 2.4 `timerWaitFrame40Hz` — `0x00412df0` (3,03 % race + ~1,6 % de shims de reloj)
- **Propósito**: **espera de frame a 40 Hz** (busy-wait) y publica el `dt` en punto fijo 4.12 (`0x1000` = 1 frame).
- **Entradas/salidas**: sin argumentos. Salidas (globales): `DAT_00468f8c`/`DAT_00468f90` = **dt en 4.12** (`0x1000` = 25 ms),
  `DAT_00468f70/74` = contador previo (para el próximo delta).
- **Semántica exacta** (`[estático]`, decompilado completo):
  1. Init una vez (`DAT_0045f2a4 == -1`): `QueryPerformanceFrequency`; **si falla** → modo `timeGetTime` con escala
     **163,84**; si funciona → `QueryPerformanceCounter` y escala `_DAT_00468fa0 = 163840,0 / frecuencia` (**double**).
  2. Bucle: leer el contador actual (`DAT_00468f78/7c`) — en modo QPC reintenta mientras devuelva 0 —; `delta = actual − prev`
     (64 bits, con préstamo); multiplicar por la escala **en x87** (resultado en `ST(0)`); `__ftol()` → `dt`;
     **repetir mientras `dt < 0x1000`** (esto es el **spin de hasta 25 ms**).
  3. Guarda de 375 ms: si `dt > 0xf000` y el `dt` anterior era `≤ 0x1000` → `dt = 0x1000`.
  4. Avanza `prev = actual`.
- **x87**: **sí** — la escala es un `double` (`_DAT_00468fa0`) y la multiplicación queda en `ST(0)` para el `__ftol`.
  Es uno de los 35 callers de `__ftol` y explica buena parte de la muestra de `__ftol` **en el bucle de espera**.
- **Llamadas a shims**: **3** — `QueryPerformanceFrequency`, `QueryPerformanceCounter` (KERNEL32) y `timeGetTime` (WINMM).
  El perfil lo confirma: `GuestClock::qpc` 0,48 %, `GuestClock::now` 0,30 %, `GuestClock::take_stats` 0,83 % (junto con
  `arm_fist64_3`) ≈ **1,6 % del race** sólo en el camino de reloj del shim.
- **HLE: ya atacado por HW-056 — mi aporte es el contrato de verificación.** perf-cpu implementó `HW_QPC_IDLE_US` (el giro
  duerme) y advierte que **sin** yield el giro costaría hasta ~25 ms/frame de instrucciones en A9 (`docs/perf-cpu-072.md`,
  línea de `timerWaitFrame40Hz`); el perfil con `HW_QPC_IDLE_US=300` ya duerme. Lo que agrega esta lectura es **qué no se
  puede cambiar** al dormir: la única salida observable es `dt` en `DAT_00468f8c` (+`DAT_00468f90`) y `prev`; el valor debe
  seguir siendo `trunc(delta · escala)` con la escala `163840/freq` (**double**) o `163,84`, con el umbral `0x1000`, con la
  guarda `0xf000` bajo la condición "`dt` previo ≤ `0x1000`", y **sin retornar antes de los 25 ms**.
  Por lo tanto el sleep es aceptable **sólo** si `dt` no cambia más allá de lo que ya acepta el juego: medir la distribución
  de `dt` antes/después de HW-056 (no sólo el % de CPU) es la verificación que falta, y también protege el determinismo de
  los guiones de input por frame (`race_enter.script`).
  Si además se nativiza el cálculo de `dt`, desaparecen los **3 cruces de shim** y su `__ftol`, y queda el punto 1 de §3.

### 2.5 `FUN_00422320` — `0x00422320` (1,94 % race, 0,73 % menu)
- **Propósito**: **transformación y proyección de 4 vértices** (la etapa 3D→2D del render software): rotación/traslación por
  una matriz **3×3 en punto fijo 0.12** (globals `DAT_00476ee0…_DAT_00476ef0`, `>> 0xc`), traslación `DAT_00476ec0/c4/c8`,
  y **división en perspectiva** por Z (`(DAT_00476f00 << 16) / z`, con `z==0 → 1`), con recortes.
- **Entradas/salidas**: sin argumentos (stdcall `void(void)`); todo por globals: entra por los vértices del objeto en
  `DAT_00476fa8`/`b0`/`b8` (o el puntero `DAT_00476f00<<16`), sale por `DAT_00476fa0/a2/a4` (x,y,z proyectados s16) y
  `_DAT_00476ff8`/`_DAT_00476ffc` (x,y de pantalla s16, 2 vértices por par de globals). El byte/bitset `DAT_00476ecc` acumula
  **banderas de recorte**: `0x1000000`/`0x800000`/`0x400000` (x/y/z fuera de ±0x8000) y `0x4000` (fuera de ±0x2000 en
  pantalla), `0x40000` (z fuera de rango).
- **x87**: **no**. Todo entero (multiplicaciones 0.12 + `>>12`, división entera por Z). Es de los pocos hot que **no** toca
  la FPU.
- **Llamadas a shims**: ninguna. Hoja.
- **Callers**: **14** — `0x406350, 0x4073c0, 0x408160, 0x408e20, 0x40a160, 0x40bd70, 0x40d360, 0x40dd30, 0x40eaa0, 0x40f1d0,
  0x43f420, 0x448a90, 0x44c200, 0x44dba0` (muchos caminos de escena distintos → se llama varias veces por frame, por objeto).
- **HLE: SÍ, pero de ganancia más modesta y sin ahorro de x87/shim.** Al ser una rutina entera y hoja, lo único que aporta el
  HLE es **eliminar el costo de entrada/salida de dynablock y emitir código ARM mejor** (4 vértices con multiplicaciones).
  Como toda la aritmética es entera y determinista, **el contrato es exacto y testeable**: mismas salidas y **mismo bitset**
  de recorte para las mismas entradas de globals. Riesgo bajo, pero medir primero: si el costo está en el dispatch (que ya
  aparece con 9,59 % en race), el HLE ayuda; si está en los 14 call sites, no.

## 3. Qué queda abierto (reconciliado con perf-cpu)
Ya cerrado o medido por perf-cpu (`docs/perf-cpu-072.md`) — **no repetir**: `FISTP` inline con `VCVT` (winx86-0008, −4,6 % qemu);
giro del timer dormido (`HW_QPC_IDLE_US`, HW-056); HLE/AOT del span FT3 y del rasterizador **neutros en qemu** (domina el VFP
emulado) con cota superior de `HW_DIAG_SKIP_FT3` ≈11–12 %; dispatch indirecto bloqueado por BUG-045.
Lo que agrega esta RE (contrato exacto y estructura) y el próximo paso:

| # | Función | Aporte de esta RE | Próximo paso concreto |
|---:|---|---|---|
| 1 | `__ftol` | La secuencia completa (`FSTCW`/`OR AH,0xC`/`FLDCW`/`FISTP`/`FLDCW`) demuestra que el control word es **neto nulo**: se puede reconocer el **patrón entero**, no sólo el `FISTP`, y eliminar los dos `FLDCW` | Regla que matchee la secuencia/entrada de `__ftol`; cubrir NaN/saturación (`0x8000000000000000`); verificar que el guest no lea `FNSTSW`/`FSTSW` |
| 2 | `FUN_00450af3` | **Único caller** (`swRasterFT3`); escribe índices 8 bpp → contrato *byte-exacto*; layout de args aún parcial | Partir de la descripción de perf-cpu del lazo (`bl/bh`, `add ecx,ebp`/`adc bl,dl`) y completar el acumulador si se ataca en A9 |
| 3 | `swRasterFT3` | **Hoja de shims**; entradas = 3 punteros a vértices + globals de página/clip; produce índices 8 bpp → test exacto por back buffer | Si A9: nativizar el par rasterizador+span y comparar el back buffer completo |
| 4 | `timerWaitFrame40Hz` | Contrato de `dt`: escala **double** `163840/freq` (o 163,84), `trunc`, umbral `0x1000`, guarda `0xf000` condicionada, no retornar antes de 25 ms | Verificar **distribución de `dt`** antes/después de HW-056 (no sólo CPU) y el determinismo del guion de input |
| 5 | `FUN_00422320` | Entero (sin x87/shim), **14 callers**, contrato exacto incl. el **bitset de recorte** en `DAT_00476ecc` | Medir si su costo está en el dispatch (raramente: es hoja) o en los 14 sitios; HLE sólo si aparece en A9 |
| — | `inpUpdate` `0x41bb80` | **7,05 % del menú** (la más caliente del menú): polling DInput→shim por frame | Fuera del objetivo de fps de carrera; si el menú molesta, atacarlo ahí |

**Decisión que pide §0**: si la medición de fps de consola es con 3D=ON, el objetivo equivalente a (2)/(3) es
`gpuDrawPacket` `0x41c1e0` → `d3dDrawPrim` (ya inventariado en `docs/re-tracks.md` §4), no el rasterizador software.

## 4. Pendientes / verificación (no afirmar sin esto)
1. **Layout del acumulador de `FUN_00450af3`** (campos de `p2/p3/p5`, `DAT_004679c8`, `DAT_004679cc`) — leer el prologue en
   assembly; hoy es lectura parcial.
2. ~~**¿El guest lee la FPU status word?**~~ → **RESUELTO en §5**: sí lee, pero **sólo** el resultado de comparaciones
   (`FCOMP`/`FCOM`), nunca el del `FISTP`. El HLE de `__ftol` **no** tiene que imitar los flags x87; sí tiene que
   respectar el valor `0x8000000000000000` (indefinido entero) de `#IA`.
3. **Confirmar en qué modo corre la consola** (`settings.dat` línea 1) para elegir entre §2.3 y `gpuDrawPacket`: el perfil de
   HW-051 es SW.
4. **`0x0c300000`/`0x0c301000` (0,75 %/0,05 %)**: **no son código del guest** — el PE termina en `0x479000` (`SizeOfImage`
   = `0x79000`) y el mapa de Ghidra (`out/hw051-functions.csv`, 877 rangos) llega como máximo a `0x45c888`; ese EIP no está
   en el mapa y el reporte lo nombra por convención (`sub_0C300000`). Es un artefacto de la resolución PC host→EIP (zona de
   código generado / dynablock no resuelto). **No gastar tiempo de optimización ahí**; si el 0,75 % importa, el camino es
   arreglar la atribución en `pcprof_report.py`, no HLE.

## 5. Censo x87 del exe [estático] — respuesta a §4.2

**Método.** `objdump -d -M intel` sobre `HotWheels.exe` (sección `.text` = 115.747 instrucciones con **límites de
instrucción reales**) y contraste con el decode de REA/Ghidra 12.1.4 para los casos dudosos.
⚠ **Metodología (aplica a cualquier censo por bytes)**: un barrido crudo de bytes sobre `.text` da **falsos positivos**
—p. ej. `0x402ce0` parece un `FNSTCW` por bytes, pero Ghidra lo decodifica como `SUB EBX,ECX` (el byte `0xD9` es el
ModRM de esa instrucción). Por eso el censo de abajo sale del *listing*, no de un `grep` de opcodes.

### 5.1 Conteo real (todo `.text`)

| mnemónico | n | VAs (todas) |
|---|---:|---|
| **`FNSTSW`** | **109** | 0x41218c, 0x4121cc, 0x412f7f, 0x41576a, 0x415796, 0x415a1e, 0x415a37, 0x415a52, 0x415a91, 0x415aa2, 0x415abb, 0x415d3e, 0x415e87, 0x415ead, 0x415ec9, 0x415ed6, 0x415ef2, 0x415eff, 0x415f26, 0x417527, 0x41801e, 0x418033, 0x41803e, 0x41806b, 0x41807c, 0x41809e, 0x4180af, 0x4180c0, 0x4180e4, 0x4180fd, 0x418108, 0x418128, 0x418139, 0x418152, 0x418163, 0x418174, 0x4187fc, 0x419816, 0x419bc2, 0x41a387, 0x41a3c3, 0x41a88c, 0x41aaaf, 0x41ccbb, 0x41ccce, 0x41cce1, 0x41cd22, 0x41cd35, 0x41cf4f, 0x41cf62, 0x41d1c9, 0x41d269, 0x41d4e6, 0x41d65e, 0x41d6f7, 0x41da3d, 0x41e7f0, 0x41e889, 0x41ebcf, 0x41f981, 0x41f9cb, 0x41fc1f, 0x42052c, 0x420729, 0x420dee, 0x420e87, 0x4211cd, 0x4265fb, 0x426612, 0x426675, 0x42667e, 0x4266b9, 0x4266c4, 0x4271ca, 0x4271d9, 0x4271f2, 0x427201, 0x427226, 0x427235, 0x42725c, 0x42726b, 0x42735a, 0x4273c3, 0x4380c7, 0x443db7, 0x443f4f, 0x444042, 0x4441a0, 0x4441bc, 0x4486e3, 0x4498a2, 0x4498b7, 0x44d096, 0x44d0c5, 0x44d0d6, 0x44d113, 0x44d124, 0x44d1a4, 0x44d1b5, 0x44d1d3, 0x44e38f, 0x4539a5, 0x453a90, 0x45ad80, 0x45ad9a, 0x45adaf, 0x45adba, 0x45adcf, 0x45adda |
| `FSTSW` | **0** | — |
| `FISTP` (qword) | **1** | `0x4521dd` (dentro de `__ftol`) |
| `FSTCW` | **2** | `0x4521ca` (`__ftol`), `0x45739a` (helper de modo de redondeo) |
| `FLDCW` | **3** | `0x4521da`, `0x4521e0` (ambos en `__ftol`), `0x4573c2` |
| `FNCLEX` | 1 | `0x452186` (init del CRT) |
| `FWAIT` | 1 real | `0x4521ce` (en `__ftol`). Los 3 "FWAIT" que da un barrido de bytes (`0x429be9`, `0x429bf1`, `0x4516f9`) están **dentro de datos** desensamblados como código (los vecinos no son instrucciones válidas) |
| `FXAM` / `FNSTENV` / `FLDENV` / `FNSTCW` | 0 | — |

### 5.2 Los 109 `FNSTSW` son **todos** de comparación

| patrón | n | qué observa el guest | ejemplo |
|---|---:|---|---|
| `FCOMP`/`FCOM` → `FNSTSW ax` → **`test ah,0x1`** → `jne/je` | **101** | sólo **C0** (bit 0 de AH) | `0x41218c`: `fcomp QWORD PTR ds:0x45d1b8` / `fnstsw ax` / `test ah,0x1` |
| `FCOMP`/`FCOM` → `FNSTSW ax` → **`SAHF`** → `jbe/jb` | **8** | **C0→CF, C2→PF, C3→ZF** (comparación completa, incluido *unordered*) | `0x4539a5`: `fcomp QWORD PTR ds:0x45d190` / `fnstsw ax` / `sahf` / `jbe` |

**Cero** lecturas de status con otro patrón. O sea: **el status word x87 se usa exclusivamente como resultado de `FCOMP`/`FCOM`.**

### 5.3 El `FISTP` (único) nunca tiene su status leído

`0x4521dd: fistp QWORD PTR [ebp-0xc]` es el único `FISTP` del exe, dentro de `__ftol`. Instructions seguidas:
`0x4521e0 fldcw [ebp-0x2]` (restaura el control word) → `0x4521e3 mov eax,[ebp-0xc]` → `0x4521e6 mov edx,[ebp-0x8]`
→ `0x4521e9 leave` → `0x4521ea ret`. **No hay `FNSTSW` después**: el `#IA` del `FISTP` (NaN o fuera de rango `int64`)
**no lo observa nadie**.

⚠ No confundir con un barrido lineal: 60 de los `FNSTSW` quedan "al alcance" de un `call __ftol` si se camina la lista
lineal de instrucciones, pero en esos casos el `FNSTSW` pertenece a un `FCOMP` **posterior** del camino, no al `FISTP`
interno de `__ftol` (el `call`/`ret` corta el camino). La instrucción lo zanja: **un solo `FISTP`, y su status no se lee**.

### 5.4 Consecuencias para el HLE (esto es lo accionable)

1. **El HLE de `FISTP` (winx86-0008) NO tiene que modelar el status word x87.** La única cosa del `#IA` que el guest puede
   observar es **el valor**: `FISTP` con operando fuera de rango o NaN escribe el **entero indefinido
   `0x8000000000000000`** y setea C0/C2/C3. El HLE debe producir ese valor exacto (no 0 ni saturar) y **popear** el stack x87.
   La conclusión de §2.1/§3 no cambia; cambia la **razón**: no es "el status es irrelevante", es "**el status del `FISTP`
   no lo lee nadie**".
2. **Un HLE/AOT de `FCOM`/`FCOMP` sí tiene que ser exacto**, y es donde está el riesgo real:
   - los 101 sitios `test ah,0x1` observan **sólo C0** (ST0 < operando; en *unordered* C0 también vale 1 → el guest
     trata NaN como "menor");
   - los 8 sitios `SAHF` observan **C0/C2/C3 a la vez** → hace falta la semántica completa de *unordered*
     (NaN ⇒ C0=C2=C3=1 ⇒ ZF=PF=CF=1). Un `FCMP` de ARM no da eso: hace falta mapearlo.
3. **El control word está en su valor por defecto (round-to-nearest) en todo el exe salvo `__ftol`**: sólo hay 2 `FSTCW`
   en 115.747 instrucciones y uno de los dos es el de `__ftol` (que lo restaura → **neto nulo**). El otro
   (`0x45739a`/`0x4573c2`) es un helper "guardar → llamar → restaurar" de modo de redondeo. ⇒ una emulación x87 puede
   **asumir RC=nearest** para toda la aritmética salvo el interior de `__ftol` (que fija RC=truncate). Esto **simplifica**
   el modelo de punto flotante del port.
4. **`FWAIT` sólo aparece dentro de `__ftol`** (0x4521ce, entre `FSTCW` y el `OR AH,0xC`): si el HLE matchea la
   secuencia completa, ese `WAIT` también desaparece (es el que serializa la lectura del control word).
