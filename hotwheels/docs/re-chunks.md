# HW-071 — Layout de los chunks SFP y del paquete de primitiva (REA + Ghidra 12.1.4)
- Autor: `rea-re` · Fecha: 2026-10-22 · Evidencia: **[estático]**.
- Binario: `extracted/original/Program_Files/HotWheels.exe` (430.080 B, SHA-256 `5418a6aa…1122`), base `0x400000`.
- Cierra los pendientes §8.2/§8.3 de `docs/re-tracks.md` (layout de los chunks 5/6/7) y agrega el **layout del paquete
  de primitiva**, que no estaba documentado en ninguna parte.
- Método: REA/Ghidra 12.1.4 (`open_binary` + `procedure_pseudo_code` + `procedure_assembly`) y `objdump -d -M intel`
  (listing con límites de instrucción reales) para las instrucciones; los **datos** se validaron parseando los 9 `.SFP`
  del juego con `tools/sfp_dump.py` y scripts propios sobre `extracted/original/Program_Files/DATA/`.

---

## 1. `sfpLoad` (`0x43b670`): despacho de chunks [estático]

`sfpLoad` recorre el árbol de chunks (`sfpChunkBegin` `0x43cc30` → id, `sfpChunkHasMore` `0x43cc90`,
`sfpChunkSkip` `0x43cd00`) y **para cada id hace una de dos cosas**: dejar el payload como **blob crudo** (malloc +
`fread(size-8)`) o como **array de registros de tamaño fijo** (el count se deriva del tamaño del chunk con la división
mágica del compilador).

| id | qué hace `sfpLoad` | VA del caso | count |
|---|---|---|---|
| 3 | `malloc(size)` + `fread(size-8)` → `texPageCreate(0, flags, paleta@+4, índices@+0x10004)` (`0x43bb04`) | `0x43bab9` | — |
| **5** | **blob crudo**: `malloc(size)` + `fread(size-8)` → puntero local | `0x43b87c` | — |
| 6 | blob + **count** | `0x43b8f5` | `(size-8)/0x30` |
| **7** | **blob crudo** | `0x43b8b6` | — |
| 8 | blob + count | `0x43b933` | `(size-8)/0x2c` |
| 9 | **blob crudo** | `0x43b960` | — |
| 10 | blob + count | `0x43b981` | `(size-8)/0x28` |
| 11 | blob + `fread` → **`FUN_00430090(0x430090)`** (parser de física) | `0x43ba3d` | — |
| 12 | **blob crudo** | `0x43b9c5` | — |
| 13 | blob + count | `0x43b9e3` | `(size-8)/0x1c` |
| 14 | blob, **sólo** si se pasó un descriptor `.dat` | `0x43bb32` | — |
| 15 | blob + count | `0x43bb77` | `(size-8)/0xc` |

Los count salen de divisiones por constante mágica (0x30 → `mul 0xaaaaaaab`, 0x2c → `0xba2e8ba3`, 0x28 → `0xcccccccd`,
0x1c → `0x24924925`, 0xc → `0xaaaaaaab`): **confirma** la tabla `RECSZ` de `docs/re-engine.md` §10.

**Sizes por pista** (payload = `size-8` del chunk) [estático, datos reales]:

| archivo | chunk 5 | chunk 7 | chunk 6 (n) | chunk 13 (n) |
|---|---:|---:|---:|---:|
| `WAVE/WAVE.SFP` | 158.808 B | 668 B | 46 | 1 |
| `SLUMBER/SLUMBER.SFP` | 329.760 B | 1.668 B | 118 | — |
| `OFFICE/OFFICE.SFP` | 258.516 B | 1.576 B | 110 | — |
| `MENU/OBJECTS.SFP` | 171.860 B | 444 B | 29 | — |
| `MENU/CARS.SFP` | 255.660 B | 504 B | 28 | — |
| `WAVE/VEHICLES.SFP` | 109.180 B | 3.100 B | 205 | 1 |

## 2. Chunk 11 (física): layout **completo y verificado** [estático + datos]

`FUN_00430090` (`0x430090`) recibe el blob y lo desarma así (pseudocode Ghidra):

```
base = param_1
arr[0] = base + param_1[0]      // offsets RELATIVOS al payload
arr[1] = base + param_1[1]
arr[2] = base + param_1[2]
arr[3] = base + param_1[3]
arr[4] = base + param_1[4]
arr[5] = base + param_1[5]
nVerts = param_1[7]             // count del array arr[1]
nPolys = param_1[10]            // count del array arr[5]
// por cada vértice (registro de 16 B): x,y,z = 3 × int32, cada uno <<= 4  (punto fijo 1/16)
//   y se trackea el máximo de la componente Y  →  DAT_00475d14 (+25000)
// idem para el segundo array (arr[5], nPolys)
```

⇒ **Layout del chunk 11**: cabecera de **12 u32** (6 offsets relativos + counts) seguida de arrays de **registros de
16 B = {s32 x, s32 y, s32 z}** en punto fijo de **1/16** (el runtime multiplica por 16: `x<<4`).

**Verificado contra los 3 archivos** (offsets dentro del rango, counts coherentes, coordenadas plausibles):

| pista | off[0..5] | nVerts ([7]) | n2 ([8]) | n3 ([9]) | nPolys ([10]) | len payload |
|---|---|---:|---:|---:|---:|---:|
| WAVE | 48, 27192, 61320, 65296, 71896, 71896 | 2.133 | 497 | 1 | 0 | 71.904 |
| SLUMBER | 48, 75600, 137536, 147352, 163400, 163400 | 3.871 | 1.227 | 1 | 0 | 163.408 |
| OFFICE | 48, 78408, 147096, 161440, 184400, 184400 | 4.293 | 1.793 | 1 | 0 | 184.408 |

Los primeros vértices de las 3 pistas comparten `y`/`z` (sólo cambia `x`) → las pistas comparten la forma inicial del
trazado (sanity check). **`nPolys` = 0 en las tres**: la malla de polígonos de física no viene en el chunk 11 de las
pistas (el log "Physics, polys: %i, verts: %i" imprimirá polys=0); la geometría dibujable está en el chunk 5.

## 3. Chunk 5 = grafo de escena serializado con punteros **relativos** [estático]

`sfpLoad` no lo parsea: lo deja como blob. Lo parsea el **loop de fixup** (`0x43bd40`…`0x43bf60`), que **rebasa** los
punteros: todo `u32` que apunta a otro objeto es un **offset relativo a la base del blob**, y al cargar se le suma la base.

Layout de un **objeto** del chunk 5 (decodificado del fixup):

| offset | tipo | significado |
|---|---|---|
| `+0x00` | u8 | cantidad de **hijos** (`n`) |
| `+0x04` | u16 | índice en una tabla de colores (`table[ecx]`, `+0x08` del elemento) |
| `+0x06` | u16 | índice en una tabla de u16 (pares de coordenadas) |
| `+0x08` | s16 | cantidad de la 1ª sublista de hijos |
| `+0x0a` | s16 | cantidad de la 2ª sublista de hijos |
| `+0x0c` | u32 | puntero relativo (hijo) |
| `+0x10` | u32 | puntero relativo (hijo) |
| `+0x14` | u32 | puntero relativo |
| `+0x18` | u32 | puntero relativo |
| `+0x1c` | u32 | puntero relativo |
| `+0x20` | u32 | puntero relativo |
| `+0x24` | u32 | puntero relativo |
| `+0x28` | u32 | puntero relativo **a otra base** (no al blob) |

Un **hijo** tiene registros de **0x14 B (20 B)**: `+0x00` s16 = cantidad de nietos, `+0x04` = offset relativo de un
array de nietos (elementos de **8 B** de separación), `+0x08`/`+0x0c` = punteros relativos a las 2 sublistas.

**Límite honesto**: está probado el *mecanismo* (punteros relativos rebasados + jerarquía de hijos de 20 B) y las
tablas de índice, pero **no** la semántica de cada puntero (`+0x0c`…`+0x24`) ni a qué campo del objeto apunta cada uno.
No hace falta para el port (el port re-implementa el camino de dibujo, no el grafo), pero queda como pendiente real si
alguien quiere editar pistas.

## 4. Chunk 13 = índice del chunk 5 [estático + datos]

Registro de **0x1c B**: `+0x00` **u16 = código de tipo**, `+0x04` **u32 = offset dentro del chunk 5**.
El fixup de `sfpLoad` (`0x43bd86`) lee el código y trata **7**, **0x23** y **0x27** de forma especial: para `0x23`/`0x27`
parchea el `u16` en `+0x04` del objeto destino con una entrada de tabla de color (`table[i]`, `+0x08`) y el `u16` en
`+0x06` con una entrada de la tabla de u16, y hace `|= 0x20` sobre `+0x04`.

Contenido real: **1 registro** en `WAVE.SFP` y 1 en `VEHICLES.SFP`; `SLUMBER`/`OFFICE`/menú **no tienen chunk 13**.
El registro de WAVE: `code=0x0000`, offset al chunk 5 = `0x38`, y `+0x08 = 0x00070101`, `+0x0c = 0x00400000`,
`+0x10 = 6`.

## 5. Chunk 6 = catálogo de objetos (0x30 B por registro) [estático]

Registros de **0x30 B**; el **nombre no está en el offset 0** (el primer campo no es texto: al volcarlo crudo aparecen
bytes de control). `objFindByName` (`0x43e150`) busca nombres `GFX_*`/`NUL_*`/`APA_*`/`ANM_*` → el nombre está en otro
offset del registro. **Pendiente**: hallar el offset exacto del nombre (los nombres por pista están sin resolver).

## 6. Layout del **paquete de primitiva** (compatible con PsyQ) [estático]

El paquete es lo que consume `gpuDrawPacket` (`0x41c1e0`) y, en D3D, `d3dDrawPrim` (`0x445520`):

```
+0x00  u32   next        (puntero al siguiente paquete: lista enlazada)
+0x04  u8    flags       (0x80 en FT3; 0x21 en FT3-semi)
+0x05  u8    flags       (0x80 / 0x21)
+0x06  u8    flags       (0x23 en FT3-semi)
+0x07  u8    CODE        (primitiva)
+0x08  s16   v0.x        \
+0x0a  s16   v0.y         |
+0x0c  u8    v0.u         |  vértices: stride 8 B
+0x0d  u8    v0.v         |  (x s16, y s16, u u8, v u8, +2 B)
+0x0e  u16   clut         /
+0x10  s16   v1.x
+0x12  s16   v1.y
+0x14  u8    v1.u
+0x15  u8    v1.v
+0x16  u8    tpage       (bits 0-4 = página de textura 0..31; bits 5-6 = modo ABR)
+0x18  s16   v2.x
+0x1a  s16   v2.y
+0x1c  u8    v2.u
+0x1d  u8    v2.v
+0x1e  u16   (libre)
+0x20  …     v3 (sólo FT4/GT4) → stride 0x28
```

- **Códigos** (verificados en los **escritores**, no sólo en comparadores):
  `0x24` FT3 (`0x4165bf`, `0x44c68c`) · `0x26` FT3 semitransp (`0x42b07b`, `0x42b332`) · `0x2c` FT4 (`0x4122fc`,
  `0x412af5`) · `0x34`/`0x3c` GT3/GT4 (consumidos en `0x40b5e3`/`0x40b5e8`: la rama **Gouraud**, que en D3D descarta
  el color) · `0x0f` = "área de dibujo" (rect 12+12 bit empaquetado en `+0x04`/`+0x08`).
- **El código se elige en runtime**, no está grabado por pista ⇒ la pregunta "qué códigos PsyQ usa cada pista" no tiene
  respuesta en los datos: depende del **grafo del chunk 5** de cada pista (tipos de objeto → FT3/FT4/GT3/GT4). Con lo
  verificado, la diferencia entre pistas es de **contenido** (chunk 5: 158 KB / 329 KB / 258 KB; vértices de física:
  2.133 / 3.871 / 4.293), no de formato.
- **Despacho por código**: ambos caminos (D3D `0x4460c4`+`0x4460dc`, SW `0x41cda8`+`0x41cdc0`) indexan
  `code - 0x0f` con un rango válido `0x0f..0x3e` (`cmp eax,0x2f` / `ja` ⇒ fuera de rango no dibuja).
- **Display list**: `FUN_0041c190` (`0x41c190`) recorre un array de u32 donde `1` = "el siguiente u32 del array es un
  puntero a paquete", `-1` = "volver atrás", `0` = fin, y cualquier otro valor = puntero a paquete (de ahí sale el
  siguiente por `paquete+0`). De ahí el `+0x00 = next` del paquete.

## 7. Pendiente que queda (real, no cosmético)
1. **Nombre en chunk 6**: offset exacto del string dentro del registro de 0x30 B (y catálogo por pista).
2. **Semántica de los 8 punteros del chunk 5** (§3) y del código de tipo del chunk 13.
3. `chunk 7`/`9`/`12` (blobs crudos) no están decodificados: no se sabe quién los consume ni su layout.
