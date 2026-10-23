# Auditoría D3D6 — BUG-047 (sol-gfx / HW-068)

Corte 2026-10-21. Evidencia de implementación: lectura de código local. Uso del exe: auditoría previa `docs/re-engine.md` §3 y traza Wine `docs/wine-reference.md` §8 (4.598 escenas). **La traza previa no cubre todas las pistas**: las filas «usado» no acreditan igualdad por pista. RE nueva respondida por rea-re en `docs/re-tracks.md` §§4–6 (Ghidra 12.1.4, estático); oráculo por pista a opus-gfx (HW-067).

| Camino / contrato | Exe: evidencia disponible | Shim / Replayer | RefRaster / GXP | Gap y prioridad causal |
|---|---|---|---|---|
| DrawPrimitive / FVF / stride | Una llamada en 0x446070 (constructor 0x445520): STRIP=5, FVF=0x104, 24 B, 3/4 vértices (estático); coincide Wine | COM slot 28 copia count×24, Device acepta prim 4..6 y FVF exacto | List/strip/fan; conserva orden | Cubierto para llamadas conocidas. Indexed/strided/VB sin implementar, no observados |
| RHW / perspectiva | El exe escribe RHW=1; Z no escrito; painter sin Z-buffer (estático §3.1) | DrawVertex y ring conservan z/rhw; `Replayer::to_clip` produce z=0,w=1 | RefRaster UV afín, shader GXP z=.5,w=1 | RHW=1 confirmado por RE nueva; Z queda sin escribir en el exe. H1 refutada para este source: replay descarta Z, backend deshabilita depth y shader fija Z. No agregar profundidad: alteraría orden esperado |
| Clipping / coordenadas | Paquetes XY s16; rechazo abs(coord)>0x1000 en exe; viewport packet 0x0f (estático) | Copia XY y convierte a clip; viewport→scissor | RefRaster recorta bbox, GXP clip/scissor | RefRaster no valida NaN/Inf/extremos antes de convertir a enteros; falta caso real que lo alcance. No cambiar renderer sin medir entradas |
| Longitud de comandos | Draw común 20/26 palabras. Texturas vistas 256×256 ARGB1555: 32.776 palabras (Wine previo) | **Antes n truncado a u16**; C1 corregido con escape y n completo u32 (`9da4281`) | Todos los lectores usan decoder común (incluido interp60 y carry) | Defecto host confirmado. Umbral 16 bpp 512×256 / ARGB8888 256×256 / ≥10.923 vértices. **No atribuido a BUG-047** hasta medir máximos reales |
| Blend / cull | SRC/DEST y alpha blend en §3.1/§8; cull=NONE | Pasa SRC/DEST/ALPHABLEND; cull NONE es no-op correcto | Factores por canal / glBlendFunc; cull deshabilitado | Cubierto para estados observados; otros RS devuelven OK + log y se ignoran |
| COLOROP / stages | Stage0 COLOROP=SELECTARG1; sólo textura sin color (estático/Wine) | Acepta COLOROP 2/4, resto ignorado; Replayer ignora TSS | RefRaster/GXP devuelve textura, sin modulación de color | SELECTARG1 cubierto; MODULATE y otros ops no implementados. No observados en traza previa |
| Sampler / mipmaps | MAG/MIN LINEAR; no mipmaps/address override observado | MAG gobierna d.linear; MIN no cambia selección de LOD; address no implementado | Wrap fijo; point/linear según política Vita; PAL8 lineal usa RGBA resuelta | MIN distinto de MAG, clamp/mirror y mipmaps son gaps genéricos. Verificar por pista antes de implementar |
| Formatos | Enum anuncia 1555/565/4444/PAL8; preferencia exe 1555; RE nueva de 0x445010: case PAL8 inalcanzable (corrige nota previa de VRAM<4 MB) | DDraw valida máscaras; Replayer convierte los 5 formatos, soporta 8888 aunque no lo enumera | RefRaster ARGB; GXP sube RGBA o índice+paleta | Conversión cubierta por tests D3D existentes. Probar con fuentes reales por pista, no inferir colisión de hash |
| Paleta por textura | SFP tiene página 256²+paleta; loader usa paleta global de primera página (estático §10) | DDraw tiene **una paleta global**; SetPalette sólo valida handle; SetEntries incrementa generación PAL8 de texturas | Snapshot lleva 256 colores; GXP preserva paleta en textura propia | Paletas COM independientes no cubiertas; RE nueva confirma paleta global de primera página, así que no hay uso independiente por pista en este camino |
| Lock/Unlock vivo | Texturas managed 256²; flags 0x821; SetTexture por draw (Wine) | Unlock aumenta generación; set_texture compara generación y toma snapshot propio | Hash FNV64 por handle incluye tamaño/formato/key/paleta/bytes; TexFree borra hash | RE nueva: cambio vivo 0x404a80 recrea textura entera, rebind por página 0x4453b0. Mutación sin SetTexture y SetColorKey sin bump quedan gaps genéricos; no reproducidos por exe |
| Blt sobre textura | BltFast 8bpp visible en UI; no Blt de textura en traza previa | BltFast no aumenta generación destino; 16/32bpp unsupported | Replayer no ve cambio sin nueva generación | RE nueva confirma blit CPU sobre página 256² + recreación completa de textura (0x404a80), no COM Blt. Gap genérico no causal para ese path |
| Drops / caché / vida útil | Cola 3 slots; escena usa texturas persistentes | Carry guarda último upload/free por ID y último binding por stage, en orden; lease impide reuso IN_FLIGHT | Recursos backend sólo consumer; snapshot evita memoria guest prestada | Golden carry existente + regresión grande C1 verdes. Ninguna colisión FNV64 observada; no confundir hipótesis con causa |

## C1: medición determinista antes / después (host)

`tests/run_d3d_ring_large.sh`, mismos fixtures del commit rojo `51eaf4e` y fix `9da4281`:

| Criterio | Antes | Después |
|---|---|---|
| Roundtrip n={0,65534,65535,65536,131080} y comando posterior | falla para n=65536 | todos completos, payload patchable correcto |
| Device→RefRaster 256² ARGB8888 y 512×256 RGB565 | falla upload/Present | 1 upload + 1 Present por formato; pixel(3,3)=0xff00ff00 |
| interp60, Draw de 10.923 vértices + upload grande | capture rechazada | capture/tick PASS; todas las XY interpoladas conservan el offset correcto |
| Carry desde READY descartado, textura 256² 8888 | textura/pixel faltante | 1 drop; textura conservada; pixel(2,2)=0xff00ff00 |
| Header escape truncado / longitud 0xffffffff | sin formato extendido | decoder rechaza sin avanzar cursor ni exponer payload |

Costo estático: +4 B sólo para comandos con n≥65535; formato pequeño idéntico. No es optimización ni medición de FPS.

Validación adicional: `run_d3d.sh` PASS (golden `0x6d85c1e26d7a5b3c` idéntico), CTest d3d_bridge/frame_queue/interp60 3/3 PASS. ASan/UBSan y fixture QEMU ARM PASS. Build Vita aislado PASS (1.936.046 B, SHA256 `fab6a3bd6204d18502daad7ed4428952066f4396a1421512358caef095a5a6c6`); revisión de diseño/código rev-gfx APRUEBA (`coordination/review-HW068-c1.md`). Oráculo por pista pendiente. No evidencia nueva de consola.
