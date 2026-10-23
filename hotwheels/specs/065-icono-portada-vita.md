# SPEC-065 — Icono y portada de Vita desde el juego original
- Feature: HW-076 · Dueño: sol-cores · Milestone: M6 · Estado: aprobada (host/ZIP; sin instalación física)
- Pedido usuario: icono de burbuja y portada de inicio originales; no modificar imagen durante gameplay.

## Problema
LiveArea usa imágenes genéricas dibujadas por el port. Los originales están disponibles localmente en extracted/iso/HWICON.BMP y autostart/background.bmp. SPEC-008 prohibía usarlos: este pedido posterior autoriza su uso en el paquete privado local, manteniendo prohibido commitear/distribuir material del juego en git.

## Contrato observable
`make_sce_sys.py --original-dir DIR --out OUT` lee HWICON.BMP para icon0.png y autostart/background.bmp para bg.png y startup.png. Icono128×128, fondo840×500, startup280×158, PNG indexado8bits (≤256 colores), opacos y sin interlace. Preservar imagen completa y proporción con bandas negras centradas si hacen falta; icono con nearest para conservar píxeles originales, portada con Lanczos y paleta adaptativa. No IA, logos inventados, recortes ni deformación. template.xml referencia los dos PNG correctos.

El generador sin --original-dir mantiene los genéricos deterministas de SPEC008. La opción original explícita falla si falta fuente/decodificación/Pillow; no disimula error con genéricos. Rechazar salida original dentro del sce_sys versionado para no sobrescribir artefactos propios con copyright. Originales/generados quedan en extracted/ y OUT ignorados; sólo scripts/tests/spec/docs se versionan.

package.sh genera SIEMPRE assets dentro de OUT/sce_sys y los empaqueta desde allí. Si HW_ORIGINAL_ART_DIR está definido no vacío, usar ese DIR explícito y fallar ante fuente ausente. Si la variable está vacía, pedir genéricos explícitamente (tests/distribución sin material). Si no está definida y existen ambas fuentes en extracted/iso, usar originales locales; si ninguna está disponible, conservar genéricos y anunciarlo. Fuente local parcial es error, no fallback silencioso. Loguear modo y directorio elegido. build_vita.sh sin cambios. No alterar TITLE_ID HWMR00002, versión, eboot, runtime, configuración/partida ni música.

## Criterios de aceptación
- [x] CA1 → tests/test_sce_sys.py: fixtures BMP sintéticos propios prueban origen correcto para icono/portadas, dimensiones, paleta≤256, proporción/bandas y referencias XML. Generación repetida da mismos bytes; ruta versionada bloqueada; fuente explícita ausente falla. Default genérico sigue igual al árbol versionado.
- [x] CA2 → mismo test (VitaSDK disponible): package con fuente sintética explícita genera VPK con seis entradas esperadas, TITLE_ID correcto y eboot idéntico byte a byte; no modifica sce_sys versionado. Fuente inválida falla; opción vacía genera genéricos. test_vpk.sh existente sigue verde.
- [x] CA3: inspección visual local de icono/portada/startup reales; VPK separado que reutiliza eboot de build identificada (e6749fb), SHA previo/post y hash de binario dentro del ZIP idénticos. Manifiesto de fuentes/scripts/config/hashes anterior al empaquetado y SHA final del VPK. Ningún material original añadido al índice. Review Sonnet/Luna antes de done.

## Diseño y límites de sistemas
Sólo trabajo host en empaquetado, fuera de ejecución del juego: sin hilos guest, GIL, audio, GPU, afinidades, prioridades o buffers runtime modificados. Vida de imágenes limitada a proceso generador; salida en staging propio evita carreras entre empaquetados y escritura al sce_sys compartido. Pillow sólo se importa en modo original; stdlib sigue suficiente para genéricos. Fallos dejan error explícito; no se reescribe eboot. No hay claim de rendimiento ni necesidad de build D2Vita.

## No objetivos / evidencia
Sin deploy, consola, CONSOLE_BUSY ni Vita3K durante A/B OFFICE. La visualización real de LiveArea instalada queda para integración coordinada posterior. No cambia HW075 ni sus artefactos congelados. Pruebas host/ZIP + inspección de PNG locales no demuestran instalación física.

## Cierre y congelado
Implementación078f73d; tests finales156d960; spec/tests/CA3 APRUEBA par-luna68f722a y código/final APRUEBA Sonnet en coordination/review-HW076-code.md. Cinco tests host, test_vpk genérico y mutantes centrales verdes. VPK privado c5b88984…52fbcc conserva eboot d6b0d94f…689ade de e6749fb. Publicación sin originales (HW_ORIGINAL_ART_DIR vacío). Sin deploy/LiveArea instalada; no alterar rutas tras el snapshot sin coordinar. P3 no bloqueantes: fallo de comparación de16k píxeles imprime diff lento; fixture vertical sería necesaria si se reutilizara el generador para fuentes verticales, fuera de estos BMP horizontales.
