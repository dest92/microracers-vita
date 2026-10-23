# BUG-032 — D3D6 XYZRHW a resolución nativa

## Problema y contrato

En D3D6 los vértices `XYZRHW` ya están transformados por el guest a píxeles del target (comúnmente 640×480 o 800×600). El backend VitaGL los convierte a clip space y rasteriza directamente sobre 960×544. `render_scale=native` hace explícito el mapeo a la región de salida: `fit` conserva 4:3 (725×544, centrado) y `stretch` usa 960×544. `render_scale=original` conserva el mapeo legacy `glViewport` con aspecto 4:3. Texturas y UV no se remuestrean por la opción de resolución.

La preferencia `[display] filter=point|linear|game` gobierna el muestreador de texturas D3D GXP: `point` fija `GL_NEAREST` (default nítido para sprites/texturas pequeñas con colorkey), `linear` prefiere `GL_LINEAR`, y `game` prefiere `D3DTSS_MAGFILTER`. Para textura con colorkey y alpha test, todas las políticas usan nearest: con el compare `GL_GREATER, 0`, alfa bilineal parcial pasa el test y puede escribir el RGB mezclado del texel transparente como halo. En los draws lineales restantes PAL8 se resuelve a RGBA antes de filtrar; nunca se interpolan índices de paleta. Esto no cambia el escalado final de framebuffer, ni el rasterizador de referencia/golden, que conserva el sampler explícito del `DrawCall`.

El renderer trata `x/y` enteros XYZRHW como centros de píxel fuente, igual que el `RefRaster` (su conversión NDC→sample resta 0.5). Por eso el destino es `dst_origin + (src + 0.5) * dst_size / src_size`: el medio píxel fuente se escala junto con la geometría, no se añade un offset fijo de 0.5 píxel de salida. El test compara este GXP nativo con el resultado legacy `glViewport + uniform` para 640×480 y 800×600, y rasteriza un strip sintético con scissor. Scissor convierte el rectángulo fuente a límites enteros de salida y origen inferior GXM. Colorkey/alpha test conserva el shader y los estados existentes; el path PAL8 lineal resuelve a RGBA antes de filtrar, evitando interpolar índices.

## Aceptación

- `hwvita.ini` parsea/serializa `[display] render_scale=native|original`; default native, valor inválido produce warning y conserva default.
- `hwvita.ini` parsea/serializa `[display] filter=point|linear|game`; default point. Para draws sin colorkey+alpha-test: point fuerza nearest, linear fuerza bilinear, game conserva el estado D3D. Los draws keyed con alpha test usan nearest para impedir halos; un helper host cubre esas combinaciones.
- Transformaciones para 640×480 y 800×600 coinciden con el viewport legacy sin nuevo desplazamiento; un strip sintético confirma interior, barras de fit y scissor reducido. `fit`/`stretch` producen las regiones arriba indicadas; `original` mantiene fit 4:3 incluso con target guest 800×600.
- El fixture host D3D XYZRHW/PAL8/colorkey/alpha y filtrado conserva el golden RefRaster; el helper nativo prueba mapping/half-pixel a 640×480 y 800×600. El build Vita y una carrera real guionada en Vita3K presentan bajo ambos modos.
- En Vita3K se guardan capturas native fit, native stretch y original; se mide duración de escena/present o FPS bajo mismo guion y se informa el cociente de píxeles/fill. No se extrapola ese costo a consola.

## Razonamiento bajo nivel

La configuración se lee antes de crear el backend y queda inmutable durante el proceso; la inicialización y los draws de vitaGL ocurren en el hilo de presentación/contexto GXM ya serializado, por lo que no hay estado de render concurrente que requiera un lock adicional. El GXP ya convierte XYZRHW a clip usando un uniform; native cambia sus coeficientes, no agrega trabajo por fragmento ni cambia ABI/atributos. El costo variable es el número de muestras destino: fit cubre 725×544 = 394.400 píxeles, stretch 960×544 = 522.240 (1,324× frente a fit), más el overdraw de la escena. Texturas/UV conservan tamaño y localidad; se mantienen alpha test y scissor para acotar fragmentos. VitaGL/GXM mantiene el framebuffer compartido con P8; el present P8 restaura su estado GL al finalizar cada D3D.

En D3D, `DrawCall::linear` refleja el estado del guest, mientras que la config es política persistente del host. El backend combina ambos justo al elegir shader/GL sampler; no muta el ring ni los estados COM compartidos. La preferencia no agrega sincronización o estado mutable entre hilos.

## No objetivos

No se cambia resolución guest, viewport D3D, resolución de texturas, HUD P8, tasa de frames, o renderer host RefRaster. La captura Vita3K sirve para validar mapping y costo relativo del emulador, no rendimiento final en hardware.
