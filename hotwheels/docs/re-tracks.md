# HW-071 — RE de pistas y render D3D6 (REA + Ghidra 12.1.4)
- Autor: `rea-re` · Fecha: 2026-10-21 · Evidencia: **[estático]** si no se indica.
- Binario: `extracted/original/Program_Files/HotWheels.exe` (430.080 B, SHA-256 `5418a6aa…1122`), base `0x400000`.
  Completá con `docs/re-engine.md` (mapas de nombre→VA de Ghidra 11); acá todo se cita por **VA**.

## 1. Cómo se elige la pista en el menú
- Tres pistas. **Dos órdenes distintos** (esto evita un error de índice):
  - **Orden del menú** (el que ve el jugador): **Wave, Office, Slumber** (índice de menú 0/1/2).
  - **Orden de la tabla** de archivos en `.rdata` (`0x45ff5c`): **Wave, Slumber, Office**.
  `state_raceLoad` (`0x4111b0`) **intercambia 1↔2** antes de indexar esa tabla → menú 0/1/2 ⇒ archivos Wave/Slumber/Office.
  Objetos de menú: `GFX_WAVEWATCH__` (`0x461bac`), `GFX_SLUMBERSUMMIT__` (`0x461bd0`), `GFX_ATTHEOFFICE__` (`0x461bbc`);
  carátulas `GFX_TRACKT__`/`GFX_TRACKT1__`/`GFX_TRACKT2__` (`0x46196c…`). Nombres de pantalla "Slumber" (`0x45fa6c`),
  "Office" (`0x45fa74`).
- Variables (`menuBindVar`): **`0x4722c0` = índice de pista del menú (0-based)**; `0x4722bc` = **modo** (0 = campeonato, que
  fuerza pista 0; 1 = carrera simple). Se cargan de `settings.dat` (`settingsLoad` `0x426bf0`): **línea 5 → `0x4722bc`**,
  **línea 6 → `0x4722c0`**, **línea 18 (short) → `0x4722e0` = pistas desbloqueadas** (con 0 el selector sólo habilita la playa
  y pisa la línea 6). Escrituras de menú sobre `0x4722bc`: `0x42545e, 0x4258a7, 0x425959, 0x425b81`.
  *(Esto cierra el pendiente que había en esta sección; verificado por I/O en `docs/bug047-oraculo.md` / HW-067, `6507df6`.)*
- Al confirmar: `menuShutdown` y salto a `state_raceLoad` (`0x4111b0`, alcanzado sólo por el puntero de estado, por eso
  Ghidra no lo crea como función). `0x4111b0` es el primer xref de `0x4722c0` y usa las tablas estáticas de abajo.

## 2. Archivos por pista (tablas estáticas en `.rdata`, [estático, volcado de punteros])
`state_raceLoad` indexa tablas por pista. Estructura confirmada leyendo los punteros:

| pista (índice) | SFP | script IA | HUD | sombreado | fuentes |
|---|---|---|---|---|---|
| Wave (0) | `data\wave\wave.sfp` `0x45ff74` | `data\wave\ai.dat` `0x45ffbc` | `data\wave\hud_low.tga` `0x45f2b8`, `hud_high.tga` `0x45f2d0` | `data\wave\shade.tab` `0x45f2e8` | `hw_small.txt` `0x45f2fc`, `hw_medium.txt` `0x45f314` |
| Slumber (1) | `…\slumber.sfp` `0x45ff88` | `…\slumber\ai.dat` `0x45ffd0` | `…\slumber\hud_low.tga` `0x45f35c`, `hud_high.tga` `0x45f378` | `shade.tab` `0x45f394` | `hw_small.txt` `0x45f3ac`, `hw_medium.txt` `0x45f3c8` |
| Office (2) | `…\office.sfp` `0x45ffa4` | `…\office\ai.dat` `0x45ffe4` | `…\office\hud_low.tga` `0x45f3e4`, `hud_high.tga` `0x45f3fc` | `shade.tab` `0x45f418` | `hw_small.txt` `0x45f430`, `hw_medium.txt` `0x45f44c` |

- Tabla de punteros a SFP/ai.dat: `[0x45ff5c]=wave.sfp, [0x45ff60]=slumber.sfp, [0x45ff64]=office.sfp,
  [0x45ff68]=wave\ai.dat, [0x45ff6c]=slumber\ai.dat, [0x45ff70]=office\ai.dat` (dos arrays de 3, orden Wave/Slumber/Office).
- Arte de carga y pantalla partida: `data\loading\wave.tga|slumber.tga|office.tga` (`0x461700/0x461718/0x461734`) y
  `split_wave|split_slumber|split_office.tga` (`0x46174c/0x461768/0x461788`).
- `VEHICLES.SFP` es **idéntico en las tres pistas** (389.400 B cada uno) → no distingue pistas.

## 3. Páginas de textura por pista (volcado de los 9 SFP con `tools/sfp_dump.py` + script propio)
Todas las páginas de todas las pistas son **256×256, 8 bpp + paleta 256×{R,G,B,pad}` (0x10404 B) con el mismo
`flags = 0x00010003`**. No hay diferencia de formato/paleta por pista.

| archivo | tamaño | páginas (chunk id 3) | chunks presentes |
|---|---|---|---|
| WAVE/WAVE.SFP | 1.900.852 | **25** | 1,2,3,4,5,6,7,11,12,13,14,15 |
| SLUMBER/SLUMBER.SFP | 1.701.708 | **18** | 1,2,3,4,5,6,7,11,14,15 |
| OFFICE/OFFICE.SFP | 2.050.508 | **24** | 1,2,3,4,5,6,7,11,14,15 |
| MENU/OBJECTS.SFP | 973.492 | 12 | 1,2,3,4,5,6,7,14,15 |
| MENU/CARS.SFP | 790.636 | 8 | idem |
| LOADING/OBJECTS.SFP | 67.704 | 1 | idem |
| */VEHICLES.SFP | 389.400 (×3) | 4 | 1,2,3,4,5,6,7,12,13,14,15 |

Límite: 32 páginas (`d3dSetTexPage` acepta índice `0..0x1f`); ninguna pista lo supera → **no hay aliasing de páginas**.

## 4. Render D3D6: inventario verificado (respuesta al pedido de HW-068 / sol-gfx)
Camino: `gpuDrawPacket` `0x41c1e0` → si `DAT_00473c9c` (is3D) y `DAT_00473c98` (dev3) → `d3dDrawPrim` `0x445520`.
Existe **una sola** llamada a `DrawPrimitive` en todo el exe (dentro de `0x445520`). No hay `DrawIndexedPrimitive`.

### 4.1 Por primitiva, en `FUN_00445520` (`0x445520`)
| Llamada (vtable de Device3) | Argumentos exactos | VA del sitio |
|---|---|---|
| `SetTextureStageState` (+0xa0) | `(0, D3DTSS_COLOROP=1, D3DTOP_SELECTARG1=2)`, **una vez por paquete**, antes del switch | `0x445581` (función) |
| `SetTexture` (+0x98) | `(0, *(page[tpage&0x1f]+0xc))` = `IDirect3DTexture2` de la página | por case (0x24/0x2c/0x34/0x3c) |
| `SetRenderState` (+0x58) | según modo ABR (tabla abajo), sólo si cambió `DAT_00462b20` | `0x445fd0…0x44605f` |
| `DrawPrimitive` (+0x70) | `(D3DPT_TRIANGLESTRIP=5, FVF=0x104, verts, 3 ó 4, D3DDP_DONOTLIGHT=0x10)` | `0x446070` |

- **FVF = 0x104** = `D3DFVF_XYZRHW(0x004) | D3DFVF_TEX1(0x100)`. **Stride 24 B** (x,y,z,rhw,u,v floats).
- **rhw = 1.0 siempre**: se escriben 4 constantes `0x3f800000` (slots `uStack_9c/84/6c/54`), una por vértice.
- **`z` NUNCA se escribe**: en los 24 B del vértice se asignan x,y,rhw,u,v y **los 4 slots de z quedan sin tocar**
  (`fStack_a0/88/70/58` no aparecen en el pseudocódigo). Es **basura de pila** (lo que haya quedado de llamadas
  previas). Con D3D real no importa porque **el motor no crea Z-buffer ni toca ningún render state de Z**
  (`D3DRS_ZENABLE/ZWRITEENABLE/ZFUNC` no se setean nunca). → **Exigencia para el shim: descartar z / no adjuntar
  depth buffer ni habilitar test de profundidad.** Si el backend Vita habilita Z (o usa z para ordenar), el
  resultado depende del historial de pila, que **cambia con la mezcla de primitivas de cada pista** → es una causa
  candidata de "polígonos que saltan" que sería distinta por pista. Ver §6 H1.
- u,v = `tabla[byte] * k`, tabla de 256 floats en `0x4727ec` inicializada con `i * 1/255` (`0x44552b`, `DAT_00472bec`)
  y `k` en `0x45d60c`.
- Recorte propio del juego antes de dibujar: `param_1[0x28]/[0x29]` = escala máxima de span horizontal/vertical,
  fijada por el paquete 0x0f como `(w*0x834)/0x140` y `(h*1000)/0xf0`. Si el span del triángulo lo supera → **no dibuja**
  (cull, no glitch). Además rechaza si `|coord| > 0x1000` (6+ comparaciones de `__ftol`).

### 4.2 Modos de mezcla (ABR) → render states
`uVar5` = modo ABR: si el bit 1 del código está apagado → 3 (opaco); si está prendido → `(tpage & 0x60) >> 5`.

| ABR | `SRCBLEND(0x13)` | `DESTBLEND(0x14)` | `ALPHABLENDENABLE(0x1b)` | VA verificado |
|---|---|---|---|---|
| 0 | `DESTCOLOR` (9) | `SRCCOLOR` (3) | 1 | `0x445fd8` (SRC) |
| 1 | `INVDESTCOLOR` (10) | `ONE` (2) | 1 | `0x445ff3` (SRC) |
| 2 | `ZERO` (1) | `INVSRCCOLOR` (4) | 1 | `0x44600e` (SRC) |
| 3 y formato **con** alfa | `INVSRCALPHA` (**6**) | `SRCALPHA` (**5**) | 1 | `0x44603a` (SRC) y `0x446047` (DEST) |
| 3 y formato **sin** alfa (`A bits==0`) | — | — | **0** | — |
Sólo se citan como VA los sitios verificados en bytes (`push imm8; push 0x13|0x14`): `0x445fd8`=9/0x13, `0x445ff3`=10/0x13,
`0x44600e`=1/0x13, `0x44603a`=6/0x13, `0x446047`=5/0x14. Los `DESTBLEND` de ABR0/1/2 y el `ALPHABLENDENABLE=0` se cargan
por registro en el pseudocódigo (`uVar21=3/2/4`), no por `push` inmediato → sin VA puntual. Los **números** coinciden con la
traza Wine de `FINDINGS.md` #18 (`(6/5)`, `(1/4)`, `(0xa/2)`). Corrección de sol-gfx (2026-10-21): `D3DBLEND_SRCALPHA=5`,
`D3DBLEND_INVSRCALPHA=6` (la tabla de re-engine.md §3.1 los nombraba al revés para el combo 6/5; se conservan los **números**).

**Consecuencia sobre la transparencia (importante, [estático] + inferencia):** `res = src·(1−srcA) + dst·srcA`. El packer (§4.5) deja
`alpha=0` en los texeles normales → esos salen **opacos** (`src·1`). Para que un texel "transparente" funcione, el valor "key"
(`fmt+0x3c`) que devuelve el packer tiene que traer el **bit de alfa en 1**: es decir el motor usa **alfa invertido (1 = transparente)**
y `COLORKEYENABLE` queda apagado. Un shim que asuma la convención estándar (1 = opaco) pinta al revés → **candidato fuerte de
"texturas mal" en BUG-047** (los menús son SW 8 bpp y no pasan por acá; la carrera sí). El valor exacto de `fmt+0x3c` se confirma
leyendo el registro de formato en runtime (log del propio exe `"Using %i bits textures (%i%i%i%i)"` `0x462b84`) — **pendiente**.

### 4.3 Estados fijos de inicialización (`d3dInitDevice` `0x444d00`)
`CULLMODE(0x16)=NONE(1)` · `TEXTUREMAG(0x11)=LINEAR(2)` · `TEXTUREMIN(0x12)=LINEAR(2)` (sin mipmaps ni `TEXTUREMIPFILTER`) ·
`COLORKEYENABLE(0x29)=TRUE` **sólo si el formato elegido tiene `A bits == 0`** (campo `+0x2b` del registro de formato).
**Nunca** toca Z, fog, dither, specular, shade, ni luces/matrices/materiales.

**Ausencias verificadas** (útiles para el oráculo): no hay `DrawIndexedPrimitive`, no hay vertex buffers ni `DrawPrimitiveStrided`,
no hay multitextura (una sola `SetTexture`/etapa 0; `SetTextureStageState` sólo toca la etapa 0), no hay fog, no hay Z-buffer,
no hay `SetTransform`, no hay `Execute`/execute buffers.

### 4.4 Elección de formato de textura (`FUN_00445010` `0x445010`)
Registro de formato (68 = 0x44 B, construido por `FUN_0044970` `0x444970` desde el `DDPIXELFORMAT`): `+0x08..+0x27` copia del
`DDPIXELFORMAT`, `+0x28/29/2a/2b` = **popcount de** dwRBitMask/dwGBitMask/dwBBitMask/**dwRGBAlphaBitMask** (bits por canal),
`+0x2c` = `dwRGBBitCount` (bpp), `+0x30/34/38/3c` = las máscaras R/G/B/**A**, `+0x40` orden RGB/BGR, `+0x41` palettizado.
Recorre la lista de `EnumTextureFormats` y devuelve el **primer** match en este orden:
1. `bpp==8 && palettizado` (case 0) → 2. `bpp==16 && A==1 && R==G==B==5` (**ARGB1555**) → 3. `bpp==32 && A=R=G=B=8`
(ARGB8888) → 4. `bpp==16 && A=R=G=B==4` (ARGB4444) → 5. cualquier 16 bpp con `A!=0` → 6. cualquier 32 bpp con `A!=0` →
7. `bpp==16 && A==0 && R==5 && G==6 && B==5` (RGB565) → 8. `bpp==16 && A==0 && R==8 && G==8` (nunca matchea).
**Detalle que puede ser un bug del original**: el `do{ iVar5=iVar5+1; switch(iVar5) … }while(iVar5<8)` **incrementa antes**
de entrar al switch, así que el **case 0 (8 bpp palettizado) nunca se alcanza** en esta corrida del bucle — contradice la
nota de re-engine §3.2 ("8 bpp pal. sólo si libre < 4 MB"). Con la lista típica de un HAL, el elegido es **ARGB1555**,
que es lo que midió Wine en `FINDINGS.md` #18 (`[wine] ARGB1555`). **Confirmación barata**: el propio juego loguea el
resultado — `"Using %i bits textures (%i%i%i%i)"` `0x462b84` y `"Using %i bits textures (palette)"` `0x462ba8`.

### 4.5 Conversion 8bpp→formato y transparencia (`FUN_00448230` `0x448230`, llamada desde `dxSurfCreateTexture` `0x446110`)
```
pack(R,G,B, fmt, mode):
  if fmt->bpp_field(+0x2c)==0 → return 0xffffffff
  if mode==0:  if R==0 && G==0 && B==0        → return fmt->key(+0x3c)   // NEGRO es la clave
  else:        if R<0x20 && G>0xfa && B<0x20  → return fmt->key(+0x3c)   // MAGENTA (255,0,255) es la clave
  v = (R>>(8- Rbits)) | (G>>(8-Gbits))<<Rbits | (B>>(8-Bbits))<<(Rbits+Gbits)   // o el orden BGR si fmt->+0x40
  if (mode!=0 && v==0) v = 1                    // nunca producir 0 (reservado como clave)
  return v
```
Consecuencias para el port:
- **El packer NO escribe bits de alfa, pero sí usa el MÁSCARA de alfa como valor "key".** Verificado en bytes: el constructor
  del registro de formato (`FUN_0044970` `0x444970`, llamado desde el callback de `EnumTextureFormats` `0x444fe0`) copia el
  `DDPIXELFORMAT` a `rec+8` y calcula `rec+0x28/29/2a/2b` = **popcount de dwRBitMask/dwGBitMask/dwBBitMask/dwRGBAlphaBitMask**,
  `rec+0x2c` = `dwRGBBitCount`, `rec+0x30/34/38/3c` = las máscaras R/G/B/**A**. Por lo tanto **`fmt+0x3c` = `dwRGBAlphaBitMask`**
  (para ARGB1555 = `0x8000`), no un color calculado.
  → El valor "key" que devuelve el packer es **el bit de alfa prendido con RGB en 0 = "negro con alfa=1"**, y con el blend de
  §4.2 (`SRC=INVSRCALPHA`, `DEST=SRCALPHA`) eso da `dst` = **texel transparente**; los texeles normales (alfa=0) dan `src` =
  **opacos**. Es la convención de **alfa invertido (1 = transparente)** — **confirmado, no inferencia**.
- Al crear la superficie, `dxSurfCreateTexture` sólo agrega `DDSD_CKSRCBLT` (`0x11007` en vez de `0x1007`) y fija el
  color key **si `A bits == 0`**; el valor de la clave es `*(byte*)(&DAT_00473c90 + 0x44c)` = **`DAT_004740dc` = índice
  de negro de la paleta global** (`0x4740dc`, calculado en `texPageCreate` `0x4048b0` y re-calculado en `FUN_00404a80`).
- La paleta global (256×RGBA) vive en `DAT_00473cda` y **la fija la primera página cargada** (`0x4048b0`, `bVar7 = (DAT_00468ce0==0)`).
  El índice de negro `DAT_004740dc` sale de buscar la entrada con R=G=B=0 en esa paleta → **depende de la paleta, no de la pista.**

### 4.6 Texturas: creación, Lock/Unlock y SetPalette (`dxSurfCreateTexture` `0x446110`)
1. `DD4::CreateSurface` (+0x18) `(dwSize 0x7c, flags 0x1007|CKSRCBLT, 256, 256, pixfmt elegido, caps TEXTURE(0x1000),
   caps2 TEXTUREMANAGE(0x10))`.
2. `Surface4::Lock` (+0x64) `(NULL, &ddsd, DDLOCK_WAIT|WRITEONLY|NOSYSLOCK = 0x821, NULL)`;
   conversión 8bpp→formato por software con `FUN_00448230`; `Surface4::Unlock` (+0x80) `(NULL)`.
   La conversión tiene 3 ramas por `bpp` destino: **8** (`(char*)(fmt+0xb)`), **16** (escribe `short`), **32** (escribe `uint`);
   cualquier otro bpp → **no convierte** (textura sin inicializar).
3. Si el formato destino es palettizado (`+0x41 != 0`): `DD4::CreatePalette` (+0x14) `(DDPCAPS_8BIT|ALLOW256=0x44,
   &DAT_00473c90+0x4c, …)` + `Surface4::SetPalette` (+0x7c); al liberar, `SetPalette(NULL)`.
4. QI a `IID_IDirect3DTexture2` (`0x45d740`) → se guarda para `SetTexture`; **no se usa ningún otro método de Texture2**.

## 5. Texturas actualizadas en vivo (Lock/Unlock en runtime) — la diferencia real por pista
- `d3dSetTexPage` (`FUN_004453b0` `0x4453b0`): con `param_2==0` libera **todas** las páginas; si no, libera la página
  `param_1` (`0..0x1f`) y guarda el objeto de textura nuevo en `(this + param_1*4 + 0x14)`. **No hay update parcial**:
  cada cambio reemplaza el objeto de textura completo (Release + nuevo `IDirect3DTexture2`).
- `FUN_00404a80` (`0x404a80`, llamada desde `0x441a90` y `0x426ec0`): **compone** una página en runtime —
  blitea un sub-rectángulo `w×h` (campos `+0xc/+0xe` de la fuente) en la página destino con **stride 256**
  (`dest[row*0x100 + col]`), reconstruye la paleta de la fuente y, opcionalmente, re-fija la paleta global
  (incluido `DAT_004740dc`); después **recrea la textura de la página** vía `dxSurfCreateTexture` + `d3dSetTexPage`.
- `FUN_00441a90` (`0x441a90`, el compositor de TGAs embebidos): abre un archivo con lector de chunks y junta dos bloques —
  **id 0x13 (19)** → `memPoolAlloc(DAT_00475530, size)`: **tabla de métricas de glifos** con punteros relativos que fija al cargar
  (`*(char**)(buf+0x14) = buf + *(int*)(buf+0x14); *(char**)(buf+4) = buf + *(int*)(buf+4)`); y **id 0x14 (20)** → pool 0:
  un **TGA embebido** (arranca `12 01 01 00 00 00 01 18`, `idlen=0x12`). Después llama a `FUN_00404a80(pagina_destino, tga, 1, offset)`.
  ⚑ **RESUELTO (2026-10-21)**: los ids 19/20 no están en los SFP sino en los **archivos de fuente por pista**
  `data\{wave,slumber,office}\HW_SMALL.TXT` (3.656 B) y `hw_medium.txt` (4.896 B), que **no son texto** sino contenedores
  chunked: id 1 (raíz) → id **19** (1.072 B, métricas, igual en todos) + id **20** (TGA 2.576/3.816 B: el **atlas de glifos**).
  Los tres `HW_SMALL.TXT` tienen **MD5 distintos** → el atlas de fuente **sí cambia por pista**.
- `FUN_00404a80` es en realidad el **cargador/compositor de TGA**: valida `byte[1]==1 && byte[2]==1` (cmapType/imageType = TGA
  paletizado sin comprimir), toma `idlen=byte[0]`, ancho de `+0xc`, alto de `+0xe`, la **paleta en `0x12+idlen`** y los píxeles de
  `+0x20`; blitea el rectángulo en la página destino con stride 256 y recrea la textura de esa página. → HUD, sprites, fuentes y
  cualquier TGA chico comparten las **mismas 32 páginas de 256×256** mediante blits sucesivos (un TGA = un rectángulo en una página).
  Consecuencia para el shim: una página puede recibir **varios** blits y **varios** `SetPalette` a lo largo de la carga; como cada
  blit recrea el objeto de textura completo, cualquier caché debe invalidarse por **identidad de objeto nuevo**, no por handle reusado.

### 5.1 Inventario por pista de las composiciones de textura [qemu, 2026-10-22]

**Medición**: 3 corridas `run_race.sh 30 sw` con `HW_TRACE=CreateFileA` y `settings.dat` por pista
(`tools/race-d3d-track{1,2,3}.dat`); se cuenta cada apertura de archivo (la traza emite 2 líneas por llamada).
Nivel: **[qemu]** (el guest es el exe real, bajo qemu-arm).

| | WAVE (track1) | OFFICE (track2) | SLUMBER (track3) |
|---|---:|---:|---:|
| **total de `CreateFileA`** | **99** | **99** | **99** |
| `data\<pista>\hud_high.tga` | 3 | 3 | 3 |
| `data\<pista>\hw_small.txt` | 1 | 1 | 1 |
| `data\<pista>\hw_medium.txt` | 1 | 1 | 1 |
| `data\<pista>\shade.tab` | 3 | 3 | 3 |
| `data\<pista>\ai.dat` | 3 | 3 | 3 |
| `data\<pista>\<pista>.sfp` | 1 | 1 | 1 |
| `DATA\<pista>\VEHICLES.SFP` | 1 | 1 | 1 |
| **composiciones de textura (TGA) por pista** | **5** | **5** | **5** |

- **El trabajo de composición es IDÉNTICO en las tres pistas** (5 composiciones: 3 del HUD + 1 atlas `hw_small` +
  1 atlas `hw_medium`; el resto son menú/pantalla de carga, idénticos en las tres). ⇒ **H2 queda refutada como
  diferenciador entre pistas**: no hay "pistas que componen más". La diferencia entre pistas es de **contenido**
  (chunk 5: 158/258/330 KB; vértices de física: 2.133/4.293/3.871), no de cantidad de composiciones.
- **`hud_low.tga` existe en disco (66.348 B) pero NUNCA se abre**: el juego usa `HUD_HIGH.TGA` (66.340 B) con los
  `settings.dat` por defecto. El shim no debe esperar que se cargue el "low" (y no debe fallar si falta).
- El patrón "3 aperturas" es uniforme para menú/carga/HUD/`shade.tab`/`ai.dat` (3 handles distintos: p. ej.
  `0x00f10110`, `0x00f10114`, `0x00f10118` para `hud_high.tga`): el guest **reabre** los mismos assets, no los cachea.
- **Identidad de pista confirmada por I/O** (independiente del render): track1 abre `data\wave\wave.sfp` +
  `DATA\WAVE\VEHICLES.SFP`; track2 `data\office\office.sfp` + `DATA\OFFICE\VEHICLES.SFP`; track3
  `data\slumber\slumber.sfp` + `DATA\SLUMBER\VEHICLES.SFP`. Confirma el mapeo menú→archivos de §1 y cierra el pendiente
  de **track3 = SLUMBER** que opus-gfx tenía sin verificar.
- Los strings del trace son **los VA del exe**: `data\wave\hud_high.tga` @ `0x45f2d0`, `hw_small.txt` @ `0x45f2fc`,
  `hw_medium.txt` @ `0x45f314` → **confirma contra runtime la tabla estática de §2** (y `hud_low.tga` @ `0x45f2b8`
  es la única que nunca se abre).

### 5.2 Layout del paquete de primitiva (compatible con PsyQ) [estático]

El paquete que consume `gpuDrawPacket` (`0x41c1e0`) tiene **stride 8 B por vértice**, `code` en `+0x07` y `tpage` en
`+0x16` (bits 0-4 = página 0..31, bits 5-6 = modo ABR). Layout completo, códigos y display list en
**`docs/re-chunks.md` §6**.

## 6. BUG-047 — qué distingue a las pistas no-playa (hipótesis refutables)
> **Estado del oráculo (HW-067, `6507df6`, niveles qemu/wine/Vita3K):** BUG-047 **no reproduce** fuera de la consola —
> WAVE/OFFICE/SLUMBER dan estructuralmente igual a Wine x86 en qemu/RefRaster *y* en GXP/Vita3K, con el **mismo ring**.
> Por lo tanto H1 (z) y H6 (convención de alfa del shim) quedan **excluidas como causa conocida** (si el shim estuviera
> invertido, GXP lo mostraría). Quedan como candidatas **sólo-hardware**: vida útil CPU→GPU de vértices/texturas mientras la
> GPU lee (el riesgo de §7.1) o runtime ARM real ≠ qemu. Fase 2: SPEC-052. Ver `docs/bug047-oraculo.md`.

El código de render es **un único camino**: no hay diferencia de render states/TSS/FVF/formato entre pistas
(§4). Tampoco hay diferencia de formato de página (§3, todas `0x00010003`). Por lo tanto la diferencia es **contenido**
(geometría/páginas/HUD) o **timing de carga**. Hipótesis evaluadas (se dejan documentadas con su refutación):

- **H1 — `z` indefinido (nueva, con evidencia fuerte).** El vértice D3D se arma con x,y,rhw,u,v; **z no se escribe** (§4.1).
  En D3D real no hay depth buffer → inocuo. Si el backend Vita/VitaGL habilita depth test o usa z para ordenar, el
  resultado depende de la **basura de pila**, que cambia con la mezcla de primitivas de cada pista. **Refutación**:
  capturar el mismo frame en Wine (referencia) vs. GXP y ver si el glitch aparece sólo en GXP; y en el shim, forzar
  z=0 y deshabilitar depth. Barato y decisivo.
- **H2 — composición de texturas en runtime (§5).** El compositor (`0x441a90` + `0x404a80`) arma la página con blits de TGA
  (fuentes por pista con atlas **distinto**, HUD, sprites). Si el shim no reproduce bien "reemplazar la textura entera de la
  página" tras cada blit, o reusa el handle del objeto viejo, las texturas salen mal en las pistas cuyas páginas se componen
  más (y no en la playa si compone menos). **Refutación**: contar llamadas a `0x404a80`/`0x441a90` por pista en Wine/qemu
  y verificar que el shim invalida por objeto nuevo.
  ⚑ **MEDIDO (2026-10-22, [qemu], §5.1)**: las tres pistas componen **lo mismo** (5 composiciones: 3 HUD + atlas
  `hw_small` + atlas `hw_medium`; 99 aperturas de archivo en las tres). ⇒ **la premisa de H2 ("las pistas cuyas páginas
  se componen más") es falsa**: no hay diferencias de cantidad de composición entre pistas. H2 queda **cerrada como
  diferenciador**; sigue siendo válida la parte de *mecánica* (invalidar caché por objeto nuevo, no por handle).
- **H3 — datos de geometría del chunk 5.** El layout interno de los chunks 5/6/7 sigue **pendiente** (re-engine §11).
  Sin él no se puede afirmar si la playa usa sólo `FT4` y las demás `GT3/GT4` (códigos que el camino D3D **descarta el
  color Gouraud**, §4.1) — pero eso cambiaría el *color*, no la geometría. Descartable para "polígonos que saltan".
- **H4 — pipeline/runtime (de HW-067).** Si el ground-truth Wine reproduce el glitch, es del motor de transformación
  (dynarec/x87) y no del backend: `FUN_0040ffb0` (`0x40ffb0`) recorre hasta 10 listas de objetos (`DAT_0047708c`, campos
  `+0x1c`/`+0x34`) y despacha la display list a `FUN_0041c190` (`0x41c190`), que recorre paquetes enlazados por el u32
  inicial (centinelas `1` = "siguiente sublista", `-1` = "anterior") y llama `gpuDrawPacket` por paquete.
- **H5 — colorkey/paleta.** El índice de negro `DAT_004740dc` y la paleta global dependen de la **primera página cargada**
  y de que exista una entrada exactamente negra en la paleta (§4.5). Si el orden de carga o la paleta por pista cambia el
  índice clave, la transparencia recorta lo que no debe. **Refutación**: leer `0x4740dc` y la paleta tras cargar cada pista.
- **H6 — convención de alfa (CONFIRMADA, dejar de tratarla como hipótesis).** El motor pinta la transparencia con **alfa invertido
  (1 = transparente)**: el packer devuelve `dwRGBAlphaBitMask` (0x8000 en ARGB1555) para los texeles clave y 0 para los normales, y
  el blend de ABR3 es `SRC=INVSRCALPHA`/`DEST=SRCALPHA` (ver §4.2/§4.5, verificado en bytes). Un shim que use la convención estándar
  (1 = opaco) invierte opacos y agujereados → causa directa de "texturas mal" **sólo en la carrera** (los menús son SW 8 bpp y no
  pasan por el packer). **Verificación en el shim**: convertir 8bpp con alfa=1 únicamente en los texeles negros/magenta (255,0,255).

## 7. Ownership, orden de memoria y bordes (razonamiento SO/hardware)
No hay código nuestro en juego, pero este análisis es el **contrato** que el shim debe cumplir; se deriva del mecanismo del exe (§4/§5).

### 7.1 Quién es dueño de cada buffer (CPU ↔ GPU)
- **Fuente de verdad (CPU, memoria de sistema)**: la página de 256×256 índices 8 bpp y su paleta 256×RGBA viven en un pool del
  propio juego (`FUN_00405690`, puntero de datos en `&DAT_00473c90 + 0x20`, paleta en `+0x4c`). El motor las lee/escribe con
  bucles de CPU (`rep movs`/byte a byte): **no hay DMA del juego** hacia/desde la página.
- **Derivado (GPU/propiedad del driver)**: el `IDirect3DTexture2` se crea con `caps2 = DDSCAPS2_TEXTUREMANAGE` → memoria de video
  administrada por el driver, y **no es fuente de verdad**. Prueba estática: ningún camino hace read-back de textura (no hay
  `Lock` de lectura sobre texturas, ni `GetDC`, ni `Blt` desde textura; ver §4.6). → El shim **no necesita** leer de vuelta.
- **Transferencia de propiedad explícita**: `dxSurfCreateTexture` toma la página por `Lock(0x821)`→conversión→`Unlock(NULL)`; el
  `Unlock` es la barrera que publica los bytes. Para los **WAV**, `dsLoadWav` (`0x44f0f0`) hace Lock→`memcpy`→Unlock y **libera el
  buffer temporal del guest** (`FUN_00452740`): después del Unlock, el dueño del PCM es el buffer de DSound, no el guest.
- **Identidad del recurso GPU cambia en cada update**: `FUN_00404a80` muta la página y luego `d3dSetTexPage` (`0x4453b0`) hace
  `Release` de la textura vieja y guarda una **nueva** en `(dev3 + idx*4 + 0x14)`. Consecuencias para el shim, por mecánica:
  1) una caché keyed por handle/VA del objeto viejo queda **vieja** (invalidar por objeto o generación, no por puntero reusado);
  2) el `Release` ocurre **en el hilo de render del guest**, mientras puede haber draws ya encolados hacia la GPU que referencian
     la textura vieja → hace falta **destrucción diferida con valla de frame** (liberar recién cuando la GPU terminó de leer), o
     se libera memoria que la GPU todavía puede estar leyendo (use-after-free, no un simple glitch visual).

### 7.2 Orden de memoria
- **Guest x86 (TSO)**: los writes a la página se ordenan solos y el `Lock/Unlock` del driver es la publicación. El motor **no**
  usa primitivas de sincronización (es monohilo, `re-engine` §0), así que no hay fences en el guest que imitar.
- **Anfitrión ARMv7 (modelo débil, Cortex-A9 + SGX)**: no hay coherencia implícita CPU↔GPU.
  - Antes de que GXM lea un buffer escrito por la CPU (página/upload): **limpiar D-cache a PoC** (`dcache clean`, rango con
    `DMB`/`DSB`), y si ese rango fue leído antes por la GPU, **invalidar** antes de reescribir. Vale para cada re-upload tras
    `FUN_00404a80`.
  - La cola de comandos es un productor (hilo de render del guest) / consumidor (hilo de submit/present). El índice de escritura
    debe publicarse con **release** y el de lectura con **acquire** (`dmb ish`/`ldrex-strex` según la implementación); publicar el
    índice sin barrera permite que la GPU vea el *slot* antes que sus bytes. Este es el mismo patrón que el repo ya documenta para
    el ring D3D/`interp60` (spec 037/043) — **no inventar un segundo mecanismo**.
  - Código generado no aplica acá (no hay dynarec en este camino), pero sí aplica el punto anterior para el buffer de vértices.

### 7.3 Bordes (0, máximo, wrap, error)
- **Índice de página**: `tpage & 0x1f` → siempre `0..31`; `d3dSetTexPage` acepta `0 <= idx < 0x20` y con `param_2 == 0` libera
  todas. Más de 32 páginas es **error fatal** previo ("Too many texture pages!!!", `0x45f1ac`): ninguna pista lo alcanza (§3), pero
  el shim no debe asumir "hay lugar".
- **Blit de TGA sin chequeo de límites**: `FUN_00404a80` escribe en `dest[(row+y)*0x100 + (col+x)]` con `w/h` tomados del header
  del TGA y `col/row` del llamador, **sin validar contra 256×256**. El guest es confiable aguas arriba, pero un TGA o un offset
  fuera de rango produciría un write fuera de la página: el shim debe **clampear/validar** en el borde (memoria guest del juego,
  no del shim: un OOB acá corrompe el pool del juego, no da un fallo limpio).
- **Packer**: `fmt+0x2c == 0` → devuelve `0xffffffff` (formato sin bits: caso de error del original, hay que propagarlo, no tragarlo).
  Multiplicar/desplazar por `Rbits==0` no ocurre porque el picker descarta esos formatos, pero el shim **no** debe asumir
  `Rbits/Gbits/Bbits/Bbits > 0` al convertir (§4.4).
- **Vértices**: el exe rechaza `|coord| > 0x1000` antes de dibujar (§4.1); el shim no debe "arreglar" ese rechazo (cambiaría el
  conjunto de primitivas dibujadas) ni saltar la validación para `NaN`/extremos en la conversión a enteros.

## 8. Pendientes concretos (chicos, en orden)
1. ~~Fijar el índice de pista~~ → **cerrado** (§1: `0x4722c0` = índice, `0x4722bc` = modo, swap 1↔2 en `0x4111b0`).
2. ~~Layout interno del chunk 5 (geometría) y de 6/7~~ → **cerrado parcialmente** (2026-10-22, `docs/re-chunks.md`):
   el chunk 5 es un **grafo de escena con punteros relativos** (fixup en `0x43bd40`…`0x43bf60`; objetos con 8 punteros
   relativos y jerarquía de hijos de 20 B); el chunk 6 son registros de 0x30 B; el **chunk 11 quedó decodificado por
   completo y verificado** contra los datos (cabecera de 12 u32 + arrays de 16 B = {s32 x,y,z} en punto fijo 1/16).
   **Lo que NO secerró**: la semántica de cada puntero del chunk 5 y el offset del nombre dentro del registro de chunk 6.
   Y la pregunta "qué códigos PsyQ usa cada pista" tiene respuesta negativa: **el código se elige en runtime** (§5.2),
   no está grabado por pista; la diferencia entre pistas es de contenido.
3. ~~Contar llamadas al compositor de TGA por pista~~ → **cerrado** (2026-10-22, [qemu], §5.1): **5 composiciones por
   pista, idénticas en WAVE/OFFICE/SLUMBER** (3 del HUD + atlas `hw_small` + atlas `hw_medium`), 99 aperturas de archivo
   en las tres. ⇒ **H2 queda cerrada como diferenciador entre pistas** (§6).
